# rocksdb-storage

把 Bedrock 专用服务器的世界存档从 LevelDB 换成 [RocksDB](https://rocksdb.org/) 的
[LeviLamina](https://lamina.levimc.org/) 模组。原存档不会被改动。

[English](README.md) | 中文

---

## 这是什么

BDS 一直用一个老式的 LevelDB 来存世界存档。装上这个模组后，服务器读写存档都会经过
RocksDB——一个更活跃、可调优的键值存储引擎。装之前就已经存在的存档会被自动迁移，
原目录原样保留，不会删除或改名。

## 安装

1. 服务器需要 LeviLamina 26.51.7 或更高版本，BDS 1.26.51.1（其他版本请先备份再用）。
2. 在服务器根目录用 [lip](https://lip.levimc.org/) 安装：

   ```bash
   lip install github.com/killcerr/rocksdb-storage
   ```

   需要指定版本时加 `@版本号`，例如 `lip install github.com/killcerr/rocksdb-storage@0.0.0`。
3. 重启服务器。

启动日志里出现下面这行就说明生效了：

```
[rocksdb-storage] rocksdb-storage: opened rocksdb database at worlds/Bedrock level\rocksdb
```

## 第一次启动会发生什么

如果世界目录里已经有一份旧的 LevelDB 存档（`db` 文件夹），模组会在第一次启动时把它
复制到同级的 `rocksdb` 文件夹，日志里能看到进度：

```
rocksdb-storage: migrating worlds/Bedrock level/db into worlds/Bedrock level\rocksdb
rocksdb-storage: migrated worlds/Bedrock level/db into worlds/Bedrock level\rocksdb (keys=..., 3.2s), leveldb database kept at worlds/Bedrock level/db
```

迁移期间不要强杀服务器；万一中断，下次启动会重新来一次，因为原始 `db` 从头到尾没有被改动。

迁移完成后：

```
worlds/Bedrock level/
├── db/        旧存档，保持原样
├── rocksdb/   服务器实际使用的存档
└── level.dat
```

## 配置

`plugins/rocksdb-storage/config/config.json`（改完需要重启）：

```json
{
    "version": 1,
    "enabled": true,
    "migrateExisting": true
}
```

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `enabled` | `true` | 关掉后世界会用原来的 LevelDB 打开。 |
| `migrateExisting` | `true` | 设为 `false` 时不迁移旧存档，直接按原样打开它。 |

## 注意事项

- **没有反向迁移。** 迁移之后 `db` 里就是迁移那一刻的旧快照。移除模组或删掉 `rocksdb` 文件夹，
  服务器只会看到那份旧快照（世界看起来回退了），所以想保留新进度就别动 `rocksdb`。
- 目前**未启用压缩**，`rocksdb` 占用会比 `db` 大一些。
- 如果服务器开了非默认的数据库文件加密，本模组不适用。
- 只在 BDS 1.26.51.1 + LeviLamina 26.51.7 上验证过；换成别的版本，模组不会生效，也不会破坏数据。
- 客户端不支持（这是服务器端模组）。
- 目前是开发阶段，版本 `0.0.0`。

## 从源码构建

需要 xmake 和 clang-cl：

```bash
xmake f -y -p windows -a x64 -m release
xmake
```

产物在 `bin/rocksdb-storage/`，把它整个拷到 mods 目录即可。第一次构建会下载并编译依赖
（包括 rocksdb），需要几分钟。

## 许可

GPL-3.0-or-later，见 [LICENSE](LICENSE)。第三方代码：

- `src/leveldb/`、`src/db/`、`src/util/` 取自 [mojang/leveldb](https://github.com/mojang/leveldb)
  （`origin/main`），BSD-3-Clause © The LevelDB Authors，各文件头保留原声明；
- RocksDB 被静态链接进产物（xmake 包），Apache-2.0 / GPL-2.0 双许可——分发二进制时需一并附带其许可
  文本。
