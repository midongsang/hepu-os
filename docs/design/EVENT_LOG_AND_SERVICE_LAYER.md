# 统一事件日志与系统服务层

日期：2026-11-26
范围：第一阶段 `feat/eventlog-system-2026-11-26`；第二阶段加固 `feat/log-hardening-2026-11-26`
状态：基础版与第二阶段加固（dmesg 切事件流、追加/一级轮转、emergency 异常路径）已落地；服务监督、用户态 logd、远程 syslog 仍未完成。

## 1. 为什么先做这一层

当前系统并不是“没有日志”，而是同一条事实被三套实现分别记录：

- `kernel/arch/x86/klog.c`：32 位内核文本环形日志，`klog_save_to_disk()` 写 `/hepu_boot.log`；
- `kernel/arch/x86_64/klog64.c`：64 位另一份 8KB 文本环；
- `kernel/arch/x86/boot_gfx.c`：启动画面自己的 `boot_log`；
- `kernel/arch/x86/crashlog.c`：崩溃帧追加进 klog，但不是独立事件流；
- `kernel/drivers/trace.c`：性能 trace，和系统事件没有统一序号。

服务层同样分裂：`kernel/sched/services.c` 有一套“systemd 式”注册/启动/停止表，`kernel/arch/x86/microg_service.c`、`kernel/arch/x86/distributed_os.c`、`kernel/arch/x86_64/hsservices.c` 又各自维护服务集合。结果是：日志无法按序号/级别/来源统一查询，服务没有单一事实源，32/64 的命令入口也不一致。

第一阶段不重写调度和桌面，只建立一条**可被两端共用的最小事实层**：

1. 统一事件日志 `event_log`：结构化记录，单一环形缓冲，平台只负责时钟/pid/落盘适配；
2. 统一服务表：32/64 共用 `kernel/sched/services.c`，shell 通过同一组 `services_*` API 管理；
3. 统一命令入口：`journal` / `log` / `service`，先让 32/64 都能查到同一份数据；
4. 保留旧 `klog` / `klog64` 作为文本适配器，避免一次性改动所有调用点。

## 2. 事件日志数据模型

头文件：`kernel/include/event_log.h`
实现：`kernel/shared/log/event_log.c`

```c
typedef struct {
    uint32_t seq;          /* 单调序号，从 1 开始 */
    uint64_t tick;         /* 平台单调 tick；32 位 PIT / 64 位 PIT64 */
    uint8_t  level;        /* DEBUG/INFO/WARN/ERROR/FATAL */
    uint32_t pid;          /* 平台 pid 回调，未知为 0 */
    uint32_t code;         /* 事件码：1=boot, 10=service register, 11=start, 12=stop, 13=fail, 77=selftest */
    char     source[16];   /* boot / kernel / kernel64 / service / kunit */
    char     msg[144];
} event_log_entry_t;
```

容量：256 条，满了覆盖最旧；消息 144 字节。选择小容量是因为当前阶段先保证“有统一序号和统一查询”，不是长期审计盘。

API：

- `event_log_init(clock_fn)`：初始化环形缓冲；
- `event_log_set_clock()` / `set_pid_source()`：平台适配；
- `event_log_set_sink()`：注册文本落盘函数；
- `event_log_emit(level, source, code, fmt, ...)`：格式化后写入；
- `event_log_snapshot()`：按时间顺序取条目，供 GUI/CLI；
- `event_log_format()`：按最低级别格式化，**优先保留最近能装进缓冲的条目**；
- `event_log_save()`：调用平台 sink；
- `event_log_clear()`：清空内存环（不动已落盘文件）。

### 2.1 平台接入

32 位（`kernel/arch/x86/klog.c`）：

- 时钟：`pit_get_ticks()`；
- pid：`sched_current_pid()`；
- 文件回调：`ide_ext2_read_file` / `ide_ext2_write_raw` / `ide_ext2_get_file_size` / `ide_ext2_delete`，根目录文件名 `hepu_events.log`；
- `klog_printf_lvl()`、`klog_puts()`、`klog_printf()` 在保留原文本环的同时写入 `event_log`。

