# 统一事件日志第二阶段加固 · 证据（2026-11-26）

分支：`feat/log-hardening-2026-11-26`
基线：`9c942dc`（第一阶段合并提交）
范围：dmesg 切统一事件流、落盘跨启动追加 + 按大小一级轮转、异常上下文 emergency 变体、KUnit 扩充。

## 1. 改动范围（仅限本路独占文件）

`kernel/include/`

- `event_log.h`：路径/文件名/轮转阈值/emergency 容量常量收敛到头文件；平台文件回调结构体 `event_log_file_ops_t`；emergency 三件套、`event_log_persist`、文件访问器与测试阈值钩子声明。
- `klog.h`：`klog_emergency_puts()` 声明。

`kernel/shared/log/`

- `event_log.c`：普通环保留原自旋锁；新增独立 emergency 环（无锁、无变参格式化、pid 记 0、独立序号、溢出 drop 计数 + 原地统计条目 code=78）；`event_log_persist()` 走「取大小 → 读旧 → 内存拼接 → 整文件写回」，超阈值把旧主文件降级为 `.1`；阈值测试钩子。

`kernel/arch/`

- `x86/klog.c`（32 位）：file ops 适配 `ide_ext2_read_file` / `ide_ext2_write_raw` / `ide_ext2_get_file_size` / `ide_ext2_delete`；`klog_emergency_puts()` 只写内存文本环、不进锁路径。
- `x86/crashlog.c`：`crashlog_append()` 改走 `klog_emergency_puts()` + `event_log_emergency_write()`；顶部注释写明异常上下文禁令。
- `x86_64/klog64.c`（64 位）：file ops 适配 `ext2_64_read_file` / `ext2_64_write_file` / `ext2_64_stat` / `ext2_64_delete_file`（read_file 不 NUL 终止，按返回字节数处理）。

`kernel/sched/`

- `shell_cmd_fs.c`：32 位 `cmd_dmesg` 默认 `event_log_dump()`，`-l` 级别过滤，`-k` / `--legacy` 回退旧 klog 文本环。

`kernel/arch/x86_64/`

- `shell64.c`：64 位 `shell_cmd_dmesg` 同口径重写；legacy 分支保留原 `klog64_snapshot` 文本环输出。命令表与 cmds 补全数组未动（另一路职责）。

`kernel/kunit/`

- `kunit_event_log.c`：6 个用例 `stream_integrity`（序号单调/NUL 终止/级别范围）、`format_filters`（INFO 可见、FATAL 过滤）、`ring_wraps_oldest`（满环覆盖最旧、序号仍递增）、`emergency_drop_count`（溢出计数与统计条目）、`append_keeps_history`（带盘追加后旧内容仍在）、`rotation_creates_dot1`（带盘下调阈值后触发轮转并生成 `.1`）；无 ext2 后端时显式 `[KUNIT-SKIP]`。

文档：`docs/design/EVENT_LOG_AND_SERVICE_LAYER.md`（2.1/2.2/2.3/4/5/6 节）、`docs/status/SYSTEM_TOOLS.md`（dmesg、journald/syslog 行）、`docs/status/OPEN_BUGS.md`（OPEN-05 剩余外围能力）。

## 2. 构建

工作目录为 worktree 根：

```bash
make -j2                                  # 32 位，退出 0，无 error:
make -C kernel/arch/x86_64 -j2            # 64 位，退出 0，无 error:/undefined
python3 tools/check_docs.py               # warns=0 errors=0
```

构建与文档检查记录：`build/build-summary.txt`（含 64 位 `Nothing to be done for 'all'` 原文与 `check_docs.py` 输出）。
两次构建均为增量构建（对象已最新），退出码 0，日志内无 `error:` / `undefined`；产物 `build_v250/hepu-kernel.bin`、`build_x86_64/hepu-kernel64.bin`（1 008 912 字节）在位。

## 3. 32 位无盘冒烟

```bash
HEPU_EV=$PWD/ev-nodisk bash tools/regress/smoke32.sh --no-disk 70
```

串口：`serial32/nodisk-ser32.log`。

关键行：

