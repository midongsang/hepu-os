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

`kernel/sched/services.c` 是 32/64 共同的注册/启动/停止/查询实现：

- 注册、启动、停止、失败都会写 `event_log`，来源 `service`；
- 事件码：10=registered、11=started、12=stopping、13=start failed（第一阶段沿用）；
- 第二阶段新增局部事件码（常量定义在 `services.c` 内，未改 `kernel/include/event_log.h`）：
  14=backoff scheduled（已安排退避重试）、15=give up（超过重试上限停止重试）、
  16=dependency error（缺依赖 / 循环依赖）；
- 64 位没有 32 位 `crash_ctx_snapshot_for_restart` 实现，`HEPU_KERNEL_64` 下提供显式 no-op 桩，避免链接失败，也不伪造恢复现场；
- 64 位启动时调用 `services_init()` + `services_start_boot()`，注册 network/audio/wifi/bluetooth/gui 五项；
- 32/64 shell 都提供 `service list|status|start|stop|restart|enable|disable`，读同一张表。

### 3.1 依赖拓扑顺序（第二阶段）

- 服务定义带前置依赖 `depends[]`；`services_start_boot()` 先做全图校验
  （`services_validate_graph()`），再按 Kahn 拓扑排序启动，依赖先于被依赖者；
- 缺依赖：事件 16 同时报出“谁依赖谁”（`<svc> needs <dep>`），该服务置 FAILED，
  不静默跳过、不假装启动；
- 循环依赖：DFS 回边检出，事件 16 记录环上两端，环上服务剔除出启动序列；
- 内置依赖关系：wifi→network，bluetooth→wifi，gui→network+audio。

### 3.2 健康状态与 STUB 标注（第二阶段）

运行时健康状态 `service_health_t`：`HEALTH_UNKNOWN / HEALTH_STUB /
HEALTH_HEALTHY / HEALTH_FAILED`。

- `STUB`：无真实 daemon/进程，仅状态机空壳。五项内置服务全部标 STUB；
- `service start` 返回成功只代表状态机接受请求；空壳服务保持 STUB，
  **不得**报 HEALTHY。`services_is_healthy()` 仅认 HEALTHY，STUB/FAILED 均不算健康；
- `service list/status` 输出健康列；STUB 行附 “no real daemon/process,
  state machine only” 备注。第三阶段起 64 位 `shell64.c` 的 `shell_cmd_service()`
  与 32 位 `shell_cmd_system.c` 同一口径：`list`/`status` 都带 health 列与 STUB 备注，
  `list` 头部打印 `services=/ active=/ healthy=` 计数。

### 3.3 失败计数与指数退避（第二阶段）

- 每次失败记 `fail_count`；可重启服务按 tick 指数退避，退避间隔
  1/2/4/8 tick（基数 1，位移封顶 3）；
- 达到重试上限（默认 3，可按服务 `max_retries` 覆盖）后置 FAILED 并停止重试，
  事件 15 记 give up；退避排程记事件 14；
- 退避判定在 `services_monitor_tick()` 内按单调 tick 评估；KUnit 可通过
  `services_set_clock_hook()` 替换时钟做确定性验证。
- **生产调度接入口径（第三阶段）**：`services_monitor_poll()` 由两侧真实主循环
  节流调用——64 位 `desktop64.c` 桌面主循环与 `shell64.c` 文本 shell 循环、
  32 位 `shell.c` 文本 shell 循环；内部按约 250ms 收敛到一次 `services_monitor_tick()`，
  单次只扫表，不构成可感知卡顿。退避到期的重试（含 `services_start()` 及其
  `vga_printf`/`event_log_emit`/可能的落盘）只在主循环上下文执行；PIT/键盘/串口
  中断里只允许置标志，不得直接调 `services_start()`。

### 3.4 命名空间收敛（第二阶段）

平行集合盘点与处置见
`docs/status/evidence-service-supervisor-2026-11-26/service-registry-mapping.md`。
64 位音频后端 `hs_audio_init()` 已绑定到统一表 `audio` 服务；microG 槽位、
分布式远端能力、IPC 名字表、应用框架组件分属不同抽象层，保留并标注归属，
不作为第二套系统服务注册表。

**边界要说清**：这五项内置服务的 start/stop 仍是打印空壳（64 位音频仅初始化
后端结构体），健康标 STUB 不代表真实 daemon/进程已被拉起。本轮统一的是
“服务事实源、启动顺序、健康标注与退避监督框架”，不是真实进程拉起能力。

