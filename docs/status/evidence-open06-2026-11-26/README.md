# OPEN-06 服务监督残余 —— 第三阶段证据（2026-11-26）

分支：`feat/open05-06-2026-11-26`
基线：`1b11d93`

本目录记录 OPEN-06 第三阶段的真实 QEMU 启动取证：监督节拍接入两侧真实主循环、
64 位 `service` 命令展示口径对齐、显式开关探针夹具的失败→退避→重试→成功链路。

## 1. 改动范围

### 源码

| 文件 | 改动 |
| --- | --- |
| `kernel/include/services.h` | 新增生产调度入口 `services_monitor_poll()` 声明 |
| `kernel/sched/services.c` | 探针开关判定与 `svc-probe` 夹具（前 3 次失败、第 4 次成功）；`services_monitor_poll()` 节流轮询（约 250ms 一次）；探针在 `services_start_boot()` 中按开关注册 |
| `kernel/sched/shell.c` | 32 位文本 shell 主循环接入 `services_monitor_poll()` |
| `kernel/arch/x86_64/desktop64.c` | 64 位桌面主循环接入 `services_monitor_poll()` |
| `kernel/arch/x86_64/shell64.c` | `shell_cmd_service()` 对齐 32 位口径：`list`/`status` 均输出 health 列与 STUB 备注，`list` 头部带 `services=/active=/healthy=` 计数；文本 shell 循环接入 `services_monitor_poll()` |
| `kernel/arch/x86_64/bootopts64.c`、`bootopts64.h` | 解析 `hepu.svcprobe=1`，新增 `bootopts_svcprobe()` 访问器 |
| `kernel/kernel_main_64.c` | 服务注册前提前解析一次 cmdline（原有常规解析保留，幂等重复解析） |
| `Makefile`、`kernel/arch/x86_64/Makefile` | `SVC_PROBE=1` 编译开关，定义 `HEPU_SVC_PROBE`；默认构建不编入探针 |

### 文档

- `docs/status/OPEN_BUGS.md`：OPEN-06 行更新（已验证项与残余项分列）。
- `docs/design/EVENT_LOG_AND_SERVICE_LAYER.md`：3.2 / 3.3 / 5 / 6 节口径更新。

### 纪律说明

- 退避到期的重试（含 `services_start()` 及其 `vga_printf` / `event_log_emit` / 可能的落盘）
  只在主循环上下文执行；PIT / 键盘 / 串口中断里只允许置标志，不在中断上下文直接调 `services_start()`。
- 探针夹具明确标注 `TEST PROBE (not a product service)`，不与内置五项 STUB 混在一起，
  不伪造真实 daemon。默认发布构建不注册该探针。
- KUnit `system-manager` 套件仍用 `services_set_clock_hook()` 假时钟注入；生产节拍接入
  未改变其断言。

## 2. 复现命令

工作树：`/home/user/Doubao/chats/38452875115179010/wt-open0506`

```bash
source tools/regress/env.sh

# 默认全量构建（探针关闭），产出回归镜像
bash tools/regress/build_all.sh

# 32 位无盘冒烟
bash tools/regress/smoke32.sh --no-disk 70

# 32 位带盘冒烟
bash tools/regress/smoke32.sh --disk 70

# 64 位默认 HPET 桌面启动
bash tools/regress/boot64.sh

# 文档 / BSS 校验
python3 tools/check_docs.py
python3 tools/check_stage2_bss.py
```

探针取证镜像（仅取证构建，不进发布路径）：

```bash
# 1. 编探针内核
make -C kernel/arch/x86_64 SVC_PROBE=1 -j2

# 2. 打包探针镜像（输出路径可用 HEPU_IMG 覆盖）
HEPU_IMG=<本目录>/build/probe.img python3 tools/build_uefi_dual.py

# 3. 注入账户/系统/显示配置（与 build_all.sh 同一套 debugfs 写入）后启动取证
bash docs/status/evidence-open06-2026-11-26/boot-probe.sh
```

## 3. 回归结果与关键判据

### 3.1 32 位无盘冒烟

日志：`serial-32-nodisk-raw.txt`（原始串口）、`serial-32-nodisk.txt`、`summary-32-nodisk.txt`

- `ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=6 p3_skip=2`
- `system-manager` 套件（来源：`serial-32-nodisk-raw.txt`）：

```
[KUNIT] suite: system-manager (0x00000006 tests)
[KUNIT] result: 0x00000006 passed, 0x00000000 failed, 0x00000000 skipped
```

- 无盘环境 ext2 后端不可用，6 条用例显式 SKIP（p3-security 两条、event-log 两条、
  admin 两条）；SKIP 不计 PASS，与既有口径一致。

### 3.2 32 位带盘冒烟

日志：`serial-32-disk-raw.txt`（原始串口）、`serial-32-disk.txt`、`summary-32-disk.txt`

- `ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=0 p3_skip=0`
- `p3-security: passed=2 failed=0 skipped=0`
- `system-manager` 套件（来源：`serial-32-disk-raw.txt`）：

```
[KUNIT] suite: system-manager (0x00000006 tests)
[KUNIT] result: 0x00000006 passed, 0x00000000 failed, 0x00000000 skipped
```

### 3.3 64 位默认 HPET 桌面启动

日志：`serial-64-default-desktop-raw.txt`（原始串口）；桌面/锁屏/登录截图见 `frames64/`。

