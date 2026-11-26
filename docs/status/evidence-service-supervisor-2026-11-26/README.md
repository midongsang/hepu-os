# 服务监督第二阶段证据：依赖顺序 / 健康检查 / 重启退避 / 命名空间收敛（2026-11-26）

分支：`feat/service-supervisor-2026-11-26`
基线：main 合并提交 `9c942dc`

## 修改范围

- `kernel/include/services.h`：健康枚举（`HEALTH_UNKNOWN/HEALTH_STUB/HEALTHY/FAILED`）、def 新增 `stub`/`max_retries`、runtime 新增 `health`/`fail_count`/`retry_attempt`/`backoff_until`；新 API `services_is_healthy`、`services_healthy_count`、`services_validate_graph`、`services_set_clock_hook`。
- `kernel/sched/services.c`：依赖拓扑启动、缺依赖/循环依赖检出、健康标注、失败计数与 tick 指数退避、局部事件码常量、64 位音频后端绑定。
- `kernel/sched/shell_cmd_system.c`：`service list`/`status` 区段加 health 列、STUB 备注与 `healthy=` 计数头行（32 位 shell；64 位 `shell64.c` 展示归第三路，未改）。
- `kernel/arch/x86/microg_service.c` + `kernel/include/microg_service.h`：归属注释。
- `kernel/arch/x86/distributed_os.c` + `kernel/include/distributed_os.h`：归属注释。
- `kernel/arch/x86_64/hsservices.c` + `hsservices.h`：归属注释；`hs_services_init()` 移除音频重复初始化（音频收敛到统一表 `audio` 服务）。
- `kernel/kunit/kunit_system_manager.c`：`system-manager` 套件扩到 6 用例。
- 文档：`docs/design/EVENT_LOG_AND_SERVICE_LAYER.md` 第 3 节（3.1~3.4）与第 6 节、`docs/status/SYSTEM_TOOLS.md`、`docs/status/OPEN_BUGS.md`（OPEN-05）。

事件码在 `services.c` 内局部定义，未改公共头 `kernel/include/event_log.h`：

| 码 | 语义 | 级别 |
|---|---|---|
| 10 | 已注册（沿用第一阶段） | INFO |
| 11 | 已启动（沿用） | INFO |
| 12 | 停止中（沿用） | INFO |
| 13 | 启动失败（沿用） | ERROR |
| 14 | 退避排程（新增） | WARN |
| 15 | 达到上限放弃（新增） | ERROR |
| 16 | 依赖错误：循环 / 缺依赖 / 依赖未激活（新增） | ERROR |

退避参数（`services.c` 局部常量）：默认最多重试 3 次；退避间隔 `1/2/4/8` tick（`1 << shift`，shift 上限 3）；到上限后置 `HEALTH_FAILED` 且不再重试。

## 构建

```text
make -j2                                  # 32 位，0 error
make -C kernel/arch/x86_64 -j2            # 64 位，0 error
python3 tools/check_docs.py               # warns=0 errors=0
```

## 32 位无盘冒烟

命令：

```bash
cd <worktree>
HEPU_EV=$PWD/ev-nodisk bash tools/regress/smoke32.sh --no-disk 70
```

摘要见 `smoke32-nodisk-summary.txt`：

```text
mode=nodisk ... ext2_mounted=0 ext2_nodisk=1
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=2 p3_skip=2
p3-security: passed=0 failed=0 skipped=2
```

`system-manager` 套件 6 用例全过：

```text
[KUNIT] suite: system-manager (0x00000006 tests)
[KUNIT] result: 0x00000006 passed, 0x00000000 failed, 0x00000000 skipped
[KTEST] RESULT: ALL PASS
```

p3-security 两项无 ext2 后端按预期 SKIP（SKIP 不算 PASS）：

```text
[KUNIT-SKIP] p3-security/s3_normal_user_denied (ext2 backend unavailable)
[KUNIT-SKIP] p3-security/posix_error_table (ext2 backend unavailable)
```

串口关键行摘录：`smoke32-nodisk-serial-key.txt`（仅保留 KTEST/KUNIT 套件与结果、
SKIP、自检结论、事件落盘等关键行；完整原始串口未入库）。

## 32 位带 ext2 盘

命令：

```bash
cd <worktree>
HEPU_EV=$PWD/ev-disk bash tools/regress/smoke32.sh --disk 70
```

摘要见 `smoke32-disk-summary.txt`：

```text
mode=disk ... ext2_mounted=1 ext2_nodisk=0
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=0 p3_skip=0
p3-security: passed=2 failed=0 skipped=0
```

`system-manager` 套件 6/6 通过；串口关键行摘录：`smoke32-disk-serial-key.txt`
（完整原始串口未入库）。

