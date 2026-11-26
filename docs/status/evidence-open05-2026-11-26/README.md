# OPEN-05 用户态 logd 与事件日志外围 —— 第四阶段证据（2026-11-26）

分支：`feat/open05-06-2026-11-26`
基线：`b43a467`（OPEN-06 两提交 `e537923` / `b43a467` 未改写）

本目录记录 OPEN-05 第四阶段的真实 QEMU 启动取证：事件环游标拉取 syscall、非驻留
`/bin/logd.elf` worker 按日期落盘与游标、64 位桌面主循环周期拉起、emergency 环并入
`journal` 输出，以及五条门禁回归。emergency 探针边界的单独取证见
[README-emergency.md](README-emergency.md)。

## 1. 改动范围

### 源码

| 文件 | 改动 |
| --- | --- |
| `kernel/include/syscall.h` | 新增 `SYS_EVENT_LOG_PULL 233`（号表唯一事实源） |
| `kernel/include/event_log.h` | 新增 `event_log_newest_seq()` / `event_log_pull_after()` 声明 |
| `kernel/shared/log/event_log.c` | 新增 `event_log_newest_seq()`、`event_log_pull_after()`；`event_log_dump()` 末尾追加 emergency 段（`---------- emergency ring ----------`，条目 `[EMERG]` 前缀） |
| `kernel/arch/x86_64/syscall64_full.c` | `sys_event_log_pull()` / `wrap_event_log()` 与分派表登记；`SYSCALL_EVENT_LOG_PULL` 别名宏 |
| `kernel/arch/x86_64/desktop64.c` | 桌面主循环内 6s 节流钩子：事件环出现新序号才 `hepu_exec("/bin/logd.elf")` |
| `kernel/kernel_main_64.c` | 公共启动段（图形/文本两路径都经过）的 emergency 探针注入点，仅由 `HEPU_EMERG_PROBE` 宏开启 |
| `kernel/arch/x86_64/Makefile` | `EMERG_PROBE=1` 编译开关，定义 `-DHEPU_EMERG_PROBE` |
| `user/ulib.h` | `syscall6` 封装、`hepu_event_entry_t` 镜像布局、`event_log_pull()` / `event_log_date()` |
| `user/Makefile` | 纳入 `logd.elf` 编译规则 |
| `user/apps/logd/logd.c` | 非驻留 worker：读 RTC 拼日期文件、读游标、拉取新条目、整文件写回、游标落盘、`exit(0)` |

### 接口口径

- syscall 233 cmd0：a2=`after_seq`，a3=条目缓冲（`hepu_event_entry_t[]`），a4=max≤64，
  a5=`out_last_seq`，a6=flags（bit0 附带 emergency 快照）；cmd1：a3 槽出 `uint32_t tm[6]`
  （年/月/日/时/分/秒）。
- 32 位路径共用号表但分派表不登记，未知号返回 -1（ENOSYS 口径）；32 位 logd 未实现。
- 事件条目 188 字节：seq@0 u32、tick@8 u64、level@16 u8、source_len@17 u8、pid@20 u32、
  code@24 u32、source@28[16]、msg@44[144]。

## 2. 复现命令

工作树：`/home/user/Doubao/chats/38452875115179010/wt-open0506`

```bash
source tools/regress/env.sh

# 1. 清干净再建：先清 64 位内核 / 32 位根 Makefile / user，删残留对象后全量重建
make -C kernel/arch/x86_64 clean
make clean
make -C user clean
rm -rf build_x86_64 build_v250
make -C user
bash tools/regress/build_all.sh

# 2. 默认桌面取证（复制镜像到本目录 build/logd.img 操作，不污染回归镜像）
bash docs/status/evidence-open05-2026-11-26/boot-logd.sh

# 3. 门禁
bash tools/regress/smoke32.sh --no-disk 70
bash tools/regress/smoke32.sh --disk 70
bash tools/regress/boot64.sh
python3 tools/check_docs.py
python3 tools/check_stage2_bss.py
```

探针取证镜像的构建与 `journal` 抓取见 [README-emergency.md](README-emergency.md)。

## 3. 取证结果与关键判据

### 3.1 干净重建与默认构建边界

原始检查：`build/default-build-checks.txt`

```
== strings open05-probe count ==
0
== strings svc-probe count ==
1
== probe macros in build64.log ==
0
```

- 默认内核 `hepu-kernel64.bin` 中 `open05-probe` 字符串计数 0；`build64.log` 编译命令
  不含 `-DHEPU_SVC_PROBE` / `-DHEPU_EMERG_PROBE`（出现 0 次）。