```text
[KUNIT] suite: p3-security (0x00000002 tests)
[KUNIT-SKIP] p3-security/s3_normal_user_denied (ext2 backend unavailable)
[KUNIT-SKIP] p3-security/posix_error_table (ext2 backend unavailable)
[KUNIT] result: 0x00000000 passed, 0x00000000 failed, 0x00000002 skipped
[KUNIT] suite: event-log (0x00000006 tests)
[KUNIT-SKIP] event-log/append_keeps_history (ext2 backend unavailable)
[KUNIT-SKIP] event-log/rotation_creates_dot1 (ext2 backend unavailable)
[KUNIT] result: 0x00000004 passed, 0x00000000 failed, 0x00000002 skipped
[KTEST] RESULT: ALL PASS
```

p3-security 两项 SKIP 与 event-log 中两项磁盘用例 SKIP 均为无 ext2 后端的显式 SKIP，不计 PASS。

## 4. 32 位带 ext2 盘冒烟

```bash
HEPU_EV=$PWD/ev-disk2 bash tools/regress/smoke32.sh --disk 70
```

脚本退出 0：`serial=1 initrd=1 net=1 desk=1 fault=0`，`ext2_mounted=1`，`ktest_result=PASS`，`fail_suites=0 fail_cases=0 skip_cases=0 p3_skip=0`。

串口：`serial32/disk-ser32.log`（首轮截断留档 `serial32/disk-ser32-first-run.log`）。

关键行：

```text
[KUNIT] suite: p3-security (0x00000002 tests)
[KUNIT] result: 0x00000002 passed, 0x00000000 failed, 0x00000000 skipped
[KUNIT] suite: event-log (0x00000006 tests)
[KUNIT] result: 0x00000006 passed, 0x00000000 failed, 0x00000000 skipped
[KUNIT] suite: system-manager (0x00000001 tests)
[KUNIT] result: 0x00000001 passed, 0x00000000 failed, 0x00000000 skipped
[KTEST] RESULT: ALL PASS
[EVENT] saved to /hepu_events.log
```

磁盘读回：测试盘 LBA 2048 起、116 736 扇区为 ext2 分区，抽出后 debugfs 读回：

```bash
dd if=/tmp/hepu-smoke32-test.img of=/tmp/hepu32_ext2_h2.img bs=512 skip=2048 count=116736
debugfs -R 'cat /hepu_events.log' /tmp/hepu32_ext2_h2.img
```

读回内容：`readback/32-events-disk-run.txt`（4 130 字节，64 行）。追加用例写入的两次标记同时在位：

```text
MARK-A-append-test
MARK-B-append-test
00000220 00000000000000b1 INFO kunit pid=0 code=77 wrap flood 199
...
00000281 00000000000000b1 INFO kunit pid=0 code=77 wrap flood 260
```

本轮测试盘未达 8 KB 轮转阈值，盘上无 `.1`；轮转触发与 `.1` 生成由带盘 KUnit `rotation_creates_dot1` 在测试内临时阈值 64 字节验证（用例结束后还原/清理现场，故盘上不留 `.1`）。

## 5. 64 位 UEFI 三轮带盘取证

镜像：`build/hepu.img`（256 MB，GPT；ESP FAT32 @ LBA 2048–264191；ext2 root @ LBA 264192，245 760 扇区）。三轮均 `snapshot=off`（`snapshot=on` 会丢弃落盘写入），QEMU 走 OVMF pflash UEFI 启动，串行锁 `/tmp/hepu-qemu.lock`。

串口：`serial64/boot64-round1.log`、`boot64-round2.log`、`boot64-round3.log`。每轮两次落盘：

```text
[EVENT] early save to /hepu_events.log
[EVENT] saved to /hepu_events.log
[SERVICES] boot services started: 5/5
```

第三轮结束后从镜像重新抽出 ext2 分区读回：

```bash
dd if=build/hepu.img of=/tmp/hepu64_ext2_h3.img bs=512 skip=264192 count=245760
debugfs -R 'cat /hepu_events.log' /tmp/hepu64_ext2_h3.img
```

读回内容：`readback/64-events-after-3-boots.txt`（7 401 字节，93 行）。三轮历史均保留：

```text
00000001 0000000000000000 INFO  boot64       pid=0 code=1 64-bit kernel entry
00000002 0000000000000000 INFO  service      pid=0 code=10 registered network
...
00000017 0000000000000358 INFO  kernel64     pid=0 code=0 [GUI] desktop environment ready
```

