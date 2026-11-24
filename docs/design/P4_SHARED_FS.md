# P4：32/64 共享文件系统基础层与 64 位 S4 验收

日期：2026-11-24
方案：A（抽取 32/64 共享的 VFS/ext2 基础层，不做两套实现的长期维护）

## 1. 范围

P4 不物理合并两套 VFS 树，也不把 32 位 `vfs_node_t` 改成 64 位的
`vfs_backend` 模型。先把两边必须一致的契约、路径算法、ext2 目录遍历和
错误码抽到 `kernel/shared/fs/`，再让 32/64 各自复用；同时补齐 64 位
镜像级回归和 `getdents` 非根目录。

这样做的边界很清楚：

- 共享的是**语义和底层算法**，不是锁、缓存、挂载表这些架构相关实现。
- 32 位保留层级 `vfs_node_t` + 静态 fd 表；64 位保留挂载点表 + 最长前缀 +
  `vfs_backend` ops。
- 任何一边要改目录项解析、路径拆分、挂载点匹配或 POSIX 错误码，都只改
  共享层一处。

## 2. 共享层内容

### 2.1 POSIX 错误码 `fs_errno.h`

统一 `FS_EPERM/ENOENT/EIO/EBADF/ENOMEM/EACCES/EBUSY/EEXIST/ENOTDIR/
EISDIR/EINVAL/EMFILE/ENOSPC/ENOTEMPTY`。`kernel/include/vfs.h` 的
`VFS_*` 不再是独立数值，而是这些共享宏的别名，避免 32 位 VFS 与 64 位
syscall 层各维护一份错误码表。

### 2.2 路径算法 `fs_path.h/.c`

- `fs_path_split()`：拆最后一级父目录与名字，带长度上限，超长截断不越界。
- `fs_path_prefix_match()`：挂载点最长前缀匹配，要求边界落在 `/` 或路径结尾。
  根挂载点 `/` 是特例：只要路径以 `/` 开头就算匹配，否则 `/s4dir` 这类
  子路径不会被路由到根挂载的 ext2 后端。
- `fs_dirent64_reclen()`：Linux dirent64 记录长度 `(19 + namelen + 7) & ~7`。

32 位 `kernel/fs/vfs.c` 的 `split_path()` 与 64 位
`kernel/arch/x86_64/vfs64.c` 的 `prefix_match()` 已改为调用共享实现。

### 2.3 ext2 目录遍历 `ext2_dir_algo.h/.c`

`ext2_dir_walk()` 统一做：

- 按 `i_size` 计算目录块数；直接块号为 0 的未分配块跳过，间接块交给
  各架构的读块回调解析；
- `rec_len` 的 4 字节对齐、最小值、越界检查；
- `name_len` 上界检查；
- 跳过 inode=0 的空洞项与 `.`/`..`；
- 通过回调把目录项交给各架构，读块回调和条目回调各自持有独立上下文。

32 位 `ext2_readdir_ino()` 与 64 位 `ext2_64_list_dir_entries_ex()` 都改为
调用它。32 位 `kernel/include/ext2.h` 同时改为包含
`kernel/shared/fs/ext2_shared.h`，删除本地重复的超级块、组描述符、inode、
目录项和文件类型常量。

## 3. 64 位 `getdents` 支持非根目录

旧实现固定读根目录，`fd` 参数被丢掉。P4 改为：

1. `fs_fd_get_path(fd)` 取该 fd 的路径快照；
2. `vfs64_lookup(path)` 确认对象是 `VFS_DIR`，否则 `ENOTDIR`；
3. `vfs64_iterate(path, ...)` 枚举该目录；
4. 按 dirent64 布局打包，最多 64 条、最多 8192 字节；
5. 用户态由 `fs_do_getdents()` 统一 `copy_to_user`，内核态由
   `fs_do_getdents_kernel()` 直接读内核缓冲。

`sys_getdents()` 只保留 syscall 层的用户指针窗口校验和 `count < 19`
边界，枚举语义全部下沉到 `fs_dir_sys.c`，避免 syscall 文件里再维护一套
ext2 直读回退。

## 4. S4 验收

S4 的定义是“同一套文件操作用例在 32/64 位都通过”。P4 的落地方式：

- 64 位内核自检 `fs_s4_selftest()`：在 ext2 挂载后创建 `/s4dir`、
  写入并读回 `/s4dir/p4-getdents.txt`、用目录 fd 枚举并断言能看到该文件；
- 64 位用户态探针 `/bin/s4getdents.elf`：真实走
  `SYS_LNX64_GETDENTS`，从 `/s4dir` 的 fd 枚举并断言目录项存在；
- `tools/regress64.sh`：构建 `S4_SELFTEST=1` 内核、生成单镜像、QEMU 启动、
  串口断言上述标记，并拒绝异常/ panic / 断言失败。

串口必须出现：

```text
[S4] mkdir PASS
[S4] write-read PASS
[S4] getdents-nonroot PASS
[S4U] getdents-nonroot PASS
[S4] fs selftest PASS
[S4] result=0x1
```

