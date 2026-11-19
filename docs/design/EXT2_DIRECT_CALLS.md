# 32 位应用层对 ext2 的直调清单（2026-11-19）

> `kernel/fs/` 之外仍直接调用 `ide_ext2_*` 的地方。这些调用绕开了 VFS 的
> fd 表、目录入口和缓存一致性，是"两条路径并存"的现状。本文只做盘点与
> 排序，不含改动。

## 一、总量

**151 处**（排除 `kernel/fs/` 自身与 `extern` 声明），分布在 20 个文件。

## 二、按模块

| 模块 | 文件 | 直调内容 | 建议 |
|---|---|---|---|
| shell 文件命令 | `sched/shell_cmd_fs.c` | `read_file` / `write_raw` / `list_entries` / `mkdir` | **最该先改** |
| 移动端文件管理器 | `sched/mobile_files.c` | 同上 | 该改 |
| 移动端应用 | `sched/mobile_{contacts,sms,phone,camera,calc,browser}.c` | 读写各自数据文件 | 该改 |
| 内置应用 | `apps/ime.c`、`camera_app.c` | 读写输入法词频、照片元数据 | 该改 |
| 字体加载 | `gfx/gfx_font_manager.c` | 读字体文件 | 该改 |
| 权限管理 | `arch/x86/permission_manager[_ext].c` | `mkdir` / `writefile` / `read_file`（初始化数据目录） | 初始化部分可保留 |
| 桌面依赖 | `arch/x86/hpdesk32_deps.c` | `read_file`（文件管理器读） | 该改 |
| 内核日志/安全 | `arch/x86/klog.c`、`security/mac.c` | 待细查 | 待定 |
| 启动自测 | `arch/x86/init_sequence.c` | `selftest` / 标记文件读取 | **合理保留** |

## 三、VFS 侧能力对照

32 位 VFS **接口并不缺**：

```
vfs_open / vfs_read / vfs_write / vfs_close
vfs_mkdir / vfs_rmdir / vfs_unlink
vfs_readdir / vfs_readdir_fd     （已接 ext2 真实目录，4f42795 / f5aa34d）
```

32 位 sys 层也齐：`sys_open / sys_unlink / sys_mkdir / sys_rmdir / sys_readdir`。

**所以"改走 VFS"不是缺能力，是没接线。**

## 四、建议顺序

1. **`shell_cmd_fs.c`（100% 直调）**——改完之后 `ls /bin`、`mkdir` 这类才真正
   走 VFS，也是最直接的可见收益，且验证方式明确（shell 里敲命令）
2. `mobile_files.c`（文件管理器，用户最常摸）
3. `gfx_font_manager.c`（字体加载，开机路径）
4. `mobile_{contacts,sms,phone}.c`
5. `permission_manager` 的初始化——**可以保留直调**，但要在代码里注明
   "引导期建目录，VFS 尚未就绪"

## 五、改动前必须核对的差异

| 差异点 | 直调现状 | VFS 现状 |
|---|---|---|
| 路径语义 | `read_file(dirpath, name)` 两段式 | `vfs_open(fullpath)` 整路径 |
| 目录项结构 | `fm_entry_t` | `vfs_dirent_t`（name/type/size） |
| 写入同步 | `ide_ext2_sync()` 显式调用 | VFS 写路径是否同步**未核** |
| 错误码 | 返回 -1/-2 | 返回 -1（部分场景语义不同） |
| 权限检查 | 无 | `compat_sandbox_path_allowed`（已有） |

其中**写入同步**这一条最需要先确认：VFS 的写是否落到 `ide_ext2_sync`，
否则改过去会出现"写了但没落盘"。

## 六、不建议本轮动的

- `init_sequence.c` 的自测与标记文件读取：它就是要验底层，走 VFS 反而绕远了
- `klog.c`：崩溃路径，任何额外层都可能害事

## 七、写入同步核实结果（2026-11-19）

**结论：VFS 的写路径不落盘，改过去必须先接同步，否则"写了但没落盘"。**

链路：

```
vfs_write(fd, buf, size)                      kernel/fs/vfs.c
  └─ node->ops->write = ext2_vfs_write        kernel/fs/ext2_dir.c:115
       ├─ ext2_write_data(...)                → 写块缓存 + 标脏
       ├─ ext2_write_inode(...)               → 写 inode 缓存
       └─ 结束，没有任何 flush
```

对照两个 sync：

| 函数 | 做什么 | 是否真落盘 |
|---|---|---|
| `ext2_sync()` | `ext2_block_flush()` + 把 `ext2_disk`（仅 32KB 常驻区）写回 | **否**（只动内存镜像） |
| `ide_ext2_sync()` | `ide_write_range(sb_area, data_lba, 64)` + `ext2_block_flush()` | **是** |

所以现在的直调之所以"写得进去"，是因为它们都是
`ide_ext2_writefile/write_raw(...)` **末尾自己调了 `ide_ext2_sync()`**
（`apps/ime.c:88`、`permission_manager.c:81`、`mobile.c:394/451` 都能看到）。

**已补的接口**：`vfs_sync()`（`vfs.c`，2026-11-19），包装 `ide_ext2_sync()`。
设计上**不**在每次 `write` 后自动调（那会每写一次全盘同步）；由"写完必须落盘"
的调用方显式调用。

**改造时的硬规则**：

1. 任何经 `vfs_write` 的写操作，在业务结束点必须调一次 `vfs_sync()`
2. 不允许把 `ide_ext2_sync()` 直接暴露给应用层（保持单一入口）
3. 改完必须验"写 → 重启 → 读回"，不能只看"写返回成功"

## 八、P3 阻塞：32 位 VFS 的目录操作是"内存树"，不落盘（2026-11-20）

动 P3 之前先核对了实现，结论是先别动。

```
vfs_mkdir(path)   → vfs_create_node + vfs_add_child      （0 处 ext2 调用）
vfs_unlink(path)  → remove_child + kfree
vfs_rmdir(path)   → remove_parent 检查 + remove_child + kfree
```

三个函数**只改内存里的 VFS 树，一个字节都不写盘**。对照直调版：

- `ide_ext2_mkdir` → 真的在 ext2 里建目录项
- `ext2_path_unlink_v2` → `ext2_write_inode(...)`（`ext2_path.c:35/38/94/96`），真删

**如果照方案把 shell 的 `mkdir` / `rm` 换成 `vfs_mkdir` / `vfs_rmdir`，
结果是"当次会话看起来成功、重启全没了"——比不改更糟。**

### 正确顺序

1. **先给 32 位 VFS 补"目录操作 → ext2 后端"的分派**
   （参考 64 位 S6 的 `backend unlink/mkdir/rmdir` 做法：路径式方法挂在
   backend 上、失败回退直调、返回码透传）
2. 再改 shell 的 `mkdir` / `rm`

在补完分派之前，`mkdir` / `rm` **保持直调**，不做"看起来更统一"的假改造。

## 九、为什么 P1 / P2 是安全的

- **P1（`ls` / `stat`）是纯读**：`vfs_readdir` 已接 `ext2_readdir_ino`（真读盘），
  `vfs_find` 读的是挂载时建好的树 → 都是真数据
- **P2（`cat` / `write`）的写是真写**：`vfs_write` → `ext2_vfs_write` →
  `ext2_write_data`（真写缓存）+ `vfs_sync()` 落盘 → 落地

两者的共同点：**VFS 在"文件读写"这块确实接了 ext2 后端，只是"目录增删"没接。**
这个边界之前没人明确写过，是这次动 P3 才暴露出来的。
