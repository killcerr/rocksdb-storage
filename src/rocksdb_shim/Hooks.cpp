#include "rocksdb_shim/Config.h"
#include "rocksdb_shim/RocksDb.h"

#include "ll/api/mod/NativeMod.h"
#include "leveldb/db.h"

#include <chrono>
#include <system_error>

#include "ll/api/memory/Hook.h"

using namespace ll::literals::memory_literals;

namespace rocksdb_shim {

ShimsConfig& shimsConfig() {
    static ShimsConfig config;
    return config;
}

namespace {

std::pair<std::unique_ptr<leveldb::DB>, leveldb::Status> makeIoError(std::string msg, std::string detail) {
    leveldb::Status status = leveldb::Status::IOError(
        leveldb::Slice(msg.data(), msg.size()),
        leveldb::Slice(detail.data(), detail.size())
    );
    return {nullptr, status};
}

// Copies the existing leveldb database into a fresh rocksdb database and returns
// a rocksdb-backed handle. The leveldb data is never modified or removed, so an
// aborted migration only ever leaves the rocksdb directory incomplete.
std::pair<std::unique_ptr<leveldb::DB>, leveldb::Status> migrateLevelDbToRcks(
    auto&&                 originOpen,
    leveldb::Options const& options,
    std::string const&      name,
    std::filesystem::path const& rocks
) {
    leveldb::DB* ldb = nullptr;
    if (leveldb::Status s = originOpen(options, name, &ldb); !s.ok()) return {nullptr, s};
    std::unique_ptr<leveldb::DB> ldbGuard(ldb);

    // Whatever a previous attempt left behind is discarded; leveldb is the only
    // source of truth here.
    std::error_code ec;
    std::filesystem::remove_all(rocks, ec);

    std::string                  err;
    std::unique_ptr<rocksdb::DB> rocksDb = openRocksDb(rocks, &err);
    if (!rocksDb) {
        std::filesystem::remove_all(rocks, ec);
        return makeIoError("rocksdb open failed", err);
    }

    ll::mod::NativeMod::current()->getLogger().info(
        "rocksdb-storage: migrating leveldb database {} into {}", name, rocks.string()
    );

    auto const start   = std::chrono::steady_clock::now();
    auto       lastLog = start;
    bool       logged  = false;

    size_t                     keys = 0, bytes = 0;
    bool                       dirty = false;
    rocksdb::WriteBatch        batch;
    leveldb::ReadOptions       ro;
    std::unique_ptr<leveldb::Iterator> it(ldb->NewIterator(ro));
    if (!it) return makeIoError("leveldb NewIterator failed", name);
    auto fail = [&](leveldb::Status const& status) {
        rocksDb.reset();
        std::filesystem::remove_all(rocks, ec);
        return std::pair<std::unique_ptr<leveldb::DB>, leveldb::Status>{nullptr, status};
    };
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        auto k = it->key();
        auto v = it->value();
        batch.Put(rocksdb::Slice(k.data(), k.size()), rocksdb::Slice(v.data(), v.size()));
        dirty = true;
        ++keys;
        bytes += k.size() + v.size();
        if (keys % 4096 == 0) {
            rocksdb::WriteOptions wo;
            wo.sync = false;
            if (rocksdb::Status ws = rocksDb->Write(wo, &batch); !ws.ok()) {
                return fail(leveldb::Status::IOError("rocksdb write failed", ws.ToString()));
            }
            batch.Clear();
            dirty = false;

            auto const now = std::chrono::steady_clock::now();
            if (!logged || now - lastLog >= std::chrono::seconds(2)) {
                logged               = true;
                lastLog              = now;
                double const elapsed = std::chrono::duration<double>(now - start).count();
                ll::mod::NativeMod::current()->getLogger().info(
                    "rocksdb-storage: migrating {}: {} keys, {:.1f} MiB, {:.0f} keys/s",
                    name,
                    keys,
                    static_cast<double>(bytes) / (1024 * 1024),
                    static_cast<double>(keys) / elapsed
                );
            }
        }
    }
    if (leveldb::Status st = it->status(); !st.ok()) {
        return fail(leveldb::Status::IOError("leveldb iteration failed", st.ToString()));
    }
    if (dirty) {
        rocksdb::WriteOptions wo;
        wo.sync = false;
        if (rocksdb::Status ws = rocksDb->Write(wo, &batch); !ws.ok()) {
            return fail(leveldb::Status::IOError("rocksdb write failed", ws.ToString()));
        }
    }
    it.reset();

    rocksdb::FlushOptions flush;
    flush.wait = true;
    if (rocksdb::Status fs2 = rocksDb->Flush(flush); !fs2.ok()) {
        return fail(leveldb::Status::IOError("rocksdb flush failed", fs2.ToString()));
    }
    if (rocksdb::Status ws = rocksDb->SyncWAL(); !ws.ok()) {
        return fail(leveldb::Status::IOError("rocksdb wal sync failed", ws.ToString()));
    }
    rocksDb.reset();  // drop the rocksdb handle
    ldbGuard.reset(); // and the leveldb handle, so neither keeps files open