- 口径说明：`svc-probe` 字面量在默认 bin 中命中 1 条，属 OPEN-06 既有运行时门控设计——
  探针注册块由 `svc_probe_enabled()` 运行时判定（cmdline `hepu.svcprobe=1` 或
  `HEPU_SVC_PROBE` 宏），函数与字符串字面量被无条件编译进 rodata。运行时未注册的证据是：
  默认桌面串口 0 次 `svc-probe`（见 `serial64/serial-64-default-raw.txt`），构建日志无探针宏。
  本阶段未改动 OPEN-06 源码。

### 3.2 默认 HPET 桌面：logd 被节流拉起并正常退出

原始串口：`serial64/serial-64-default-raw.txt`（默认 HPET，未使用 `-no-hpet`）。

HPET 在跑（来源：`serial64/serial-64-default-raw.txt`）：

```
[HPET] primary tick armed: timers=0x3 period_ticks=0x186A0 gsi=0x10 route=0x10 intcap=0x4 vec=0x42 rc=0x0
[DESKTOP64] Desktop running with mouse support
```

内置五项保持 STUB，无任何 `svc-probe` 条目（来源同上）：

```
[SERVICES] registered: network (stub)
[SERVICES] registered: audio (stub)
[SERVICES] registered: wifi (stub)
[SERVICES] registered: bluetooth (stub)
[SERVICES] registered: gui (stub)
[SERVICES] boot services started: 5/5
```

worker 链路（来源同上）：

```
[LOGD] spawn /bin/logd.elf
[USER64] exec: /bin/logd.elf
[USER64] exec: read 8576 bytes
[UPROC] pid=100 slot=0 active=1
[LOGD] consumed=16 wrote=/var/log/hepu/2026-11-26.log bytes=1350 last_seq=16
[SYSCALL64] Process exited with code 0
[UPROC] exec path=/bin/logd.elf rc=100
```

判据：6s 节流后拉起 `/bin/logd.elf`；worker 消费 16 条、写入 dated 文件、游标到 16；
串口 `bytes=1350` 与读回文件实际字节数（1350）一致；进程 `code 0` 正常退出；
桌面主循环在 worker 退出后继续（串口后续仍有 `[P0] island` / `[PERF]` 推进行）。

### 3.3 ext2 读回：dated 文件连续、游标一致、事件逐条核对

宿主机 dd 抽 ext2 分区（LBA 264192，245760 扇区）后 `debugfs` 独立读回：

- `readback/64-dated-file.txt`：`/var/log/hepu/2026-11-26.log`，16 行，seq 从 1 连续到 16，
  1350 字节；`file` 判定为 `ASCII text`（非二进制）；每行 seq 为 8 位十进制、tick 为
  16 个 hex 字符；
- `readback/64-cursor.txt`：`/var/log/hepu/.cursor`，内容 `16`，等于 last_seq=16；
- `readback/64-kernel-events-snapshot.txt`：同镜像内核 `/hepu_events.log` 读回，作为对照表参照。

**逐字节核对**：`/hepu_events.log` 在启动过程中落盘两次——前 14 行是早期保存的
seq 1..14，15–30 行是后一次保存的完整 seq 1..16，第 31 行是 seq 17（桌面就绪）。
`diff readback/64-dated-file.txt` 对快照 15–30 行结果为**逐字节一致**（无差异行）；
前 14 行与快照 1–14 行亦一致。

事件逐条核对（左列串口原文，右列读回文件行）：

| 串口原文（`serial64/serial-64-default-raw.txt`） | 读回文件行（`readback/64-dated-file.txt`） |
| --- | --- |
| `=== HepuOS 64-bit Kernel booting ===`（启动横幅；事件 seq 1） | `00000001 0000000000000000 INFO  boot64  pid=0 code=1 64-bit kernel entry` |
| `[SERVICES] registered: network (stub)`（行 34；事件 seq 2） | `00000002 0000000000000000 INFO  service  pid=0 code=10 registered network (stub)` |
| `[SERVICES] started: network (stub)`（行 41；事件 seq 7） | `00000007 0000000000000000 INFO  service  pid=0 code=11 started network (stub)` |

### 3.4 门禁（默认构建 tip 上跑）

#### 32 位无盘冒烟：`smoke32.sh --no-disk 70`

原始串口：`serial32/serial-32-nodisk-raw.txt`；脚本判定汇总：`serial32/smoke32-nodisk.txt`。

