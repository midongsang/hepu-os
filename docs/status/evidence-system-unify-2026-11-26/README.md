# 系统层级统一 · 集成与本轮基线证据（2026-11-26）

分支：本地 `main`
集成提交：`9c942dc merge(system): 统一事件日志与服务管理入口第一阶段（feat/eventlog-system-2026-11-26）`
父提交：`22cf5ad`（本轮 P1/P3/OPEN-02/SB-25 收尾）+ `a89cb06`（事件日志第一阶段）

## 1. 合并方式

`git merge --no-ff feat/eventlog-system-2026-11-26`。两支自 `58d772d` 分叉，期间 main 前进 13 个提交。
改动文件交集只有 `kernel/arch/x86_64/Makefile`，且分处不同 hunk（main 在 265 行附近加 `MSC_SWEEP_TEST`，
事件日志分支在 162/200 行加 `event_log.c`/`services.c` 与 `vpath`），由 git 自动合并，**无手工冲突**。

## 2. 门禁（`bash tools/gate.sh`，合并后 main 重跑）

原始输出见 `gate-full.log`；构建日志 `build32.log`/`build64.log`、VFS 日志 `verify.log`。

```text
[1/6] 构建 32 位                              ✓ 0 error
[2/6] 构建 64 位                              ✓ 0 error
[3/6] VFS 跨启动（verify_p1p3.sh）             ✓ 写阶段 + 读阶段 PASS
[4/6] 带盘冒烟                                ✓ p3-security 带盘真实 2/2/0
      p3-security: passed=2 failed=0 skipped=0
[5/6] kunit 连续 3 次启动（挂盘）               ✓ 第 1/2/3 次 ALL PASS（p3_skip=0）
      每次：[KUNIT] result: 0x00000002 passed, 0x00000000 failed, 0x00000000 skipped
[6/6] 结论
GATE: PASS
```

## 3. 32 位无盘冒烟（SKIP 语义未回退）

`bash tools/regress/smoke32.sh --no-disk 70`，摘要见 `32-nodisk-smoke-summary.txt`：

```text
ext2_mounted=0 ext2_nodisk=1
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=2 p3_skip=2
p3-security: passed=0 failed=0 skipped=2
[KUNIT-SKIP] p3-security/s3_normal_user_denied (ext2 backend unavailable)
[KUNIT-SKIP] p3-security/posix_error_table (ext2 backend unavailable)
```

要点：无盘时 p3-security 明确 SKIP 2 项、不冒充 PASS；有盘（门禁第 4 步）才要求真实 2/2/0。
两种模式的判据都没有被放宽。

## 4. 64 位 UEFI 运行（snapshot 关闭，真实落盘）

镜像：先 `make -C kernel/arch/x86_64 -j2`，再 `HEPU_IMG=<私有路径> python3 tools/build_uefi_dual.py`
（`64-build-image.log`；镜像放私有路径——放 `/tmp/hepu_uefi_dual.img` 会被并行的造盘脚本清掉，
首次尝试因此拿到空盘，串口只有 PXE 失败，已作废重跑）。
QEMU：OVMF pflash + `-drive format=raw,file=<img>,file.locking=off`（**无 snapshot**），`-serial file:`。

串口（`64-serial.log`）：

```text
=== HepuOS 64-bit Kernel booting ===
[SERVICES] boot services started: 5/5
[EXT2_64] ext2 found on master drive
[EVENT] early save to /hepu_events.log
[EVENT] saved to /hepu_events.log
```

盘上读回（`dd` 抽 ext2 分区 LBA264192 起，`debugfs -R 'cat /hepu_events.log'`），文件 `64-hepu_events.log`，17 行：

```text
00000001 ... INFO  boot64    pid=0 code=1  64-bit kernel entry
00000002 ... INFO  service   pid=0 code=10 registered network
...
00000011 ... INFO  service   pid=0 code=11 started gui
00000012 ... INFO  kernel64  pid=0 code=0  [BOOT] driver framework initialized
00000014 ... INFO  kernel64  pid=0 code=0  [BOOT] ext2 root mounted
```

说明：64 位启动事件与服务注册/启动事件共用同一序号空间，且是**从盘上读回**的，不是仅串口打印。

## 5. 文档检查

`python3 tools/check_docs.py`（`check-docs.txt`）：`warns=0 errors=0`，当前版本 0.62.1。

## 6. 边界（本轮基线不等于系统管理完整）

以下项在合并后仍**未完成**，由四条并行工作流分头推进，未取得运行证据前不写「已实现」：

- `dmesg` 仍读 legacy klog/klog64，未切 `event_log`；
- `/hepu_events.log` 每次覆盖写，无追加、无轮转、无用户态 logd、无远程 syslog；
- `services.c` 内置 network/audio/bluetooth/wifi/gui 的 start/stop 仍是打印空壳，无真实监督/健康检查/退避；
- `microg_service.c`、`distributed_os.c`、`hsservices.c` 仍是平行服务集合；
- `mount/umount`、`chmod/chown`、`useradd/userdel`、`fsck/mkfs` 仍缺；
- 32 位无串口输入，CLI 交互行为未手测（本轮 32 位行为判据为 KUnit + 带盘冒烟）；
- 事件环 256 条 / 消息 144 字节，异常上下文仍走普通自旋锁路径。
