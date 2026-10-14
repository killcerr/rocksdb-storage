#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "leveldb/db.h"
#include "rocksdb/db.h"

namespace rocksdb_shim {

// Opens a rocksdb::DB at dbPath as an UTF-8 path. Returns nullptr on failure,
// with the rocksdb status in *err when err is not null.
std::unique_ptr<rocksdb::DB> openRocksDb(std::filesystem::path const& dbPath, std::string* err = nullptr);

// Options used for every database this mod opens.
rocksdb::Options rocksOptions();

// Wraps a live rocksdb::DB into a leveldb::DB.
std::unique_ptr<leveldb::DB> wrapRocksDb(std::unique_ptr<rocksdb::DB> db);

// Opens and wraps in one step.
std::unique_ptr<leveldb::DB> makeRocksDb(std::filesystem::path const& dbPath, std::string* err = nullptr);

// Adapts a rocksdb::DB to the leveldb::DB interface.
class McRocksDb : public leveldb::DB {
public:
    explicit McRocksDb(std::unique_ptr<rocksdb::DB> db);

    leveldb::Status Put(leveldb::WriteOptions const&, leveldb::Slice const&, leveldb::Slice const&) override;
    leveldb::Status Delete(leveldb::WriteOptions const&, leveldb::Slice const&) override;
    leveldb::Status Write(leveldb::WriteOptions const&, leveldb::WriteBatch*) override;
    leveldb::Status Get(leveldb::ReadOptions const&, leveldb::Slice const&, std::string*) override;
    leveldb::Iterator*       NewIterator(leveldb::ReadOptions const&) override;
    leveldb::Snapshot const* GetSnapshot() override;
    void                     ReleaseSnapshot(leveldb::Snapshot const*) override;
    bool                     GetProperty(leveldb::Slice const&, std::string*) override;
    void GetApproximateSizes(leveldb::Range const*, int, uint64_t*) override;
    void CompactRange(leveldb::Slice const*, leveldb::Slice const*) override;
    void SuspendCompaction() override;
    void ResumeCompaction() override;

private:
    std::unique_ptr<rocksdb::DB> mDb;
};

// Converts a rocksdb::Status into the equivalent leveldb::Status.
leveldb::Status toLevelDbStatus(rocksdb::Status const& status);

}  // namespace rocksdb_shim
