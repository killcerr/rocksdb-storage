#pragma once

#include <atomic>

namespace rocksdb_shim {

// Switches read by the leveldb::DB::Open detour, filled from config.json when
// the mod loads; changed values apply after the next restart.
struct ShimsConfig {
    std::atomic<bool> enabled{true};
    std::atomic<bool> migrateExisting{true};
};

ShimsConfig& shimsConfig();

}  // namespace rocksdb_shim
