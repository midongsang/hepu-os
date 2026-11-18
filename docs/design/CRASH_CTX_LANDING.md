# CRASH_CTX 落地记录（UNIX_LESSONS.md 第三节）

分支：`feat/crash-ctx`（基于 main `391e1db`）。本文件记录第三节"服务崩溃现场 +
结构化状态查询"在 32 位原型内核上的实际落地情况：数据结构、页归属与权限、syscall
契约、快路径开销、QEMU 实跑验收片段，以及未完成项与取舍。

## 1. 数据结构

字段名、顺序与 `docs/design/UNIX_LESSONS.md` 第三节冻结契约一致，未自创字段。
定义见 `kernel/include/crash_ctx.h`：

```c
#define CRASH_CTX_MAGIC   0x43524348u  /* 'CRCH' */
#define CRASH_MSG_LEN     96
#define CRASH_SLOT_MAX    32
#define CRASH_NAME_LEN    32

struct crash_ctx {
    uint32_t magic;                       /* 0x43524348 'CRCH' */
    uint32_t pid;
    uint64_t pc;                          /* 崩溃点 */
    uint64_t sp;
    uint64_t last_syscall;                /* 最近一次系统调用号 */
    uint64_t last_arg0;
    uint64_t last_ipc_tag;               /* 最近一次 IPC 的 tag */
    uint64_t regs[16];                    /* 关键寄存器快照 */
    char     msg[CRASH_MSG_LEN];          /* 人类可读的一行 */
};

struct crash_last_crash {
    uint64_t pc;
    uint64_t last_syscall;
    char     msg[CRASH_MSG_LEN];
    uint64_t ts;                          /* 单调时钟，ms */
};

/* 结构化查询返回的二进制记录（syscall 出参，按结构体直拷） */
struct svc_query {
    char                  name[CRASH_NAME_LEN];
    uint32_t              state;          /* 0=run 1=degraded 2=restarting 3=dead */
    uint32_t              restarts;
    uint64_t              uptime_ms;
    struct crash_last_crash last_crash;
};
```

状态枚举：`CRASH_SVC_RUN=0 / DEGRADED=1 / RESTARTING=2 / DEAD=3`，与文档一致。

## 2. 页归属与权限（取舍，不假装做不到的隔离）

设计稿要求"每服务一页，服务可写、diag/查询只读"。当前 32 位原型内核**没有**
真正的"一服务一用户页"映射机制，落地取舍：

- 现场槽位放在**内核静态固定表** `static struct crash_slot slots[32]`（BSS，
  不走 kheap、不逐进程分配堆页）。按 `pid` / 服务名双索引。
- 写入方：内核 syscall 入口、异常入口、服务注册路径（内核态写）。
- 用户态**不能**直接映射/写该表；唯一出口是查询 syscall，把 `struct svc_query`
  二进制拷到用户缓冲（只读导出）。
- 进程创建时在 `process_create()` 内调用 `crash_ctx_alloc(pid, name)` 绑定槽位；
  进程退出（`sys_exit` 各回收路径）与 `service_unregister_pid()` 调用
  `crash_ctx_release(pid)`：状态置 dead、停 uptime 表，**槽位与 last_crash 记录
  保留**供查询，不擦除、不 kfree。

### 分配页的复用 / 不泄漏论证

- 槽位是固定静态数组，**不存在逐进程的堆分配**，因此没有"崩溃后堆页泄漏"这一项。
- 同名服务被重新拉起时 `crash_ctx_alloc()` 先按 pid、再按名复用同一槽位，保留
  `restarts` 与 `last_crash` 历史；空闲槽位不足时返回 NULL（不阻断进程创建）。
- 已回收进程的栈/页目录仍由原有 `sys_exit` 路径释放；crash 模块只持有内嵌在
  静态表里的 `ctx`，不单独持有堆资源。

## 3. syscall 契约

- 号：`SYS_CRASH_QUERY = 238`（避开已用的 237 `SYS_CAP_AUDIT`）。
- `SYS_MAX` 在 `kernel/include/syscall.h` 与 `kernel/include/constants.h`
  两处同步为 **239**。
- 入参：
  - `a1`：用户态服务名字符串指针；`0` = 查询当前进程（按 pid）。
  - `a2`：用户态 `struct svc_query` 出参缓冲。
- 返回：`0` 成功；`-1` 未找到；`-2` 用户缓冲不可拷。
- 出参为二进制结构体直拷，**不返回文本、不做文本解析**。diag 侧按字段读取
  （见 `user/apps/diag/diag.c` 追加段，镜像同构布局 `diag_svc_query`）。

64 位表未加条目（主仓 `syscall64_full.c` 正被另一线改动，按约定不动）；64 位
未注册号走原有空表拒绝路径。共享头改动后已确认 64 位仍能编译通过。

## 4. 快路径开销论证（指令级）

syscall 入口记录挂在 `syscall_handler()` 表分发之前：

```c
if (current && current->crash_ctx) {
    current->crash_ctx->last_syscall = (uint64_t)num;
    current->crash_ctx->last_arg0     = (uint64_t)arg1;
}
```

- `crash_ctx` 指针在 `process_create()` 时缓存进 PCB（`process_t::crash_ctx`），
  快路径**不遍历服务表、不加锁**、不做字符串拷贝。
- 开销 = 两次条件判断 + 两次内存写（`num`/`arg1` 各存一次到 ctx 字段），无循环、
  无堆操作。