计数核对：`64-bit kernel entry` 6 条、`registered network` 6 条（三轮 × 每轮 early + desktop 两块保存）；第一轮内容在第三轮之后仍在主文件中。主文件 7 401 字节，未超 8 192 阈值，本轮未生成 `.1`（轮转路径由 32 位带盘 KUnit 覆盖）。

## 6. 前后对比（第一阶段边界 → 第二阶段处置）

| 第一阶段边界（见 evidence-eventlog-2026-11-26/README.md） | 第二阶段处置 |
|---|---|
| `dmesg` 仍读 legacy klog/klog64 | 默认读 `event_log`，字段为 序号/tick/级别/pid/source/消息；`-l` 过滤；`-k`/`--legacy` 显式回退文本环 |
| 落盘每次整文件覆盖 `/hepu_events.log`，无追加、无轮转 | `event_log_persist()` 读旧拼接写回，跨启动保留旧内容；超 `EVENT_LOG_ROTATE_BYTES = 8192` 字节一级轮转 `.1`；路径字面量只在 `event_log.h` 收敛一次 |
| 异常上下文仍用普通自旋锁 + 变参格式化 | 独立 emergency 环：无锁、定长 16 条、无变参；溢出计入 drop 并写 code=78 统计条目；`crashlog_append()` 已改走该路径 |
| 事件环 KUnit 仅基础两项 | 扩到 6 项：序号单调、级别过滤、满环覆盖、emergency 计数、追加保留、轮转 `.1` |

### 追加实现的限制（不夸大）

现有 ext2 驱动没有“原地追加写”原语，`write_file` 一律整文件重写。落盘走「读旧 → 内存拼接 → 整文件写回」：

1. 每次落盘把旧文件整条读回并整块重写，盘上 IO 放大为旧文件大小 + 新块大小；
2. 旧内容受静态拼接缓冲上限约束（阈值 8 192 字节），不是无限追加；
3. “读旧—拼接—写回”窗口内无原子性保证，掉电发生在中途可能丢上一轮旧内容；
4. 32 位 `ide_ext2_writefile` 截断 4 095 字节，追加/轮转走 `ide_ext2_write_raw`，写完 `ide_ext2_sync()`。

## 7. 结论

- 32 位 `make -j2`、64 位 `make -C kernel/arch/x86_64 -j2` 均退出 0，无 error；
- 32 位无盘 `[KTEST] RESULT: ALL PASS`（p3-security 两项、event-log 两项磁盘用例显式 SKIP）；
- 32 位带盘 `p3-security: passed=2 failed=0 skipped=0`、event-log `6 passed 0 failed 0 skipped`、`[KTEST] RESULT: ALL PASS`；
- 64 位三轮 UEFI 带盘启动后读回 `/hepu_events.log`，第二轮、第三轮内容存在于盘上；
- `python3 tools/check_docs.py` `warns=0 errors=0`。

## 8. 未覆盖项 / 风险（不当作已完成）

- **用户态 logd、按日期轮转、远程 syslog 未做**：内核侧环 + 追加/大小轮转已落地，用户态守护进程、日期归档、远程输出属下一轮；已登记 `OPEN_BUGS.md` OPEN-05。
- **emergency 环未并入 `journal` 查询输出**：目前独立快照，需要时再做合并快照。
- **dmesg 命令行交互未在 32 位串口手工验证**：32 位无串口输入，命令行解析走代码核对 + KUnit 覆盖事件流；`-k`/`--legacy`/`-l` 的真实终端输出未手测。
- **轮转 `.1` 的真实落盘未在 64 位三轮带盘现场触发**：三轮总量未超 8 192 阈值；轮转路径证据来自 32 位带盘 KUnit（阈值临时下调）。64 位轮转 file ops 与 32 位同构，未独立跑到 `.1` 文件。
- **掉电一致性**：追加/轮转是整文件重写窗口，无原子性保证；未做写中途掉电夹具。
- 未碰：`services.c` 服务状态机与 `service` 命令（另一路）、`shell64.c` 命令表与两处 cmds 补全数组（第三路）、`shell_execute.c` 已有分支顺序。64 位 `journal`/`log`/`service` 是否进补全数组由第三路核对，本路未改。
