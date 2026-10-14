#include "mod/RocksDbStorage.h"

#include "rocksdb_shim/Config.h"

#include "ll/api/Config.h"
#include "ll/api/mod/RegisterHelper.h"

namespace rocksdb_storage {

// Reflected by ll::config, which needs a type with external linkage: hence
// namespace scope instead of a function-local type.
struct ConfigData {
    int  version{1};
    bool enabled{true};
    bool migrateExisting{true};
};

RocksDbStorage& RocksDbStorage::getInstance() {
    static RocksDbStorage instance;
    return instance;
}

bool RocksDbStorage::load() {
    namespace fs = std::filesystem;
    ConfigData   config;
    auto const configPath = getSelf().getConfigDir() / "config.json";
    std::error_code ec;
    fs::create_directories(configPath.parent_path(), ec);
    ll::config::loadConfig(config, configPath);

    auto& runtime = rocksdb_shim::shimsConfig();
    runtime.enabled.store(config.enabled, std::memory_order::relaxed);
    runtime.migrateExisting.store(config.migrateExisting, std::memory_order::relaxed);

    return true;
}

bool RocksDbStorage::enable() {
    getSelf().getLogger().debug("Enabling...");
    return true;
}

bool RocksDbStorage::disable() {
    getSelf().getLogger().debug("Disabling...");
    return true;
}

}  // namespace rocksdb_storage

LL_REGISTER_MOD(rocksdb_storage::RocksDbStorage, rocksdb_storage::RocksDbStorage::getInstance());