- IPC tag 在 `sys_ipc_send()` 里记一次 `last_ipc_tag`（同样只写缓存字段）。

异常入口（#DE/#UD/#GP 等用户态向量，以及 #PF 用户态回收助手）从中断帧取
pc/sp/寄存器快照，填 `ctx` 并把现场快照进槽位 `last_crash`（pc / last_syscall /
msg / ts），状态置 dead，`restarts++`。

## 5. 重启点快照

`kernel/sched/services.c` 两个既有重启点在 `restart_count++` 之前调用
`crash_ctx_snapshot_for_restart(pid)`：把 ctx 现场快照进 `last_crash`，状态置
`restarting`。这两个点面向内核 def 服务；用户态崩溃现场由异常入口直接快照。

## 6. QEMU 实跑验收（32 位，串口原文）

环境：32 位 `make` → `build_v250/kernel.elf`；QEMU `-m 256 -display none
-no-reboot`，串口落盘到本 worktree `.qemu-logs/crash-final.log`。

验收载体：AUTO 段静态用户入口 `crashsvc`（`shell.c`），进入 ring3 后先做一次
`SYS_YIELD(6)`（让 `last_syscall` 被快路径记为非零），随后除零触发 `#DE`。

串口关键片段：

```
[AUTO] crashsvc launch
[UPROC] exec-static name=crashsvc rc=3
[UEXC] Division By Zero
[CRASH-FRAME] eip=0x001A47BE cs=0x0000001B eflags=0x00000206 esp=0x4000FFF0 ss=0x00000023 err=0x00000000 eax=0x0000002A ebx=0x00000000 ecx=0x00000000 edx=0x00000000
[CRASHCTX] query crashsvc state=dead restarts=0x00000001
[CRASHCTX]   pc=0x001A47BE syscall=0x00000006 ts=0x00000096 msg=user #Division By Zero at 0x1a47be esp=0x4000fff0
[CRASHLOG] recorded: crashsvc
```

已验证：

- 崩溃后查询记录给出 `pc=0x1a47be`、`last_syscall=6`（SYS_YIELD）、
  `msg="user #Division By Zero at 0x1a47be esp=0x4000fff0"`、`state=dead`、
  `restarts=1`、`ts=0x96`。
- 二进制记录由 `crash_query_by_pid()` 填充（与 `sys_crash_query` 同一序列化路径），
  串口打印的字段即出参结构体内容。

### 未达到项与确切卡点

- **连崩 3 次（restarts=3）未在本轮达到，实际只到 1 次。** 卡点：当前
  `sys_exit` 在"无其他活进程"时回收全部进程并 `longjmp` 回主线 Shell，AUTO 段的
  C 调用栈被展开，无法在同一轮 AUTO 内连续拉起同一服务三次；系统里也还没有
  supervisor 自动拉起用户服务的闭环（`services_monitor_tick` 只面向内核 def 服务，
  且本环境未被驱动到该分支）。`restarts` 累计逻辑、同名槽位复用已就位，差一个
  真正的重启调度闭环。
- **#PF 用户态隔离**：两个页错误处理器（非 PAE / PAE）在确认 `user &&
  current->privilege==PRIV_USER` 时改走 `idt_pf_user_reap()`（切回内核目录 →
  填现场 → 杀进程），不再整机停机；但本轮验收用的是 `#DE`，#PF 路径未在串口上
  单独跑通一次空指针用例。
- **用户态 syscall 出参**：`diag.hug` 的查询段落已追加并通过 32 位编译，但本轮
  启动流程里 `diag.hug` 未被自动拉起（其后的 `linuxhello` 在本环境阻塞，靠后的
  AUTO 到不了），所以用户态 `hepu_syscall2(SYS_CRASH_QUERY, ...)` 这条 ABI 链路
  没有串口证据；内核侧同构 `svc_query` 填充已验证。
- `last_arg0` 对 YIELD 为 0；尚未跑一个带非零第 0 参数的调用做对照。

## 7. 涉及文件

- 新增：`kernel/include/crash_ctx.h`、`kernel/crash_ctx.c`
- 改动：
  - `kernel/sched/process.c`：创建时绑定槽位
  - `kernel/arch/x86/syscall.c`：快路径记录 + 分发表条目
  - `kernel/arch/x86/sys_ipc.c`：记 `last_ipc_tag`
  - `kernel/arch/x86/idt.c`：用户异常入口填现场；`idt_pf_user_reap`
  - `kernel/mm/paging.c`、`kernel/mm/paging_pae.c`：用户 #PF 走回收助手
  - `kernel/sched/services.c`：两个重启点先快照再自增
  - `kernel/ipc/service.c`：注册绑名 / 注销释放
  - `kernel/sched/sched.c`：`crash_ctx_init()`
  - `kernel/sched/shell.c`：AUTO 验收载体
  - `kernel/include/process.h`：PCB 末尾挂 `crash_ctx` 指针
  - `kernel/include/syscall.h`、`kernel/include/constants.h`：号段与 `SYS_MAX`
  - `user/apps/diag/diag.c`：追加结构化查询打印段（未重排原有诊断表）

## 8. 合并对账提醒

本分支把 `SYS_MAX` 从 238 提到 239、新增 `SYS_CRASH_QUERY=238`。合并回 main 时
需在 `syscall.h` 与 `constants.h` **两处同步** `SYS_MAX=239`；主仓 237 仍是
`SYS_CAP_AUDIT`，238 为本分支 `SYS_CRASH_QUERY`，号段不要复用。