64 位（`kernel/arch/x86_64/klog64.c`）：

- 时钟：`pit64_get_ticks()`；
- pid：`process64_get_current_pid()`；
- 文件回调：`ext2_64_read_file` / `ext2_64_write_file` / `ext2_64_stat` / `ext2_64_delete_file`，路径 `/hepu_events.log`；
- `klog64_log()` 同时写入 `event_log`。

这样旧的 `dmesg` / GUI 日志查看器仍能读文本环，新 `journal` 读统一事件流；两条路径不是互相替换，而是明确“legacy text adapter → structured event stream”的迁移关系。

### 2.2 落盘时机与追加/轮转

- 32 位：KUnit 结束后、`klog_save_to_disk()` 之后调用 `event_log_save()`；
- 64 位：ext2 根挂载后先做一次 early save，桌面 ready 后再 save 一次；
- 32 位无 ext2 盘时明确输出 `[EVENT] save skipped (no ext2)`，不伪造成功；
- 文件路径字面量只在 `event_log.h` 收敛一次：主文件 `/hepu_events.log`，轮转档 `/hepu_events.log.1`（仅一级旧档）。

**追加实现口径（重要限制）**：现有 ext2 驱动没有“原地追加写”原语，`write_file` 一律整文件重写。落盘走 `event_log_persist()`：先 `size` 取旧文件长度，`read` 读回旧内容，内存里与本轮格式化出的新块拼接后整文件写回。代价：

1. 每次落盘都要把旧文件整条读回并整块重写，盘上 IO 放大为旧文件大小 + 新块大小；
2. 旧内容受静态拼接缓冲上限约束（轮转阈值 `EVENT_LOG_ROTATE_BYTES = 8192` 字节），不是无限追加；
3. 掉电发生在“读旧—拼接—写回”中途时，整文件重写窗口内没有原子性保证，可能丢上一轮旧内容。

**轮转策略**：旧主文件已达阈值，或旧内容 + 新块超过阈值时，旧主文件降级为 `hepu_events.log.1`（覆盖已有一级旧档），新块写主文件；`.1` 不做二级归档。

### 2.3 异常上下文（emergency）路径

普通 `event_log_write()` 拿自旋锁并在写前走变参格式化，崩溃/异常隔离上下文不能使用（实测崩溃上下文做文件 IO 会触发 GPF）。第二阶段提供独立变体：

- `event_log_emergency_write(level, source, code, msg)`：无锁、定长环 `EVENT_EMERG_CAP = 16`、不变参格式化（调用方手工拼好消息）、pid 记 0；
- 环满后溢出条目计入 `emerg_drops`，并原地更新一条 `[EMERG-STAT] emergency overflow drops=<N>` 统计条目；
- 32 位 `crashlog_append()`（idt 异常隔离上下文）走 `klog_emergency_puts()`（纯内存文本环）+ `event_log_emergency_write()`（emergency 环），不再经过锁保护的普通路径；
- `bsod` 串口直出路径本身不拿事件日志锁，维持现状。

**仍走普通路径**：启动序列、驱动/服务注册、shell 命令、KUnit 等非中断上下文；这些路径可以自旋锁 + `event_log_emit()` 变参格式化。

## 3. 服务表统一

`kernel/sched/services.c` 现在是 32/64 共同的注册/启动/停止/查询实现：

- 注册、启动、停止、失败都会写 `event_log`，来源 `service`；
- 事件码：10=registered、11=started、12=stopping、13=start failed；
- 64 位没有 32 位 `crash_ctx_snapshot_for_restart` 实现，`HEPU_KERNEL_64` 下提供显式 no-op 桩，避免链接失败，也不伪造恢复现场；
- 64 位启动时调用 `services_init()` + `services_start_boot()`，注册 network/audio/bluetooth/wifi/gui 五项；
- 32/64 shell 都提供 `service list|status|start|stop|restart|enable|disable`，读同一张表。

