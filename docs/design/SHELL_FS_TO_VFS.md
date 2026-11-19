# shell 文件命令改走 VFS 的补丁方案（2026-11-19 拟定）

> 只出方案，不动代码。前置条件已具备：VFS 接口齐、`vfs_sync()` 已补
> （`d325403`）、写入不落盘的链已核实清楚。

## 一、现状

`kernel/sched/shell_cmd_fs.c`（473 行），`cmd_fs` 一个函数处理全部子命令：

```
fs mount | ls | cat | write | stat | df | sync | mkdir | rm | cp <src> <dst>
        | cpall <dst> | mv | fsck
```

全部直调 `ide_ext2_*`，两个递归辅助也是直调：

- `fs_cp_recursive(dirpath, name, dstbase, depth)` — 第 80 行
- `fs_rm_recursive(dirpath, name)` — 第 173 行

## 二、目标形态（逐命令对照）

| 命令 | 现状 | 改成 |
|---|---|---|
| `ls` | `ide_ext2_list_entries(dirpath, fm_entry_t[])` | `vfs_readdir(path, vfs_dirent_t[])` |
| `cat` | `ide_ext2_read_file(dir, name, buf, max)` | `vfs_open` + `vfs_read` + `vfs_close` |
| `write` | `ide_ext2_write_raw(...)` | `vfs_open(O_WRONLY\|O_CREAT)` + `vfs_write` + `vfs_close` + **`vfs_sync()`** |
| `stat` | 直读 inode | `vfs_find(path)` 取 `size` / `type` |
| `mkdir` | `ide_ext2_mkdir` | `vfs_mkdir(path)` |
| `rm` | `ide_ext2_delete` / `fs_rm_recursive` | `vfs_unlink` / `vfs_rmdir`（递归版重写） |
| `cp` / `cpall` | `fs_cp_recursive` | read + write + mkdir 组合 |
| `sync` | `ide_ext2_sync()` | `vfs_sync()` |
| `mv` | 直调 | `vfs_rename(old, new)`（`vfs.c:445` 已有，之前判断有误已更正） |
| `df` / `fsck` | ext2 专有 | **保留直调**（不是文件语义，是卷级操作） |

## 三、必须先补的两个 helper

1. **路径拼接** `join_path(dir, name) -> "/a/b/c"`
   现在各处用 `while (*sd) srcdir[sn++] = ...` 手工拼，重复至少 6 处。
   不收敛这个，改走 VFS 会把同样的 bug 复制一遍。

2. **目录项转换** `fm_entry_t ↔ vfs_dirent_t`
   字段基本同构（`name` / `is_dir`→`type` / `size`），转换本身简单，
   但要注意 `vfs_dirent_t.name` 是定长 `VFS_MAX_NAME`，`fm_entry_t` 未必。

## 四、分阶段（每阶段独立可回滚）

**P1 — 只改 `ls` + `stat`（纯读，零写入风险）**

验证：`fs ls /`、`fs ls /bin` 输出与直调版逐行对比一致。

**P2 — 改 `cat` + `write`（读写 + 落盘）**

验证：`fs write /t.txt hello` → `fs cat /t.txt` → **重启** → 再 `fs cat /t.txt`。
最后一步是唯一能证明"真落盘"的。

**P3 — 改 `mkdir` + `rm`（含递归）**

验证：建空目录 → 删；建非空目录 → `rm` 应失败（返回码要对齐 ext2 的 `-2`）。

**P4 — 改 `cp` / `cpall` / `mv`，删掉 `fs_cp_recursive` 的直调版**

**P5 — 收尾**：`df` / `fsck` 保留直调，但加注释说明为什么保留。

## 五、开工门槛与红线

1. **P1 没跑通就不要往下**——路径拼接和目录项格式最容易出隐性 bug，
   而且错了不一定崩，只是结果不对。
2. `write` 之后必须 `vfs_sync()`，否则"返回成功但重启就没了"。
3. 递归删除前先确认 `vfs_rmdir` 对非空目录的返回码（ext2 是 `-2`），
   shell 的错误提示依赖这个区分。
4. `mv` 走 `vfs_rename(old, new)` —— 接口在 `vfs.c:445` 已存在（`hepu_fs_rename` 也是它），
   可直接改；改前确认它对"目标已存在"的语义与 ext2 直调一致。
5. 每一阶段都要跑 `regress32`，`fault=0` 才算过。

## 六、预期收益

- `ls /bin`、`cat` 这类真正走 VFS，后面 32 位应用（文件管理器、输入法、相机）
  才有统一入口可迁
- 直调点从 151 处里减掉 shell 这一大块（`shell_cmd_fs.c` 是单文件最集中的地方）
- 权限检查（`compat_sandbox_path_allowed`）开始对 shell 生效
