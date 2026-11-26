# P1 存储缺陷修复证据 — 32 位内核 ext2 落盘（孤儿 inode / 位图-表差异）

- 分支：`fix/p1-inode32-persist`（基于 main `58d772d`）
- 日期：2026-11-26
- 范围：仅 32 位本地 ext2 分配/释放路径；未改动 64 位共享核心与共享算法代码。
- 口径：修复前后使用**同一套造盘与启动命令序列**，唯一差异是本分支这一个 commit。

## 1. 现象（修复前）

在 64MB 整盘（MBR 0x55AA，分区类型 0x83，起始 LBA 2048，扇区数 116736）上
`mke2fs -q -t ext2 -b 1024 -F part.img 58368`，QEMU `qemu-system-i386` 直启
`build_v250/kernel.elf`，跑满 68s 后按 pidfile 退出。修复前串口 88737 行，
`[KTEST] RESULT: ALL PASS`，无 GPF/PANIC。

`e2fsck -fn` 原文见 `pre-fix/e2fsck-pre.txt`，要点：

- Pass 2：`Entry '..' in <32>/<34> (34) has an incorrect filetype (was 2, should be 1).`
- Pass 3：`Unconnected directory inode 34 (<32>/<34>)`；`'..' in ... (34) is <32> (32), should be <The NULL inode> (0).`
- Pass 4：`Inode 32 ref count is 1, should be 2.`；`Unattached zero-length inode 33.`；`Unattached inode 33`；`Inode 34 ref count is 2, should be 1.`
- Pass 5：`Block bitmap differences: +1235`；`Inode bitmap differences: +(33--34)`。

盘上实测：inode 33 为 `mode=0x81A4 size=0 links=1 b0=0` 的零长度文件，inode 34 为
`mode=0x41ED size=1024 links=2 b0=1235` 的目录；二者 inode 位图位均为空闲（0），
但表项残留非零模式/大小/块指针。seed 文件 alarms.txt(inode 31)、hepu_boot.log(inode 32)
元数据与数据块均已正确落盘。

## 2. 根因定位

### 2.1 结论

32 位本地释放路径 `ext2_free_inode`（`kernel/fs/ext2_alloc.c`）释放 inode 时：

1. 清掉 inode 位图对应位；
2. 递增组/超级块空闲 inode 计数；

**但没有把盘上 inode 表项清零**。对照共享核心 `ext2_algo_free_inode`
（`kernel/shared/fs/ext2_algo.c` 约 664–679 行），其在清位图之后会
`memset(it_buf + it_off, 0, sizeof(ext2_inode_t))` 并写回表块，两段口径不一致。

于是被释放 inode 的旧 `i_mode / i_size / i_links_count / i_block` 残留在 inode 表中。
e2fsck 以位图作为“是否已分配”的权威依据：位图已空闲、表项却非零，即被判为
unattached / unused inode；残留的目录块指针进一步表现为块位图差异，以及目录
filetype、引用计数不一致。

### 2.2 排查中已排除的方向

为区分“释放路径未清表项”与“落盘写回丢失/旧副本覆盖”，在 `kernel/fs/ide_ext2.c`
临时加入串口追踪（实验代码已在最终提交中移除），覆盖两级缓存：4 槽 L1 + 页缓存 L2。

- 所有进入脏集合的块（D1）最终都被 L1（W1）或 L2（W2）写回；未出现脏块从未落盘。
- 写回时刻对比同块 L1 副本与 L2 页缓存副本：无内容分叉（无 DIV）；L1 已逐出的
  块经 L2 写回（W2E）路径也未复现旧副本覆盖新副本。
- 位图/inode 表块在 4 槽 L1 间频繁逐出，但每次逐出前先把脏 L1 写回，缓存口径自洽。

据此排除“两级缓存把旧副本写回、覆盖新内容”作为本缺陷主因；缺陷落在释放路径
表项未清零，而不是分配/写入阶段的落盘丢失。

### 2.3 涉及的关键路径

- 分配：`ext2_alloc_inode`（置位图位）→ `ext2_write_inode`（写表项）。
- 释放：`ext2_path_unlink_v2` → `ext2_free_file_blocks` → `ext2_free_inode`。
- 共享参照：`ext2_algo_free_inode`（清位图 + 清表项 + 目录计数回退）。

## 3. 修复内容

文件：`kernel/fs/ext2_alloc.c`，函数 `ext2_free_inode`。

