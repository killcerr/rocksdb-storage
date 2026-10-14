# rocksdb-storage

A [LeviLamina](https://lamina.levimc.org/) mod that moves a Bedrock Dedicated Server world off
LevelDB and onto [RocksDB](https://rocksdb.org/). The existing world data is left untouched.

English | [中文](README.zh-CN.md)

---

## What it is

BDS stores its world with an old LevelDB engine. With this mod installed, the server reads and writes
world data through RocksDB instead — an actively developed key-value store with more tuning options.
Worlds that already exist are migrated automatically, and the original directory is kept as is: nothing
is deleted or renamed.

## Install

1. The server needs LeviLamina 26.51.7 or newer and BDS 1.26.51.1 (back up first if you run anything else).
2. From the server root, install it with [lip](https://lip.levimc.org/):

   ```bash
   lip install github.com/killcerr/rocksdb-storage
   ```

   Add `@version` to pin one, e.g. `lip install github.com/killcerr/rocksdb-storage@0.0.0`.
3. Restart the server.

This line in the log means it is in effect:

```
[rocksdb-storage] rocksdb-storage: opened rocksdb database at worlds/Bedrock level\rocksdb
```

## What happens on the first start

If the world directory still holds a LevelDB database (the `db` folder), the mod copies it into a
sibling `rocksdb` folder on the first start and reports progress in the log:

```
rocksdb-storage: migrating worlds/Bedrock level/db into worlds/Bedrock level\rocksdb
rocksdb-storage: migrated worlds/Bedrock level/db into worlds/Bedrock level\rocksdb (keys=..., 3.2s), leveldb database kept at worlds/Bedrock level/db
```

Do not kill the server while this runs; should it be interrupted, the next start simply migrates again,
because the original `db` is never modified.

Once migrated:

```
worlds/Bedrock level/
├── db/        the old database, untouched
├── rocksdb/   what the server actually uses
└── level.dat
```

## Configuration

`plugins/rocksdb-storage/config/config.json` (a restart applies changes):

```json
{
    "version": 1,
    "enabled": true,
    "migrateExisting": true
}
```

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | When `false`, worlds are opened with the original LevelDB again. |
| `migrateExisting` | `true` | When `false`, an existing world is opened as it is instead of being migrated. |

## Things to know

- **There is no migration back.** After the first start, `db` is a snapshot of the moment the world was
  migrated. Removing the mod, or deleting the `rocksdb` folder, makes the server fall back to that
  snapshot — the world looks reverted, so keep `rocksdb` to keep your progress.
- **Compression is off** for now, so `rocksdb` takes up somewhat more space than `db`.
- Servers that enable the non-default database file encryption are not supported.
- Only BDS 1.26.51.1 with LeviLamina 26.51.7 has been verified. On other versions the mod stays out of
  the way and does not touch your data.
- Server-side only; there is nothing to install on clients.
- Development stage, version `0.0.0`.

## Build from source

Requires xmake and clang-cl:

```bash
xmake f -y -p windows -a x64 -m release
xmake
```

The result is in `bin/rocksdb-storage/`; copy that folder into the mods directory. The first build
downloads and compiles the dependencies, including rocksdb, which takes a few minutes.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE). Third-party code:

- `src/leveldb/`, `src/db/`, `src/util/` are taken from
  [mojang/leveldb](https://github.com/mojang/leveldb) (`origin/main`), BSD-3-Clause © The LevelDB
  Authors; its `LICENSE` and `AUTHORS` ship next to the vendored sources, and every file keeps its
  original notice;
- RocksDB is used under Apache-2.0 and statically linked into the build output (xmake package); the
  license texts live in [`licenses/rocksdb/`](licenses/rocksdb) and are included in the release zip.
