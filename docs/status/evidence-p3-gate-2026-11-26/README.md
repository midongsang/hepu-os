# P3 安全用例默认带盘门禁化证据（2026-11-26）

## 结论

方案 A 落地：32 位 multiboot 冒烟默认挂一张确定性生成的 ext2 测试盘，
`p3-security` 套件的 `s3_normal_user_denied` 与 `posix_error_table` 两个用例
在默认门禁/冒烟入口真实执行，带盘 **2 passed, 0 failed, 0 skipped**；无盘模式
保留显式 SKIP 语义，SKIP 不计 PASS，未删除或放宽任何安全断言。

- 带盘默认 smoke：`p3-security: passed=2 failed=0 skipped=0`，`[KTEST] RESULT: ALL PASS`
- 无盘 smoke：`p3-security: passed=0 failed=0 skipped=2`，两条 `[KUNIT-SKIP]`
  原因明确打印（ext2 backend unavailable）
- `bash tools/gate.sh`：`GATE: PASS`
- `python3 tools/check_docs.py`：`warns=0 errors=0`

## 改动文件

| 文件 | 改动 |
| --- | --- |
| `tools/regress/make_smoke32_disk.sh` | 新增。确定性生成 32 位冒烟 ext2 测试盘；每次先删旧盘/旧分区文件，从干净盘开始。 |
| `tools/regress/smoke32.sh` | 默认带盘启动并把 p3-security 2/0/0 作为判据；`--no-disk` / `SMOKE32_NO_DISK=1` 走无盘 SKIP 路径；两种模式判据分开。 |
| `tools/verify_p1p3.sh` | 并行安全修正：全局 `pkill -9 -f qemu-system-i386` 收窄为按本脚本 pidfile 精确 kill；判定口径不变。 |
| `tools/gate.sh` | 整段外层包 `/tmp/hepu-qemu.lock` 串行化；新增带盘 smoke 独立判据（p3 2/2/0）；连续 3 次启动日志记录 p3 SKIP/PASS 状态；去除脚本内全局 pkill。 |

## 测试盘生成方式

脚本：`tools/regress/make_smoke32_disk.sh`（环境变量 `HEPU_SMOKE32_DISK` 可覆盖路径，
默认 `/tmp/hepu-smoke32-test.img`）。

- 64MB 整盘 raw 镜像；MBR `0x55AA`，单分区类型 `0x83`，起始 LBA 2048。
- ext2 分区：`mke2fs -q -t ext2 -b 1024 -F <part> 58368`（1024B 块，约 58MB），
  与 `tools/verify_p1p3.sh` 造盘口径一致。
- 每次运行先 `rm -f` 旧整盘与旧分区文件：`mke2fs -F` 只重写元数据、不清旧块内容，
  复用旧分区会残留目录项，让 mkdir/mv 误判“已存在”。
- 镜像放在 `/tmp`（overlay/ext4）。`/home/user` 下的网络文件系统在 QEMU 被 SIGKILL
  后会残留写锁，下次启动报 `Failed to get "write" lock`，因此不放证据目录内。
- 生成记录见 `disk-gen.log`。

## 证据

### 1. 带盘默认 smoke（独立入口一条命令）

命令：`bash tools/regress/smoke32.sh 70`

```text
[EXT2] selftest: mounted (ext2 fs on IDE disk)
[KUNIT] suite: p3-security (0x00000002 tests)
[KUNIT] result: 0x00000002 passed, 0x00000000 failed, 0x00000000 skipped
[KTEST] RESULT: ALL PASS
```

摘要：`smoke32-disk-summary.txt`；串口摘录：`smoke32-disk-excerpt.log`。

### 2. 无盘 smoke（SKIP 语义保留）

命令：`bash tools/regress/smoke32.sh --no-disk 70`

```text
[EXT2] selftest: no disk, skip
[KUNIT] suite: p3-security (0x00000002 tests)
[KUNIT-SKIP] p3-security/s3_normal_user_denied (ext2 backend unavailable)
[KUNIT-SKIP] p3-security/posix_error_table (ext2 backend unavailable)
[KUNIT] result: 0x00000000 passed, 0x00000000 failed, 0x00000002 skipped
[KTEST] RESULT: ALL PASS
```

摘要：`smoke32-nodisk-summary.txt`；串口摘录：`smoke32-nodisk-excerpt.log`。

无盘下两条用例为显式 **SKIP**，不是安全断言通过；带盘出现 p3-security SKIP
在带盘模式判据中直接记 FAIL（说明盘没挂上）。

### 3. 门禁 `tools/gate.sh`

命令：`bash tools/gate.sh`，最后一行 `GATE: PASS`。

- 32/64 位构建 0 error
- `verify_p1p3.sh` 写阶段 + 读阶段各 `[VFS-TEST] RESULT: PASS`
- 带盘 smoke：p3-security `passed=2 failed=0 skipped=0`
- 连续 3 次挂盘启动：每次 `[KTEST] RESULT: ALL PASS`，p3 套件均 `2 passed, 0 failed, 0 skipped`，`p3_skip=0`

摘录：`gate-excerpt.log`；全量 stdout：`gate-full.log`；门禁内带盘 smoke 摘要：
`gate-smoke32-summary.txt`。

### 4. 文档防腐检查

`python3 tools/check_docs.py` → `warns=0 errors=0`。

## 并行安全修正说明

- `tools/gate.sh` 开头自举：未持锁时 `exec flock /tmp/hepu-qemu.lock bash <self>`，
  整段门禁在锁内串行化；内层 `verify_p1p3.sh` / `smoke32.sh` 不再重复取锁
  （`HEPU_QEMU_LOCK_SKIP=1`），避免同文件二次 `flock` 自死锁。
- `tools/verify_p1p3.sh` 原先两处 `pkill -9 -f qemu-system-i386`（全局匹配，
  会误杀同机并行任务的 i386 实例）已改为只按自己的 pidfile 精确 kill；trap 清理
  同样按 pidfile。**不改变 VFS 写/读两阶段的判定口径**。
- `tools/regress/smoke32.sh` 只按自己 pidfile 回收实例；qemu 子进程关闭锁 fd，
  避免脚本异常终止后孤儿 qemu 继续持有 `/tmp/hepu-qemu.lock`。
- 本次改动未运行旧版 `verify_p1p3.sh` 的全局清理路径；所有实例起停均在本 worktree 内。

## 边界与已知限制

- `p3_cleanup` 只按 VFS 可见节点清理（`/data/p3sec` 及其子节点）；ext2 中残留但
  VFS 不可见的目录项不在本用例清理范围。
- 测试盘 58MB ext2、58368 块；用例的 ENOSPC 路径走故障注入，不依赖写满整盘。
- 串口只输出套件级 result 行；单用例 `... PASS` 文本走 VGA，不在串口摘录中，
  套件 `2 passed` 即为两个用例均真实通过的依据。
- 造盘依赖工具链里的 `mke2fs` / `debugfs`（`source tools/regress/env.sh` 后在 PATH）。
- 首轮带盘冒烟曾出现一次 `create /data/p3sec` 断言失败，复跑即通过；未稳定复现，
  若门禁偶发 FAIL，应先留存串口日志并复查盘是否干净挂载，不得通过删/改断言兜底。
