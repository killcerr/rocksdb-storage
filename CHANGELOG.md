# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.1] - 2026-10-14

### Added

- RocksDB-backed world storage: `leveldb::DB::Open` is hooked (`LL_AUTO_STATIC_HOOK` on the 23-byte
  prologue, `_sig`) and MC receives an `McRocksDb` that implements the real `leveldb::DB` vtable, backed
  by RocksDB (`rocksdb v11.1.2`, no compression, 4 MB write buffer, 4 KB blocks, 80 MB LRU block cache,
  bloom filter 10).
- One-time migration of an existing LevelDB world into a sibling `rocksdb` directory, batched in 4096-key
  `WriteBatch`es, flushed and WAL-synced; the LevelDB `db` directory is left untouched. Failed migrations
  abort non-destructively and report `IOError`.
- `config.json` (`enabled`, `migrateExisting`) loaded through `ll::config`.
- Vendored `origin/main` sources of Mojang's LevelDB fork (`src/leveldb`, `src/db`, `src/util`) so the
  interception boundary uses the real `leveldb::` classes instead of a mirrored ABI.

### Fixed

- `McRocksDb::Get` now accepts a null `value`, which MC uses as an existence probe (Mojang's LevelDB only
  copies the payload when a string is given: `MemTable::Get`, `Version::Get`). RocksDB always copies, so
  the value is read into a `PinnableSlice` and dropped. Previously this aborted the server at world load.
