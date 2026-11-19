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
