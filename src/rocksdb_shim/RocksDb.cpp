#include "rocksdb_shim/RocksDb.h"

#include <string>
#include <string_view>
#include <vector>

#include "ll/api/mod/NativeMod.h"
#include "rocksdb/cache.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"

namespace rocksdb_shim {

static rocksdb::Slice toRocksSlice(leveldb::Slice const& s) { return rocksdb::Slice(s.data(), s.size()); }

static rocksdb::ReadOptions toRocksReadOptions(leveldb::ReadOptions const& o) {
    rocksdb::ReadOptions r;
    r.verify_checksums = o.verify_checksums;
    r.fill_cache       = o.fill_cache;
    r.snapshot         = reinterpret_cast<rocksdb::Snapshot const*>(o.snapshot);
    return r;
}

leveldb::Status toLevelDbStatus(rocksdb::Status const& s) {
    if (s.ok()) return leveldb::Status::OK();
    std::string const msg = s.ToString();
    using Code             = rocksdb::Status::Code;
    switch (s.code()) {
    case Code::kNotFound:
        return leveldb::Status::NotFound(msg);
    case Code::kCorruption:
        return leveldb::Status::Corruption(msg);
    case Code::kNotSupported:
        return leveldb::Status::NotSupported(msg);
    case Code::kInvalidArgument:
        return leveldb::Status::InvalidArgument(msg);
    default:
        return leveldb::Status::IOError(msg);
    }
}

rocksdb::Options rocksOptions() {
    rocksdb::Options o;
    o.create_if_missing = true;
    o.error_if_exists   = false;
    // Blocks are stored as they are: the rocksdb build links no compressor.
    o.compression       = rocksdb::kNoCompression;
    o.write_buffer_size = 4 << 20;
    o.max_open_files    = 1000;

    rocksdb::BlockBasedTableOptions table;
    table.block_size      = 4096;
    table.block_cache     = rocksdb::NewLRUCache(static_cast<size_t>(80) << 20);
    table.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10));
    o.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table));
    return o;
}

std::unique_ptr<rocksdb::DB> openRocksDb(std::filesystem::path const& dbPath, std::string* err) {
    std::unique_ptr<rocksdb::DB> db;
    // The name is handed over as UTF-8; rocksdb's Windows env converts it to a
    // wide path itself.
    rocksdb::Status s = rocksdb::DB::Open(rocksOptions(), dbPath.string(), &db);
    if (!s.ok()) {
        if (err) *err = s.ToString();
        return nullptr;
    }
    return db;
}

std::unique_ptr<leveldb::DB> wrapRocksDb(std::unique_ptr<rocksdb::DB> db) {
    return std::unique_ptr<leveldb::DB>(new McRocksDb(std::move(db)));
}

std::unique_ptr<leveldb::DB> makeRocksDb(std::filesystem::path const& dbPath, std::string* err) {
    auto db = openRocksDb(dbPath, err);
    if (!db) return nullptr;
    return wrapRocksDb(std::move(db));
}

// ---------------------------------------------------------------------------

namespace {

// Replays the records of a leveldb WriteBatch into a rocksdb WriteBatch; the
// record format is parsed by the vendored leveldb write_batch.cc.
class BatchInserter : public leveldb::WriteBatch::Handler {
public:
    explicit BatchInserter(rocksdb::WriteBatch& batch) : mBatch(batch) {}

    void Put(leveldb::Slice const& key, leveldb::Slice const& value) override {
        mBatch.Put(toRocksSlice(key), toRocksSlice(value));
    }
    void Delete(leveldb::Slice const& key) override { mBatch.Delete(toRocksSlice(key)); }

private:
    rocksdb::WriteBatch& mBatch;
};

class McIterator : public leveldb::Iterator {
public:
    explicit McIterator(std::unique_ptr<rocksdb::Iterator> it) : mIt(std::move(it)) {}

    bool Valid() const override { return mIt->Valid(); }
    void SeekToFirst() override { mIt->SeekToFirst(); }
    void SeekToLast() override { mIt->SeekToLast(); }
    void Seek(leveldb::Slice const& target) override { mIt->Seek(toRocksSlice(target)); }
    void Next() override { mIt->Next(); }
    void Prev() override { mIt->Prev(); }
    leveldb::Slice key() const override {
        auto const& k = mIt->key();
        mKey.assign(k.data(), k.size());
        return leveldb::Slice(mKey);
    }
    leveldb::Slice value() const override {
        auto const& v = mIt->value();
        mValue.assign(v.data(), v.size());
        return leveldb::Slice(mValue);
    }
    leveldb::Status status() const override { return toLevelDbStatus(mIt->status()); }

private:
    std::unique_ptr<rocksdb::Iterator> mIt;
    mutable std::string                mKey;
    mutable std::string                mValue;
};

}  // namespace