    ll::mod::NativeMod::current()->getLogger().info(
        "rocksdb-storage: migrated {} into {} (keys={} bytes={} {:.1f}s), leveldb database kept at {}",
        name,
        rocks.string(),
        keys,
        bytes,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
        name
    );

    auto db = makeRocksDb(rocks, &err);
    if (!db) return makeIoError("rocksdb open failed", err);
    return {std::move(db), leveldb::Status::OK()};
}

// Picks the engine for a world database: an existing rocksdb directory, an
// existing leveldb directory (migrated here, or passed through to the original
// function) or a new empty rocksdb database.
std::pair<std::unique_ptr<leveldb::DB>, leveldb::Status>
openWithPolicy(leveldb::Options const& options, std::string const& name, auto&& originOpen) {
    // Paths arrive as UTF-8 bytes; building the path from those bytes keeps
    // non-ASCII world names intact independently of the ANSI code page.
    auto dbPath = std::filesystem::path(std::u8string(name.begin(), name.end()));
    // The leveldb directory stays where it is; rocksdb data lives next to it.
    auto rocks = dbPath.parent_path() / "rocksdb";

    // rocksdb writes an OPTIONS-<n> file next to CURRENT, leveldb never does.
    std::error_code ec;
    bool           rocksMarker = false;
    for (auto const& entry : std::filesystem::directory_iterator(rocks, ec)) {
        if (entry.path().filename().string().rfind("OPTIONS-", 0) == 0) {
            rocksMarker = true;
            break;
        }
    }
    bool const hasLevelDb = std::filesystem::exists(dbPath / "CURRENT", ec);
    if (rocksMarker) { // rocksdb data present
        std::string                 err;
        std::unique_ptr<leveldb::DB> db = makeRocksDb(rocks, &err);
        if (!db) return makeIoError("rocksdb open failed", err);
        ll::mod::NativeMod::current()->getLogger().info(
            "rocksdb-storage: opened rocksdb database at {}", rocks.string()
        );
        return {std::move(db), leveldb::Status::OK()};
    }

    if (hasLevelDb) { // leveldb data still present
        if (!shimsConfig().migrateExisting.load(std::memory_order::relaxed)) {
            ll::mod::NativeMod::current()->getLogger().warn(
                "rocksdb-storage: migrateExisting=false, opening {} with stock leveldb", dbPath.string()
            );
            leveldb::DB*  ldb = nullptr;
            leveldb::Status s = originOpen(options, name, &ldb);
            return {std::unique_ptr<leveldb::DB>(ldb), s};
        }
        return migrateLevelDbToRcks(originOpen, options, name, rocks);
    }

    // nothing on disk yet
    std::string                 err;
    std::unique_ptr<leveldb::DB> db = makeRocksDb(rocks, &err);
    if (!db) return makeIoError("rocksdb open failed", err);

    // The game keeps working off the leveldb directory even when the database
    // itself lives in rocksdb: its save machinery (SaveCommand::saveHold and the
    // file listing that "save query" reports) walks that directory. With a world
    // whose leveldb directory was never created, "save hold" prints "Saving..."
    // and "save query" never reports "Data saved.", so the world is only written
    // on shutdown. Creating the leveldb database here costs a handful of files
    // and keeps that path intact; every later open checks for rocksdb first, so
    // the stub is never read.
    leveldb::DB*  stub = nullptr;
    leveldb::Status stubStatus = originOpen(options, name, &stub);
    if (stub != nullptr) {
        delete stub;
    } else {
        ll::mod::NativeMod::current()->getLogger().warn(
            "rocksdb-storage: could not create the leveldb directory {}: {}",
            dbPath.string(),
            stubStatus.ToString()
        );
    }

    ll::mod::NativeMod::current()->getLogger().info(
        "rocksdb-storage: created rocksdb database at {}", rocks.string()
    );
    return {std::move(db), leveldb::Status::OK()};
}

}  // namespace

// Intercepts the leveldb::DB::Open that creates a world database, so the
// database is served by rocksdb (or straight by the original function while the
// mod is disabled). The signature pins the supported binary: when it does not
// match, the hook is not installed and the original function is used as is.
LL_AUTO_STATIC_HOOK(
    DBOpenHook,
    ll::memory::HookPriority::Highest,
    "40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 E0 48 81 EC 20 01 00 00"_sig,
    leveldb::Status,
    leveldb::Options const& options,
    std::string const& name,
    leveldb::DB** dbptr
) {
    if (dbptr != nullptr) *dbptr = nullptr;
    if (!shimsConfig().enabled.load(std::memory_order::relaxed)) {
        return origin(options, name, dbptr);
    }
    auto result = openWithPolicy(
        options,
        name,
        [](leveldb::Options const& o, std::string const& n, leveldb::DB** d) {
            return DBOpenHook::origin(o, n, d);
        }
    );
    if (dbptr != nullptr) *dbptr = result.first.release();
    return result.second;
}

}  // namespace rocksdb_shim