在清位图、更新计数之后，按与 `ext2_write_inode` 相同的表块/偏移口径定位该 inode
所在表块，把表项清零并将表块标脏：

- 组号 `g = (inode_num - 1) / s_inodes_per_group`，组内序号 `i = (inode_num - 1) % ...`；
- 表块 `b_idx = bg_inode_table + (i * ext2_inode_size) / block_size`；
- 表内偏移 `b_off = (i * ext2_inode_size) % block_size`；
- 清零字节数取 `min(ext2_inode_size, sizeof(ext2_inode_t))`，与写入路径口径一致。

未删除任何安全/一致性断言，未降低 sync 语义，未跳过任何写入或把错误当成功。

## 4. 修复后验证

### 4.1 同盘两次启动

| 阶段 | 串口行数 | KTEST | e2fsck |
| --- | --- | --- | --- |
| 启动 1（写阶段，新盘） | 83945 | ALL PASS | 五遍检查无差异、无 WARNING |
| 启动 2（同盘读回） | 86042 | ALL PASS | 五遍检查无差异、无 WARNING |

启动 2 读回核对：

- alarms.txt(inode 31)：`mode=0x81A4 size=24 b0=1232`，数据块内容
  `07:00|0\n08:30|0\n00:00|0\n`。
- hepu_boot.log(inode 32)：`mode=0x81A4 size=589 b0=1233`，头部
  `=== Hepu OS boot log (v0.62.1) ===\n[BOOT] early sc...`。

### 4.2 回归

- `bash tools/regress/smoke32.sh 70`（无盘启动）：
  `serial=1 initrd=1 net=1 desk=1 fault=0`，`ktest_result=PASS`，
  `fail_suites=0 fail_cases=0 skip_cases=2`。
  p3-security 两项（`s3_normal_user_denied`、`posix_error_table`）在无 ext2 后端时
  保持原有 SKIP 语义，未把 SKIP 记成 PASS，也未回退跳过机制。
- `python3 tools/check_docs.py`：`warns=0 errors=0`，当前版本 0.62.1。
- 本次未改动 64 位共享代码，无需重跑 x86_64 构建。

## 5. 证据清单

### 5.1 仓库内（本目录）

- `pre-fix/e2fsck-pre.txt`：修复前 `e2fsck -fn` 原文。
- `pre-fix/debugfs-root-pre.txt`：修复前根目录/`/data` 列表。
- `post-fix/e2fsck-post.txt`：修复后 `e2fsck -fn` 原文（五遍无差异）。
- `post-fix/debugfs-root-post.txt`：修复后根目录/`/data` 列表。
- `post-fix/serial-post1-excerpt.txt`：启动 1 串口关键行（KTEST / seeded / SKIP）。
- `post-fix/serial-post2-excerpt.txt`：启动 2 串口关键行。

### 5.2 仓库外完整镜像与全量串口

证据根：`/home/user/Doubao/chats/38452875115179010/evidence-p1-inode32-2026-11-26/`

- `pre-fix/serial-pre.log`（88737 行）、`part-after-pre.img`、`e2fsck-pre.txt`、`debugfs-root-pre.txt`。
- `post-fix/serial-post1.log`（83945 行）、`serial-post2.log`（86042 行）、
  `part-after1.img`、`part-after2.img`、`e2fsck-post.txt`、`debugfs-root-post.txt`。

运行时磁盘/启动器：`/home/user/Doubao/chats/38452875115179010/.toolchain/run/p1-inode32/`
（私有 runner 持有 `/tmp/hepu-qemu.lock`，按 pidfile 退出，无全局 pkill；
镜像 `file.locking=off`，未开 snapshot，写入直落镜像。）

## 6. 口径与残留说明

- `/data` 目录块中保留一条 `inode=0` 的 `p3sec` 空目录项：这是删除后留下的可复用空洞，
  由后续 `add_dir_entry` 复用，e2fsck 不报为错误。
- 盘上 inode 15/23/27 为零长度但仍被目录项链接的文件，修复前后 e2fsck 均未对其报错，
  不属于本次 P1 落盘缺陷范围，未在本次改动中处理。
- 任务书原文提到的“inode 32 全 0”变体，在本次新盘上复现为孤儿 inode 33/34 与
  位图-表差异；验收口径以“e2fsck 无 deleted/unused/filetype/位图差异 + seed 文件可读、
  二次启动读回一致”为准。