McRocksDb::McRocksDb(std::unique_ptr<rocksdb::DB> db) : mDb(std::move(db)) {}

leveldb::Status McRocksDb::Put(leveldb::WriteOptions const& o, leveldb::Slice const& key, leveldb::Slice const& value) {
    rocksdb::WriteOptions wo;
    wo.sync = o.sync;
    return toLevelDbStatus(mDb->Put(wo, toRocksSlice(key), toRocksSlice(value)));
}

leveldb::Status McRocksDb::Delete(leveldb::WriteOptions const& o, leveldb::Slice const& key) {
    rocksdb::WriteOptions wo;
    wo.sync = o.sync;
    return toLevelDbStatus(mDb->Delete(wo, toRocksSlice(key)));
}

leveldb::Status McRocksDb::Write(leveldb::WriteOptions const& o, leveldb::WriteBatch* updates) {
    if (updates == nullptr) return leveldb::Status::InvalidArgument("null WriteBatch");
    rocksdb::WriteBatch batch;
    BatchInserter       inserter(batch);
    leveldb::Status     parse = updates->Iterate(&inserter);
    if (!parse.ok()) return parse;
    rocksdb::WriteOptions wo;
    wo.sync = o.sync;
    return toLevelDbStatus(mDb->Write(wo, &batch));
}

leveldb::Status McRocksDb::Get(leveldb::ReadOptions const& o, leveldb::Slice const& key, std::string* value) {
    rocksdb::ReadOptions const ro = toRocksReadOptions(o);
    rocksdb::Slice const       rk = toRocksSlice(key);

    rocksdb::Status s;
    if (value != nullptr) {
        s = mDb->Get(ro, rk, value);
    } else {
        // A null value means "only report whether the key exists": the leveldb
        // interface copies the payload into a string only when one is given,
        // while rocksdb always copies. Read into a slice and drop it.
        rocksdb::PinnableSlice probe;
        s = mDb->Get(ro, mDb->DefaultColumnFamily(), rk, &probe);
    }
    if (s.IsNotFound()) return leveldb::Status::NotFound(leveldb::Slice(), leveldb::Slice());
    return toLevelDbStatus(s);
}

leveldb::Iterator* McRocksDb::NewIterator(leveldb::ReadOptions const& o) {
    return new McIterator(std::unique_ptr<rocksdb::Iterator>(mDb->NewIterator(toRocksReadOptions(o))));
}

leveldb::Snapshot const* McRocksDb::GetSnapshot() {
    return reinterpret_cast<leveldb::Snapshot const*>(mDb->GetSnapshot());
}

void McRocksDb::ReleaseSnapshot(leveldb::Snapshot const* snapshot) {
    if (snapshot == nullptr) return;
    mDb->ReleaseSnapshot(reinterpret_cast<rocksdb::Snapshot const*>(snapshot));
}

bool McRocksDb::GetProperty(leveldb::Slice const& property, std::string* value) {
    static constexpr char const* kMapped[] = {
        "leveldb.stats",
        "leveldb.sstables",
        "leveldb.approximate-memory-usage",
    };
    std::string name(property.data(), property.size());
    for (auto const& from : kMapped) {
        if (name == from) {
            // Same name with the other prefix, except for the memory usage
            // property which rocksdb calls estimate-table-readers-mem.
            std::string rocks = "rocksdb" + name.substr(std::string_view("leveldb").size());
            if (rocks == "rocksdb.approximate-memory-usage") rocks = "rocksdb.estimate-table-readers-mem";
            return mDb->GetProperty(rocksdb::Slice(rocks), value);
        }
    }
    if (name.rfind("leveldb.num-files-at-level", 0) == 0) {
        name.replace(0, 7, "rocksdb"); // the '.' separator stays
        return mDb->GetProperty(rocksdb::Slice(name), value);
    }
    ll::mod::NativeMod::current()->getLogger().debug("rocksdb-storage: unsupported property '{}'", name);
    return false;
}

void McRocksDb::GetApproximateSizes(leveldb::Range const* range, int n, uint64_t* sizes) {
    mDb->GetApproximateSizes(reinterpret_cast<rocksdb::Range const*>(range), n, sizes);
}

void McRocksDb::CompactRange(leveldb::Slice const* begin, leveldb::Slice const* end) {
    mDb->CompactRange(
        rocksdb::CompactRangeOptions(),
        reinterpret_cast<rocksdb::Slice const*>(begin),
        reinterpret_cast<rocksdb::Slice const*>(end)
    );
}

void McRocksDb::SuspendCompaction() { mDb->PauseBackgroundWork(); }

void McRocksDb::ResumeCompaction() { mDb->ContinueBackgroundWork(); }

}  // namespace rocksdb_shim
