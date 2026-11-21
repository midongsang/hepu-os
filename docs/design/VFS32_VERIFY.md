# 32 位 VFS 与 ext2 后端：验证链与四个真 bug

日期：2026-11-20
提交：`9b55b06`（目录操作接 ext2 后端）、`c82e1f6`（本轮修复 + 验证链）

## 一、验证方式

`tools/verify_p1p3.sh`，一条命令跑完，不需要键盘交互：

```
同一张 ext2 盘 → 第一次 QEMU 进程（标记 'w'）跑写阶段 → 进程退出
                → 改标记为 'r' → 第二次 QEMU 进程跑读阶段
```

判据：两个阶段都 `[VFS-TEST] RESULT: PASS`。

**为什么不用 `system_reset`**：无头 QEMU 下 reset 之后第二次启动不产日志（这轮四个路子全失败）。两次独立进程不但更稳，证据链也更干净——第一个进程根本不知道第二个存在。

结果：

| 阶段 | 项 | 结果 |
|---|---|---|
| 写 | root / readdir（走 VFS） | PASS |
| 写 | `vfs_open(O_CREAT)` 建出并打开 | PASS |
| 写 | write 9 字节 + `vfs_sync` | PASS |
| 写 | 同进程立刻读回 | PASS |
| 写 | `vfs_mkdir` | PASS |
| 读 | 重启后 `p2test.txt` 还在 | PASS |
| 读 | 内容仍为 `HEPU_P2_OK` | PASS |
| 读 | `p3test` 目录还在 | PASS |
| 读 | 删除后确认消失 | PASS |

`regress32`：`fault=0 lines=895` PASS。

## 二、修掉的四个真 bug

**1. `vfs_open` 完全不认 `VFS_O_CREAT`**

只 `vfs_find`，不存在就返回 -1。但 P2 里已经先调 `ext2_path_create` 把文件建在盘上了，于是出现「盘上有、VFS 里找不到」——shell 的 `write` 永远开不了新文件。现在补齐：先落 ext2，再在内存树补节点并记 inode 号。

**2. 新建节点没挂 `ops`**

`vfs_write` / `vfs_read` 都要求 `node->ops` 非空。`vfs_create_node` 不设 ops，漏了就是「打开了但写不进去」（返回 -1 而非报错）。`ext2_file_ops` 去 `static` 以便复用。

**3. 返回码判断写反（最危险的一个）**

`ext2_path_mkdir` / `ext2_path_create` **成功返回 inode 号（> 0）**，`ext2_path_unlink_v2` **成功返回 0**，失败统一返回 -1。原先判 `!= 0` 会把成功当失败：

```
盘上目录已经建好了 → 判断成失败 → return -1 → 内存节点不建
```

结果比原来的「只改内存树」更糟：**盘上有、VFS 里没有**，反向假状态。统一改成 `< 0` 判失败。

**4. `ext2_readdir_ino` 没过滤 `inode == 0`**

`unlink` 只把目录项 `inode` 清 0。readdir 没查这一位，于是已经删掉的文件还会被 `ls` 列出来。补 `e->inode != 0`。

## 三、懒加载：补上「重启后按路径访问」

原来的缺口是：`vfs_find("/p2test.txt")` 重启后必然 NULL，只能操作当次会话新建的东西。查下去发现缺口比想象的深，两层：

**第一层：`ext2_mount()` 是死代码。** 整个内核没有任何调用点。它的实现里还有两个致命 bug：

```c
vfs_create_node(path, ...)   // 用完整路径当 name，应该是 entry->name
...                          // 建完之后从没 vfs_add_child，节点建一个泄漏一个
```

也就是说它就算被调也是坏的：节点全泄漏、按名字查不到。

**第二层：`vfs_root->inode_num` 是 0。** `vfs_root` 是第一个 `vfs_create_node` 建的，`next_inode` 从 0 起，所以它的 inode_num 恰好是 0。而 ext2 的根目录是 inode 2。拿 0 去读盘只会读到无关 inode。

修法：

1. `vfs_find` 找不到名字时，调 `vfs_lazy_lookup(parent, name)`：读父目录 inode → `ext2_lookup` 查名字 → 读子 inode 拿 mode/size → `vfs_create_node` → `vfs_add_child` 挂树。只在 `ext2_mounted`、父节点是目录时才动手，其它情况保持原语义。
2. 根节点豁免 `inode_num == 0` 判据，强制对到 `EXT2_ROOT_INODE`。

