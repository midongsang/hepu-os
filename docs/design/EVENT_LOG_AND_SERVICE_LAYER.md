# 统一事件日志与系统服务层（第一阶段）

日期：2026-11-26
范围：`feat/eventlog-system-2026-11-26`
状态：基础版已落地；服务监督、用户态 logd、日志轮转和运行时挂载仍未完成。

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
- sink：`ide_ext2_writefile("hepu_events.log", text)`；
- `klog_printf_lvl()`、`klog_puts()`、`klog_printf()` 在保留原文本环的同时写入 `event_log`。

64 位（`kernel/arch/x86_64/klog64.c`）：

- 时钟：`pit64_get_ticks()`；
- pid：`process64_get_current_pid()`；
- sink：`ext2_64_write_file("/hepu_events.log", text, len)`；
- `klog64_log()` 同时写入 `event_log`。

这样旧的 `dmesg` / GUI 日志查看器仍能读文本环，新 `journal` 读统一事件流；两条路径不是互相替换，而是明确“legacy text adapter → structured event stream”的迁移关系。

### 2.2 落盘时机

- 32 位：KUnit 结束后、`klog_save_to_disk()` 之后调用 `event_log_save()`；
- 64 位：ext2 根挂载后先做一次 early save，桌面 ready 后再 save 一次；
- 32 位无 ext2 盘时明确输出 `[EVENT] save skipped (no ext2)`，不伪造成功；
- 文件路径统一为 `/hepu_events.log`，每次覆盖写（当前不是追加日志）。

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

service list
service status <name>
service start|stop|restart|enable|disable <name>
```

32 位 `log` 是 `journal` 别名；64 位同样注册 `log`。

## 5. 已验证证据

见 `docs/status/evidence-eventlog-2026-11-26/`：

- 32 位无盘冒烟：`ktest_result=PASS`，新增 `event-log`、`system-manager` 套件全 PASS，p3 两项仍按无 ext2 显式 SKIP；
- 32 位带 ext2：`[EXT2] selftest: mounted`，`[EVENT] saved to /hepu_events.log`，debugfs 读到结构化文件；
- 64 位 UEFI：`[EVENT] early save...`、`[EVENT] saved...`，debugfs 读到 service register/start 与 boot64 事件；
- 32/64 构建均通过。

## 6. 未完成与下一阶段

1. **dmesg 统一**：当前 `dmesg` 仍读 legacy `klog` / `klog64`，应改为读 `event_log`，legacy 只保留兼容开关；
2. **用户态 logd**：当前只有内核 sink，缺用户态守护进程、追加写、按大小/日期轮转、远程 syslog；
3. **服务监督**：`services.c` 仍是状态机 + 空壳启动函数，缺真实进程拉起、健康检查、失败退避和依赖启动顺序验证；
4. **命名空间收敛**：`microg_service.c`、`distributed_os.c`、`hsservices.c` 仍是平行服务集合，需要逐个标注归属或迁入统一表；
5. **管理面扩展**：`mount/umount`、`chmod/chown`、`useradd/userdel`、`fsck/mkfs` 仍是缺口，不能因为加了 `journal`/`service` 就宣称系统管理完整；
6. **中断安全**：`event_log_write()` 在异常上下文使用自旋锁和格式化，后续需要区分 emergency 路径，避免 crashlog 与普通日志互相阻塞。