**验收探针夹具（非产品服务）**：仅当启动参数带 `hepu.svcprobe=1`（32 位
multiboot cmdline / 64 位 UEFI cmdline）或构建显式打开 `HEPU_SVC_PROBE` 宏时，
`services_start_boot()` 才额外注册 `svc-probe`。夹具前 3 次 `start_func` 确定性失败、
第 4 次成功，用于在真实 boot 中观察失败→退避排程→退避到期重试→成功(healthy)链路；
描述字段明确标注 `TEST PROBE (not a product service)`，不与内置五项 STUB 混在一起，
默认发布构建不注册该探针。

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

第二阶段（日志加固）证据见 `docs/status/evidence-log-hardening-2026-11-26/`；

- 32 位无盘：event-log 6 用例 4 passed / 0 failed / 2 skipped（追加、轮转两项无 ext2 后端显式 SKIP），p3-security 两项 SKIP 属预期，`[KTEST] RESULT: ALL PASS`；
- 32 位带盘：p3-security `passed=2 failed=0 skipped=0`，event-log 6/0/0，`[KTEST] RESULT: ALL PASS`；debugfs 读回 `/hepu_events.log`，含两次追加写入的 `MARK-A` / `MARK-B` 标记；
- 64 位 UEFI 三轮带盘（`snapshot=off`）：每轮两次 `[EVENT]` 落盘；第三轮后 debugfs 读回主文件，三轮的 boot64 入口与 network 注册事件均在，未超 8192 阈值故未生成 `.1`；
- 轮转触发与 `.1` 生成由 32 位带盘 KUnit `rotation_creates_dot1` 在测试内下调阈值验证；
- 32/64 构建均 0 error，`python3 tools/check_docs.py` `warns=0 errors=0`。

第二阶段（服务监督）证据见 `docs/status/evidence-service-supervisor-2026-11-26/`：32 位无盘/带盘冒烟、
落盘事件日志（STUB 标注、拓扑顺序、循环/缺依赖事件、退避链 13/14/15）、平行集合映射表；
64 位 UEFI 启动顺序与 `boot services started: N/N` 见该目录串口日志。

第三阶段（监督节拍接入与 64 位展示对齐）证据见 `docs/status/evidence-open06-2026-11-26/`：
32 位无盘/带盘冒烟 `system-manager` 6/0/0；64 位默认 HPET 桌面启动；探针真实 boot
失败→退避→重试→成功链路串口摘录，以及 `service list` / `service status svc-probe` 的
health 列输出。

## 6. 未完成与下一阶段

1. ~~**dmesg 统一**~~（第二阶段已完成）：`dmesg` 默认读 `event_log`，支持 `-l` 级别过滤；legacy `klog`/`klog64` 文本环仅由 `-k` / `--legacy` 显式回退；
2. **用户态 logd**：内核侧追加写与按大小一级轮转已落地（见 2.2），仍缺用户态守护进程、按日期轮转、远程 syslog（登记 OPEN-05）；
3. **服务监督**：依赖拓扑顺序、健康状态（STUB/HEALTHY/FAILED）、失败计数与 tick 指数退避、上限放弃已落地；第三阶段已把 `services_monitor_poll()` 接入两侧真实主循环节流节拍，64 位 `service list/status` 展示口径与 32 位对齐。仍未做：真实进程/daemon 拉起、存活探测与崩溃恢复，内置五项仍是空壳（残余登记 OPEN-06）；
4. **命名空间收敛**：已完成盘点映射并把 64 位音频后端迁入统一表绑定；`microg_service.c`、`distributed_os.c`、IPC 名字表、应用框架组件分属不同抽象层，保留并标注归属；跨层映射与用户态服务化仍待后续；
5. **管理面扩展**：`mount/umount`、`chmod/chown`、`useradd/userdel`、`fsck/mkfs` 仍是缺口，不能因为加了 `journal`/`service` 就宣称系统管理完整；
6. ~~**中断安全**~~（第二阶段已完成）：异常/崩溃上下文改走无锁、定长、无变参格式化的 emergency 变体；普通路径仍用自旋锁。遗留：emergency 环目前独立于普通环，尚未并入 `journal` 查询输出，需要时再做合并快照。