```
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=6 p3_skip=2
[KTEST] RESULT: ALL PASS
[KUNIT] suite: system-manager (0x00000006 tests)
[KUNIT] result: 0x00000006 passed, 0x00000000 failed, 0x00000000 skipped
```

6 条用例显式 SKIP（p3-security 两条、event-log 两条、admin 两条），SKIP 不计 PASS，
与既有口径一致。

#### 32 位带盘冒烟：`smoke32.sh --disk 70`

原始串口：`serial32/serial-32-disk-raw.txt`；脚本判定汇总：`serial32/smoke32-disk.txt`
（本轮单独留存，不与无盘共用 `$EV/tests/smoke32.txt`）。

```
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=0 p3_skip=0
[KTEST] RESULT: ALL PASS
[KUNIT] suite: p3-security (0x00000002 tests)
[KUNIT] result: 0x00000002 passed, 0x00000000 failed, 0x00000000 skipped
[KUNIT] suite: system-manager (0x00000006 tests)
[KUNIT] result: 0x00000006 passed, 0x00000000 failed, 0x00000000 skipped
```

带盘环境 ext2 后端可用，全文件 `KUNIT-SKIP` 计数 0。

#### 64 位默认 HPET 桌面：`boot64.sh`

原始串口：`serial64/serial-64-boot64-gate-raw.txt`。

```
[HPET] primary tick armed: timers=0x3 period_ticks=0x186A0 gsi=0x10 route=0x10 intcap=0x4 vec=0x42 rc=0x0
[DESKTOP64] Desktop running with mouse support
```

#### 静态校验

- `python3 tools/check_docs.py`：`warns=0 errors=0`（`build/check-docs.txt`）。
- `python3 tools/check_stage2_bss.py`：6 条规则全部 PASS（`build/check-stage2-bss.txt`）。

## 4. 产物清单

- `boot-logd.sh`：默认桌面取证脚本（snapshot=off，复制镜像 `build/logd.img`，锁屏→口令→桌面停留）。
- `boot-probe.sh`：探针镜像取证脚本（文本 shell 跑 `journal` / `journal -l 0`）。
- `serial64/serial-64-default-raw.txt`：默认 HPET 桌面原始串口（logd 链路、五项 STUB、HPET）。
- `serial64/serial-64-boot64-gate-raw.txt`：`boot64.sh` 门禁原始串口。
- `serial64-probe/serial-64-probe-raw.txt`：探针镜像 `journal` 原文（见 README-emergency.md）。
- `serial32/serial-32-nodisk-raw.txt` / `.txt`、`serial32/smoke32-nodisk.txt`：无盘冒烟串口与脚本汇总。
- `serial32/serial-32-disk-raw.txt` / `.txt`、`serial32/smoke32-disk.txt`：带盘冒烟串口与脚本汇总。
- `readback/64-dated-file.txt`：debugfs 读回的 dated 文件（16 行，ASCII text，1350 字节）。
- `readback/64-cursor.txt`：读回的 `.cursor`（`16`）。
- `readback/64-kernel-events-snapshot.txt`：同镜像内核 `/hepu_events.log` 读回。
- `build/default-build-checks.txt`：strings 计数与构建日志探针宏检查。
- `build/check-docs.txt` / `build/check-stage2-bss.txt`：两项静态校验原始输出。
- `build/logd.img` / `build/probe.img`：取证镜像副本（构建产物，不纳入版本库）。

## 5. 未闭环残余（本轮明确未做，未宣称已修）

1. **远程 syslog 未做**：用户态 socket/sendto 当前为桩，无真实收包/网络发送证据。
2. **32 位 logd 未实现**：共用号表但 32 位分派表不登记 syscall 233，返回 -1（ENOSYS 口径）。
3. **常驻 daemon / 存活探测 / 崩溃恢复未做**：形态 B 为内核节流拉起的短命 worker，单次执行
   退出；BSP 无可抢占用户调度，常驻进程会冻结桌面主循环。
4. **掉电原子性未做**：ext2 无追加原语，链路为读旧文件→内存拼接→整文件覆盖写回；
   掉电窗口无原子保证，未做写完成前后 kill QEMU 的落盘核对。
5. **256 条环淘汰历史未加固**：游标落后过环容量即丢历史，未做淘汰历史的持久化兜底。
6. **跨午夜日期轮转未处理**：worker 单次执行按当时 RTC 日期命名文件；跨午夜后的新日期文件
   与旧日期文件归并策略未实现。