验证直接体现在脚本里：读阶段新增 `tree.p2` 判定项，`vfs_find("/p2test.txt")` 返回 `found`、`vfs_lazy_rc=0`（0=成功，1=前置条件，2=读父 inode，3=非目录，4=lookup，5=读子 inode，6=建节点）。

`ext2_mount` 的两个 bug 也一并修了（用 `entry->name`、挂树、跳过 `inode==0` 的已删除项），但它目前仍无调用点——修好是为了将来接「启动时全量建树」这条路时不用再查一遍。

## 四、诊断陷阱（下次别再踩）

**返回码语义不统一。** 同族函数里 create/mkdir 返回 inode 号、unlink 返回 0。写之前必须逐个确认，否则「成功」和「失败」会整体倒置。

**inode 1 不是根目录。** 标准 ext2 里 inode 1 是「坏块 inode」（mode 恒为 0），**根目录是 inode 2**。用 `ext2_readdir_ino(1, ...)` 会因为 `mode & 0xF000 != 0x4000` 直接返回 -1。我第一次就写成 1，白跑两轮。

**`debugfs -R "stat <1>"` 在同一环境里对干净盘也报 `bad type`。** 它不可信——判断盘是否被写坏，必须拿干净盘做对照，或看 `inode_bitmap` 这类硬证据，不能只看这条命令的输出。

**验证脚本自己会丢数据。** 第一版脚本改标记时，用旧的 `$PART` 分区文件覆盖回 `$DISK`，把第一次 QEMU 刚写进去的数据全抹了——于是无论内核是否落盘，读阶段都显示「盘上没有」。改标记前必须先从盘上把分区取回来：

```bash
dd if="$DISK" of="$PART" bs=512 skip=2048 count=115200 conv=notrunc
# 改标记
dd if="$PART" of="$DISK" bs=512 seek=2048 conv=notrunc
```

**跑过的盘上会多出 `firstboot.flag` / `hepu_boot.log` 等条目**——桌面自己会写文件。核对目录内容时别以为是自己建的。

## 五、shell 命令层（P3 真正落地）

之前只验到 VFS 层，`cmd_fs` 自己的参数解析没跑过。这次把自测扩到 10 项，直接在内核里调 `cmd_fs`（绕开键盘），判定一律看盘上结果：

| 项 | 结果 |
|---|---|
| `shell.write`：`write /p2shell.txt HELLO_SHELL` → 盘上内容正确 | PASS |
| `shell.mkdir`：`mkdir /p3shell` → 盘上建出目录 | PASS |
| `shell.rm`：`rm /p3shell` → 盘上消失 | PASS |
| `shell.ls`：`ls /` → 跑完 VFS 树仍完整 | PASS |

**过程中发现 `rm` 一直是坏的**：旧写法把整段 `/p3shell` 当名字传给 `fs_rm_recursive("/", name)`，拼出来是 `//p3shell`，永远找不到——所以 `rm` 看起来跑了、实际什么都没删。

**同时发现 P3 其实没做**：`mkdir` 分支还在直调 `ide_ext2_mkdir`。这次才真正改成 `vfs_mkdir` / `vfs_rmdir` / `vfs_unlink`，`write` 分支里那段 `ext2_path_create` 兜底也去掉了（`vfs_open` 已认 `O_CREAT`）。

现在 `fs` 的 `ls` / `cat` / `stat` / `write` / `mkdir` / `rm` 六条全部走 VFS，一个直调都不剩。剩 `df` / `fsck` / `mount` / `sync` / `cp` / `cpall` / `mv` 还是直调（`cp`/`mv` 是 P4，没开始）。

## 六、仍然没验的

- **键盘那一跳**：自测直接调 `cmd_fs`，没有经过 `shell_commands[]` 的 `strcmp` 分发和串口/键盘输入。命令实现体是对的，但"敲 `fs ls` 回车真的能到这"没验。
- **`cp` / `cpall` / `mv`**：P4，未开始。
- **多级路径**：自测只覆盖根目录一层（`/p2shell.txt`、`/p3shell`），`/a/b/c` 没验。