32 位由既有 `tools/gate.sh` 的 VFS 写/读两阶段覆盖：`P3.mkdir`、
`shell.mkdir`、`shell.mv`、`disk.p3_dir` 与其余文件操作全部通过。

## 5. 实测

### 5.1 32 位门禁

`bash tools/gate.sh`：

```text
[1/5] 构建 32 位  0 error
[2/5] 构建 64 位  0 error
[3/5] VFS 跨启动（verify_p1p3.sh） PASS
[4/5] kunit 连续 3 次 ALL PASS
[5/5] GATE: PASS
```

### 5.2 64 位 S4 镜像回归

`bash tools/regress64.sh`：

```text
REGRESS64: PASS
```

关键串口行见第 4 节。

## 6. 过程中修掉的真实缺陷

1. **根挂载点前缀匹配**：共享 `fs_path_prefix_match()` 最初对 `/` 只做普通
   字符串前缀比较，`/s4dir` 匹配不到 `/`，64 位 `find_mount()` 无法把非根
   路径路由到 ext2，`vfs64_mkdir` 直接失败，`getdents` 也只能根目录。根
   挂载点改为“路径以 `/` 开头即匹配”。
2. **目录遍历回调上下文混用**：`ext2_dir_walk()` 初版只有一个 `ctx`，32 位
   读块回调需要 inode、条目回调需要 readdir 上下文，两者被当成同一个指针，
   导致根目录枚举读错 `i_size`，`P3.mkdir`/`shell.mkdir`/`shell.mv` 与
   `disk.p3_dir` 全部误判。共享接口拆成 `read_ctx` 与 `entry_ctx` 后 32 位
   门禁恢复 PASS。

## 7. 边界与后续

- 共享层当前是“契约 + 路径 + 目录遍历 + ext2 结构”，没有合并 32/64 的
  块缓存、锁、挂载表和 fd 表；这些仍按架构各自维护。
- 64 位 `fs_do_getdents` 没有 ext2 直读兜底：VFS 未挂载或后端不支持枚举时
  返回错误，尚未覆盖 `/dev` 等伪设备目录。
- `tools/make_single_img64.sh` 的 loop 挂载分支与 debugfs fallback 分支都已
  写入 `/bin/s4getdents.elf`，但 loop 分支对其它用户态程序的复制仍不完整。
- S4 只覆盖“创建目录、写读文件、枚举非根目录”这一组最小用例，不等于
  32/64 所有文件操作语义完全一致。

## 8. P4B：32 位读路径接入共享 ext2 核心（2026-11-24）

P4 的共享层只覆盖契约、路径、目录遍历；32 位 ext2 仍保留自己的 inode
解析、间接块映射和目录查找。P4B 第一步把 32 位只读路径真正切到
`kernel/shared/fs/ext2_algo.c`：

- `ext2_init` 建立静态 `ext2_context_t`，读块/写块回调指向 32 位
  `ext2_read_block`/`ext2_write_block`，配 4KB 共享暂存块。
- `ext2_read_inode`、`ext2_read_data`、`ext2_lookup` 改为调用
  `ext2_algo_read_inode`、`ext2_algo_read_data_block`、
  `ext2_algo_find_in_dir`；32 位不再维护第二套 inode 解析、间接块映射和
  目录线性查找。
- 共享核心去掉 64 位除法/取模（32 位无 libgcc 的 `__udivmoddi4`），
  块号换算改为 `10 + s_log_block_size` 移位，块内偏移改为
  `block_size - 1` 掩码；语义不变，64 位同样适用。

实测 `tools/gate.sh` → `GATE: PASS`：32/64 构建 0 error，32 位 VFS 写
18/18、读 9/9，kunit 3 次 ALL PASS；`tools/regress64.sh` →
`REGRESS64: PASS`（`[S4] result=0x1`、`[S4U] getdents-nonroot PASS`）。

写路径没有并入：实验把 `ext2_write_data` 和分配/释放也接到共享核心后，
VFS 仍 18/18 + 9/9 PASS，但 `e2fsck` 报空闲块计数差 645、块位图与 inode
位图差异。基线 P4 同一测试盘本身有 multiply-claimed block 和目录损坏，
不能单独作为回归判据；但 645 的差额说明 32 位 RAM GDT 与共享上下文仍是
两本账。写路径合并的前提是先统一分配账本，不能直接提交。

## 9. 结论：哪些能合，哪些不该硬合

| 层 | 结论 |
| --- | --- |
| ext2 盘上结构、目录遍历、路径 | 已合（P4） |
| ext2 inode/数据块只读 | 已合（P4B 第一步） |
| ext2 写/分配 | 待合，先统一 GDT 与空闲计数账本 |
| VFS namespace（32 树 / 64 挂载表） | 不物理合并：调用点遍布桌面/Shell/Settings，风险高、收益低 |
| 块缓存、锁、fd 表 | 保持各架构实现：与硬件和调度模型绑定 |
