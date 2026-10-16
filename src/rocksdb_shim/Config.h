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

// The compressors this build links; the enumerator names are the values
// config.json takes, and ll::config rejects anything else when it loads.
enum class Compression : unsigned char {
    none,
    lz4,
    lz4hc,
    zstd,
};

// RocksDB tuning, filled from the same config.json at load and read on every
// database open, so a change takes effect on the next restart. Values that
// rocksdb cannot use are folded onto these defaults in rocksOptions(), which
// logs what it ends up with.
struct DbConfig {
    // Used for every level but the bottommost one. zstd is the default because
    // it measured smaller than the LevelDB store this replaced at no CPU cost,
    // see the README; lz4 trading those bytes for speed did not pay off here.
    Compression compression{Compression::zstd};
    // Used for the bottommost level, where files stay longest; set it to
    // compression to compress everything the same way, which is the default.
    Compression bottommostCompression{Compression::zstd};
    // Passed to the compressor as it interprets it; 0 is its own default.
    int compressionLevel{0};
    // Size of a data block before compression, in bytes.
    int blockSize{4096};
    // Bits per key in the bloom filter; 0 leaves it out.
    int bloomBits{10};
    // Size of the block cache in MiB; 0 turns caching off.
    int blockCacheMiB{80};
    // Size of the memtable in MiB, and so of each flushed file's L0 size.
    int writeBufferMiB{4};
    // Open file descriptors; -1 means no limit.
    int maxOpenFiles{1000};
};

// The same instance rocksOptions() reads, written once while the mod loads.
DbConfig& dbConfig();

}  // namespace rocksdb_shim