落盘事件日志（debugfs 从带盘镜像 ext2 分区读回 `/hepu_events.log`）：`hepu_events-32disk.txt`。关键片段：

内置五项均为 STUB，注册与启动事件显式标注 `(stub)`，未伪装成健康：

```text
00000010 ... code=10 registered network (stub)
00000011 ... code=10 registered audio (stub)
00000012 ... code=10 registered wifi (stub)
00000013 ... code=10 registered bluetooth (stub)
00000014 ... code=10 registered gui (stub)
00000015 ... code=11 started network (stub)
00000016 ... code=11 started audio (stub)
00000017 ... code=11 started wifi (stub)
00000018 ... code=11 started gui (stub)
00000019 ... code=11 started bluetooth (stub)
```

启动顺序为合法拓扑（network/audio 在前，wifi 依赖 network，gui 依赖 network+audio，bluetooth 依赖 wifi）：
network → audio → wifi → gui → bluetooth。

循环依赖检出（KUnit 夹具 `k-c1` ↔ `k-c2`）：

```text
00000029 ERROR service code=16 cycle: k-c2 depends on k-c1
```

缺依赖报错（KUnit 夹具 `k-m1` 需要不存在的 `k-nope`）：

```text
00000031 ERROR service code=16 dep missing: k-m1 needs k-nope
```

退避链（夹具 `k-flaky`，`max_retries=2`）：首次失败 → 退避 1 tick → 重试再失败 → 退避 2 tick → 第二次重试再失败 → 达到上限放弃：

```text
00000033 ERROR service code=13 start failed k-flaky rc=-1
00000034 WARN  service code=14 backoff retry 1/2 k-flaky delay=1 ticks
00000035 ERROR service code=13 start failed k-flaky rc=-1
00000036 WARN  service code=14 backoff retry 2/2 k-flaky delay=2 ticks
00000037 ERROR service code=13 start failed k-flaky rc=-1
00000038 ERROR service code=15 give up k-flaky after 3 failures
```

## 64 位 UEFI 启动

镜像由 `HEPU_EV=$PWD/ev-64 bash tools/regress/build_all.sh` 生成（UEFI 双内核镜像
`ev-64/build/hepu.img`，基于本轮新内核）；QEMU 经 OVMF pflash 启动，`snapshot=on`，
启动段串口摘录 `serial64-uefi.txt`（仅服务启动阶段，完整桌面轮询未入库）。

启动顺序为合法拓扑（network/audio 在前，wifi 依赖 network，gui 依赖 network+audio，
bluetooth 依赖 wifi）：

```text
[SERVICES] service manager init done
[SERVICES] start boot service ...
[SERVICES] registered: network (stub)
[SERVICES] registered: audio (stub)
[SERVICES] registered: wifi (stub)
[SERVICES] registered: bluetooth (stub)
[SERVICES] registered: gui (stub)
[SERVICES] start: network
[SVC] net service start
[SERVICES] started: network (stub)
[SERVICES] start: audio
[SVC] audio service start (backend: hs_audio_init)
[SERVICES] started: audio (stub)
[SERVICES] start: wifi
[SVC] WiFi service start
[SERVICES] started: wifi (stub)
[SERVICES] start: gui
[SVC] GUI service start
[SERVICES] started: gui (stub)
[SERVICES] start: bluetooth
[SVC] BT service start
[SERVICES] started: bluetooth (stub)
[SERVICES] boot services started: 5/5
```

健康口径：五项启动行均标 `(stub)`，串口全程无服务被报 `healthy`；64 位音频后端
`hs_audio_init()` 由统一表 `audio` 服务拉起（`backend: hs_audio_init`），启动成功
后仍保持 STUB，不伪装成健康。

## 平行集合盘点

见 `service-registry-mapping.md`：统一表为唯一系统服务生命周期事实源；microG 槽位、分布式 mesh 远端能力、IPC 名字表、应用框架组件保留为不同抽象层并标注归属；64 位音频后端已收敛进统一表 `audio` 服务，进程快照与文件监听保留为桌面后端组件。

## 边界（不当作已完成）

- 内置 network/audio/wifi/bluetooth/gui 仍是空壳：无真实 daemon/进程拉起、存活探测与崩溃恢复；STUB 仅表示"无真实 daemon/进程，仅状态机"。
- `services_monitor_tick()` 尚未接入生产周期调度，退避重试路径目前只由 KUnit 夹具（替换时钟）驱动；真实 boot 中失败服务不会被自动拉起。
- 64 位 `shell64.c` 的 `shell_cmd_service()` 展示列仍为第一阶段格式，未显示 health 列与 STUB 备注（命令表归第三路）。
- 上述残余边界已登记 `OPEN_BUGS.md` OPEN-05。
