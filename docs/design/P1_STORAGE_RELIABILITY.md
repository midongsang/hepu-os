# P1 存储可靠性收口记录（2026-11-23）

状态：P1 三项验收证据齐备，`GATE: PASS`，`POWERLOSS-5X: PASS`，`MOUNT-FULL: PASS`。

## 1. 掉电一致性：5 次强杀

脚本 `tools/powerloss_5x.sh` 串起 `tools/powerloss_test.sh`：

- 每次重新造 58 MB ext2 分区；
- `cache=unsafe` 启动，`/powerloss_test` 触发 8 MB 顺序写负载；
- 第一个 `[POWERLOSS] chunk` 后 2 秒 `kill -9` QEMU；
- 宿主机 `e2fsck -f -n` 和 `e2fsck -f -y` 判定；
- 检查已 `sync` 的 `pl.dat` 是否在修复后的盘上。

结果：

| 次数 | 写入 chunk | e2fsck -n | e2fsck -y | pl.dat |
|---|---:|---:|---:|---|
| 1 | 2112 | 0 | 0 | 存在，2.7 MB |
| 2 | 2112 | 4 | 1（已修复） | 存在，2.1 MB |
| 3 | 2624 | 4 | 1（已修复） | 存在，2.6 MB |
| 4 | 2752 | 4 | 1（已修复） | 存在，2.6 MB |
| 5 | 2752 | 4 | 1（已修复） | 存在，2.0 MB |

5/5 满足 P1 验收：`fsck` 全部可修，0 处修不回，已 `sync` 的 `pl.dat` 全部保留。第 2–5 次只剩块位图/空闲块计数差异，属于掉电瞬间未落盘的分配记账，可由 `fsck -y` 收敛。

## 2. `fs mount -f` 端到端

`fs_vfs_selftest.c` 的 phase `f` 现在不再只直调 `ext2_mount`，而是调用真实的 shell 分支：

```c
cmd_fs("mount -f");
cmd_fs("mount -f");
```

判定根节点子项数：第一次只增不减，第二次必须稳定。`tools/mount_full_test.sh` 在干净盘上注入 `/mountfull_test='f'` 后启动，实测：

```text
[MOUNTFULL] rc1=0 rc2=0 children=1/8/8 RESULT: PASS
MOUNT-FULL: PASS
```

## 3. cpall 多层嵌套

写阶段新增 `shell.cpall_nested`（第 17 项）：

- 建 `/cpall_nest/aa/bb`；
- 写入 `/cpall_nest/aa/bb/deep.txt = "NESTED_OK"`；
- `cpall /cpall_nest_dst` 整根复制；
- 读回 `/cpall_nest_dst/cpall_nest/aa/bb/deep.txt`。

读阶段新增 `disk.cpall_nested`（第 8 项），同一张盘重启后按路径读回该文件。实测：

```text
[VFS-TEST] shell.cpall_nested 17/17 PASS  三层目录与文件内容均保留
[VFS-TEST] disk.cpall_nested 8/8 PASS  三层目录与文件跨启动保留
```

## 4. 门禁

`bash tools/gate.sh`：

```text
32/64 位构建 0 error
VFS 跨启动 PASS（写 17/17，读 8/8）
kunit 连续 3 次 ALL PASS
GATE: PASS
```

## 5. 保留的已知问题

`e2fsck` 仍可能报 `/data/clock/alarms.txt` 的 inode 32 记账问题；它不在本轮 5 次掉电样本中复现，但根因尚未定位，记为 P1 之后的已知债，不阻塞“可日常使用 v0.1”的掉电验收。