**边界要说清**：这五项内置服务的 start/stop 仍是打印空壳，`service start` 成功只代表服务状态机接受请求，不代表真实 daemon/进程已被拉起。第一阶段统一的是“服务事实源和管理入口”，不是服务监督能力。

## 4. 命令

32 位与 64 位都已接入：

```text
journal                 # INFO 及以上，统一事件流
journal -l 0..4         # 指定最低级别
journal stat            # 条数/容量/sink
journal save            # 立即写 /hepu_events.log
journal clear           # 清空内存环

dmesg                   # 默认读统一事件流（序号/tick/级别/pid/source/消息）
dmesg -l 0..4          # 事件级别过滤
dmesg -k | dmesg --legacy   # 显式回退旧 klog/klog64 文本环（兼容开关）

service list
service status <name>
service start|stop|restart|enable|disable <name>
```

32 位 `log` 是 `journal` 别名；64 位同样注册 `log`。`dmesg` 两臂输出字段一致：32 位 kstdio 不支持 `%llu`，64 位 tick 在共享格式化函数里用两个 `%08x`（高 32 位 + 低 32 位）输出。

## 5. 已验证证据

第一阶段见 `docs/status/evidence-eventlog-2026-11-26/`：

- 32 位无盘冒烟：`ktest_result=PASS`，新增 `event-log`、`system-manager` 套件全 PASS，p3 两项仍按无 ext2 显式 SKIP；
- 32 位带 ext2：`[EXT2] selftest: mounted`，`[EVENT] saved to /hepu_events.log`，debugfs 读到结构化文件；
- 64 位 UEFI：`[EVENT] early save...`、`[EVENT] saved...`，debugfs 读到 service register/start 与 boot64 事件；
- 32/64 构建均通过。

第二阶段加固见 `docs/status/evidence-log-hardening-2026-11-26/`：

- 32 位无盘：event-log 6 用例 4 passed / 0 failed / 2 skipped（追加、轮转两项无 ext2 后端显式 SKIP），p3-security 两项 SKIP 属预期，`[KTEST] RESULT: ALL PASS`；
- 32 位带盘：p3-security `passed=2 failed=0 skipped=0`，event-log 6/0/0，`[KTEST] RESULT: ALL PASS`；debugfs 读回 `/hepu_events.log`，含两次追加写入的 `MARK-A` / `MARK-B` 标记；
- 64 位 UEFI 三轮带盘（`snapshot=off`）：每轮两次 `[EVENT]` 落盘；第三轮后 debugfs 读回主文件，三轮的 boot64 入口与 network 注册事件均在，未超 8192 阈值故本轮未生成 `.1`；
- 轮转触发与 `.1` 生成由 32 位带盘 KUnit `rotation_creates_dot1` 在测试内下调阈值验证；
- 32/64 构建均 0 error，`python3 tools/check_docs.py` `warns=0 errors=0`。

## 6. 未完成与下一阶段

1. ~~**dmesg 统一**~~（第二阶段已完成）：`dmesg` 默认读 `event_log`，支持 `-l` 级别过滤；legacy `klog`/`klog64` 文本环仅由 `-k` / `--legacy` 显式回退；
2. **用户态 logd**：内核侧追加写与按大小一级轮转已落地（见 2.2），仍缺用户态守护进程、按日期轮转、远程 syslog；
3. **服务监督**：`services.c` 仍是状态机 + 空壳启动函数，缺真实进程拉起、健康检查、失败退避和依赖启动顺序验证；
4. **命名空间收敛**：`microg_service.c`、`distributed_os.c`、`hsservices.c` 仍是平行服务集合，需要逐个标注归属或迁入统一表；
5. **管理面扩展**：`mount/umount`、`chmod/chown`、`useradd/userdel`、`fsck/mkfs` 仍是缺口，不能因为加了 `journal`/`service` 就宣称系统管理完整；
6. ~~**中断安全**~~（第二阶段已完成）：异常/崩溃上下文改走无锁、定长、无变参格式化的 emergency 变体；普通路径仍用自旋锁。遗留：emergency 环目前独立于普通环，尚未并入 `journal` 查询输出，需要时再做合并快照。
