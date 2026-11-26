# 系统层级统一 · 四路并行合并复核（2026-11-26 第二轮）

基线：`9c942dc`（事件日志第一阶段并入 main）+ `f660fa4`（集成证据）
合入顺序：log → services → layout → sysadmin，全部 `--no-ff` 合并；台账整理 `99b2621`。

## 1. 合入内容与冲突处置

| 分支 | 内容 | 合并提交 |
|---|---|---|
| `feat/log-hardening-2026-11-26` | dmesg 切 `event_log`、`/hepu_events.log` 追加 + 8KB 一级轮转、emergency 无锁环、event-log KUnit 6 例 | `d37d4f0` |
| `feat/service-supervisor-2026-11-26` | 依赖拓扑启动、循环/缺依赖检出、健康状态（STUB/HEALTHY/FAILED）、tick 指数退避与上限、平行集合映射、system-manager KUnit 6 例 | `bc04197` |
| `feat/system-layout-2026-11-26` | `tools/audit_layout.py`、层级与命名空间设计文档、索引生成器补 design 分组 | `1c91f34` |
| `feat/sysadmin-cmds-2026-11-26` | chmod/chown/chgrp（数字模式落盘）、mount/umount 校验链、fsck、mkfs 显式拒绝、useradd/userdel 显式拒绝、32/64 命令对齐、admin KUnit 7 例 | `dacc64c` |

冲突只有两处文档：`docs/design/EVENT_LOG_AND_SERVICE_LAYER.md`（日志加固与服务监督各写一节，已合成 2.2/2.3 + 3.1~3.4 与统一第 6 节）与 `docs/status/OPEN_BUGS.md`——四路都登记了 `OPEN-05`，合并后按主题重排为
`OPEN-05` 日志外围（logd/日期轮转/远程 syslog）、`OPEN-06` 服务监督残余、`OPEN-07` 私有头与平行件归属、`OPEN-08` 系统管理命令延期边界；设计文档与证据 README 中的引用同步改号。

## 2. 32 位运行验证（合并后 main，全部 PASS）

`bash tools/regress/smoke32.sh --disk 70`（`ev-disk/tests/smoke32.txt`）：

```text
ext2_mounted=1  ktest_result=PASS  fail_suites=0  fail_cases=0  skip_cases=0  p3_skip=0
p3-security: passed=2 failed=0 skipped=0
56 个套件全部执行，关键三套：
  event-log                6 passed / 0 failed / 0 skipped
  system admin commands    7 passed / 0 failed / 0 skipped
  system-manager           6 passed / 0 failed / 0 skipped
[KTEST] RESULT: ALL PASS
```

`--no-disk`（`ev-nodisk/tests/smoke32.txt`）：`ktest_result=PASS`、`skip_cases=6`、`p3_skip=2`，6 条 SKIP 分别是
p3-security 2 条、event-log `append_keeps_history`/`rotation_creates_dot1`、admin `mount_dup_device`/`chmod_chown_persist`，
均为「ext2 后端不可用」的显式 SKIP，没有一条被算作 PASS。

构建：`make -j2` 与 `make -C kernel/arch/x86_64 -j2` 均 0 error；`python3 tools/check_docs.py` `warns=0 errors=0`。

## 3. 64 位回归（未关闭，合入后暴露）

四路合入后，64 位 UEFI（OVMF pflash，`snapshot=off`）启动**停在**：

```text
[IRQ64] PIT+keyboard+UART handlers registered
[IRQ64] Interrupts enabled (PIC mode)
```

之后无任何输出：`EXT2_64` 0 次、`[EVENT]` 0 次、无桌面（连续两次复现，`ev-64/ser64.log` 131 行）。
合入前（仅事件日志第一阶段）同一 harness 能走完 `[EXT2_64]` 与 `[EVENT] saved`，因此判定为本轮三支合入引入的回归。

隔离进展（服务分支上 A/B，见该分支 `docs/status/evidence-service-supervisor-2026-11-26/regression64-irq-hang-notes.md`）：

- A（音频后端初始化绑在 `audio` 服务 start，IRQ 前）：复现停机；
- B（`hs_audio_init()` 移回桌面阶段）：**仍停机** → 音频时序假设被证伪；
- 已排除：`hs_audio_init()` 函数体不碰 AC97/PCI 寄存器、`kernel_main_64.c` 相对基线无改动、64 位链接不含 `microg_service.o`/`distributed_os.o`。

**根因尚未定位，修复未提交、未合入。** 下一步建议按 `9a48877 → 9168251 → b3274bc` 二分服务分支，
并把事件日志（`klog64` 文件回调、`event_log_emit` 在 IRQ 前路径）与系统管理（64 位 shell/命令表）分别单独合入做对照；
判定口径固定为串口出现 `[EXT2_64]` 与 `[EVENT] saved`（能到桌面更好）。

## 4. 边界（不当作已完成）

- 64 位启动回归未修，64 位运行面目前**不可宣称可用**；
- `services_monitor_tick()` 无生产周期调度调用方，退避只在 KUnit 假时钟下验证；五项内置服务仍是 STUB 空壳；
- `mount/umount` 只支持第二 ext2 实例之外的最小链（第二实例明确降级）；`mkfs`、多用户/组模型为显式拒绝；64 位 `fsck --fix` 与 busy 判定未接；
- 用户态 logd、按日期轮转、远程 syslog 未做；emergency 环未并入 `journal` 查询；
- 布局梳理本轮未搬目录，`tools/audit_layout.py` 当前 errors=0、2 条 INFO（`acpi.c`/`smp.c` 平行件）；
- 32 位 CLI 无串口输入，命令交互未手测（行为判据为 KUnit + 带盘冒烟）；64 位命令运行时分派未交互实测。
