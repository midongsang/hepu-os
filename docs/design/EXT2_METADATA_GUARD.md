# ext2 元数据区保护与对抗验证（P1-1）

日期：2026-11-23
状态：核心防线通过；目录一致性修复通过；`/data/clock/alarms.txt` 种子残留未清

## 1. 要解决的问题

原分配器只守住组 0 块位图的 `bit0`，其余元数据只靠“位图位不会被踩”的假设。
一旦位图被外部工具或掉电写坏，`ext2_alloc_block()` 会把超级块、GDT、块位图、
inode 位图或 inode 表当成空闲数据块分出去，随后整块清零，损坏面会从单个文件
扩大到整个文件系统。

## 2. 防线

`kernel/fs/ext2_alloc.c` 增加 `ext2_is_metadata_block(block)`，在
`ext2_alloc_block()` 选中候选块之后、置位/清零之前判断，命中则 `continue`：

- block 0 与 `s_first_data_block` 之前的引导区；
- 每组组首的超级块（含 backup）与紧随其后的 GDT（含 backup），
  `gdt_blocks = ceil(groups*32/block_size)`；
- 当前组的 `bg_block_bitmap`、`bg_inode_bitmap`；
- 当前组 `bg_inode_table` 起 `ceil(s_inodes_per_group * inode_size / block_size)` 个块。

组 0 原有的 `bitmap[0]==0` 整页清零防线保留，作为第二道保险。

## 3. 对抗测试

`tools/ext2_meta_guard.sh`：

1. `rm -f` 旧镜像后新建干净 64 MB ext2 盘，避免历史文件残留；
2. 解析 GDT，用 `debugfs clrb` 故意清零组 0/1 共 32 个元数据块位
   （保留 block 1 的 bit0，避免触发旧防线）；
3. 启动一次 VFS 写阶段，分配器眼里的“空闲块”已包含 GDT、块位图、
   inode 位图、inode 表；
4. `e2fsck -fn` 检查结构，并确认根 inode 仍是目录。

实测输出：

```text
[VFS-TEST] RESULT: PASS
e2fsck: 只报故意制造的位图差异
META-GUARD: PASS
```

日志：`/tmp/meta_guard.log`；镜像：`/tmp/meta_guard_disk.img`、
`/tmp/meta_guard_part.img`。

## 4. 同轮修掉的目录一致性问题

对抗测试把 `e2fsck` 的结构检查跑了出来，顺带修了四类真实缺陷：

- 目录首块分配后没记 `i_blocks`：`ext2_path_mkdir_v2` 补
  `i_blocks = block_size/512`；目录扩块在 `ext2_dir.c` 补 `+= block_size/512`。
- 新建目录后父目录链接数没加：`mkdir_v2` 成功后 `parent.i_links_count++`；
  删除目录时父目录 `i_links_count--`（最低 1）。
- `bg_used_dirs_count` 没维护：mkdir `++`，删除目录 `--`。
- `ext2_path_rename` 目标已存在时只释放目标 inode/块、不删目标目录项：
  改为先 `ext2_path_unlink_v2(new_path)`，重读 `new_dir` 后再添加新项。

修复后 `i_blocks`、目录 ref count、`Directories count` 三类警告消失。

## 5. 仍未解决

`e2fsck` 仍报：

```text
Entry 'alarms.txt' in /data/clock (31) has deleted/unused inode 32.
Entry 'alarms.txt' ... incorrect filetype (was 1, should be 0)
Inode bitmap differences: -32
Block bitmap differences: -1233
```

`/data/clock/alarms.txt` 由 `kernel/arch/x86/permission_manager.c` 的幂等 seed
通过 `ide_ext2_writefile()` 创建。inode 32 的目录项存在，但磁盘上的 inode
record 全 0（mode 0、i_blocks 0），说明 inode 表块 239 的那次写入没有落盘。
临时排查已排除“数据块分配覆盖 inode 表”：分配器在 232..260 范围没有分配记录。
根因仍在 `ide_ext2_writefile` / inode 表脏页落盘路径，未证实。

本轮还加了“目录项指向零 inode 时先 unlink 再重建”的修复，但该测试盘结果未变化，
说明问题发生在写入丢页而非“已存在文件被误判”，不能据此宣称已修复。

## 6. 边界

- 元数据保护只覆盖当前组位图和 inode 表；`s_reserved_gdt_blocks` 尚未读取，
  预留 GDT 块目前只按组首 `1 + gdt_blocks` 保护。
- 64 位只验证构建，未做镜像级对抗测试。
- 掉电 5 次 `fsck` 用例尚未在本轮执行。