- HPET 在跑，未使用 `-no-hpet`（来源：`serial-64-default-desktop-raw.txt`）：

```
[HPET] primary tick armed: timers=0x3 period_ticks=0x186A0 gsi=0x10 route=0x10 intcap=0x4 vec=0x42 rc=0x0
[HPET] tick对账 kernel_tick=0x376 hpet_tick=0xD
[DESKTOP64] Desktop running with mouse support
```

- 默认构建不注册探针；内置五项保持 STUB（来源：`serial-64-default-desktop-raw.txt`）：

```
[SERVICES] registered: network (stub)
[SERVICES] registered: audio (stub)
[SERVICES] registered: wifi (stub)
[SERVICES] registered: bluetooth (stub)
[SERVICES] registered: gui (stub)
[SERVICES] boot services started: 5/5
```

### 3.4 探针夹具：失败→退避→重试→成功链路

日志：`serial-64-probe-raw.txt`（原始串口，探针取证镜像）；shell 截图见 `frames64-probe/shell.png`。

链路按时间先后（来源：`serial-64-probe-raw.txt`）：

```
[SERVICES] registered: svc-probe
[SERVICES] start failed: svc-probe(code=-1)
[SERVICES] backoff: retry 1/3 svc-probe in 1 ticks
[SERVICES] retry after backoff: svc-probe
[SERVICES] start failed: svc-probe(code=-1)
[SERVICES] backoff: retry 2/3 svc-probe in 2 ticks
[SERVICES] retry after backoff: svc-probe
[SERVICES] start failed: svc-probe(code=-1)
[SERVICES] backoff: retry 3/3 svc-probe in 4 ticks
[SERVICES] retry after backoff: svc-probe
[SERVICES] started: svc-probe (healthy)
```

判据：先排程退避（事件 14 `backoff: retry n/3 ...`），退避到期后才出现
`retry after backoff: svc-probe`；三次失败后第 4 次启动成功并标 healthy。

### 3.5 64 位 `service list` / `service status` health 列

第一轮（退避期间，来源：`serial-64-probe-raw.txt`）：

```
hepu@os:~$ service list
[SVC] services=6 active=5 healthy=0
  network  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  audio  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  wifi  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  bluetooth  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  gui  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  svc-probe  failed  failed  enabled=1 restarts=1
```

第二轮（探针恢复 healthy 之后，来源：`serial-64-probe-raw.txt`）：

```
hepu@os:~$ service list
[SVC] services=6 active=6 healthy=1
  network  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  audio  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  wifi  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  bluetooth  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  gui  active  stub  enabled=1 restarts=0
    (stub: no real daemon/process, state machine only)
  svc-probe  active  healthy  enabled=1 restarts=0

hepu@os:~$ service status svc-probe
  svc-probe  active  healthy  enabled=1 restarts=0
  desc= TEST PROBE (not a product service): first 3 starts fail, then healthy; switch-gated fixture deps=0
```

### 3.6 静态校验

- `python3 tools/check_docs.py`：`warns=0 errors=0`（见 `check-docs.txt`）。
- `python3 tools/check_stage2_bss.py`：6 条规则全部 PASS（见 `check-stage2-bss.txt`）。

## 4. 产物清单

- `serial-32-nodisk-raw.txt` / `.txt`：32 位无盘冒烟原始串口与摘要。
- `serial-32-disk-raw.txt` / `.txt`：32 位带盘冒烟原始串口与摘要。
- `summary-32-nodisk.txt` / `summary-32-disk.txt`：冒烟脚本判定汇总。
- `serial-64-default-desktop-raw.txt`：64 位默认 HPET 桌面启动原始串口。
- `serial-64-probe-raw.txt`：探针取证启动原始串口（退避链路 + `service list` / `status` 输出）。
- `frames64/`：默认 64 位启动各阶段截图（菜单 / 加载 / 锁屏 / 登录 / 桌面）。
- `frames64-probe/shell.png`：探针取证文本 shell 截图。
- `build-all-default.txt`：默认全量构建日志；`build/` 下分架构构建与镜像打包日志、`size.txt`。
- `check-docs.txt` / `check-stage2-bss.txt`：两项静态校验原始输出。
- `boot-probe.sh`：探针取证启动脚本（QEMU 串行走 `/tmp/hepu-qemu.lock`，只按 pidfile 退出）。

## 5. 未闭环残余（本轮明确未做，未宣称已修）

1. **真实 daemon / 进程拉起未做**：内置服务的 `start_func` 仍是状态机打印桩，
   没有 fork / exec 真实用户态进程；探针成功只代表状态机接受请求。
2. **存活探测未做**：health 目前只区分 STUB / HEALTHY / FAILED，没有周期性探活
   （心跳、端口/进程存活检查）。
3. **崩溃恢复未做**：运行中进程崩溃后的检测与自动拉起不在本轮；`crash_ctx` 在
   64 位仍为显式 no-op 桩，不伪造恢复现场。
4. **内置五项保持 STUB**：network / audio / wifi / bluetooth / gui 仍是空壳状态机，
   未包装成真实后端。
5. **节拍粒度边界**：监督节拍在主循环空闲轮询点推进，节流间隔约 250ms；
   文本 shell 阻塞在读行等待期间不推进重试，下一轮循环回到轮询点时才补发。
   本轮未在中断上下文挂接任何 `services_start()` 调用。
