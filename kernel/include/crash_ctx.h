/* ============================================================================
 * 赫普OS - 服务崩溃现场 + 结构化状态查询
 * 对应 docs/design/UNIX_LESSONS.md 第三节。
 *
 * 数据结构字段名/顺序按设计文档冻结，不得自行增删改序。
 * ========================================================================== */
#ifndef CRASH_CTX_H
#define CRASH_CTX_H

#include <stdint.h>

#define CRASH_CTX_MAGIC   0x43524348u  /* 'CRCH' */
#define CRASH_MSG_LEN     96
#define CRASH_SLOT_MAX    32
#define CRASH_NAME_LEN    32

/* 崩溃现场：崩溃时由内核异常入口 / syscall 快路径写入。
 * 布局与 UNIX_LESSONS.md 第三节 struct crash_ctx 一致。 */
struct crash_ctx {
    uint32_t magic;                       /* 0x43524348 'CRCH' */
    uint32_t pid;
    uint64_t pc;                          /* 崩溃点 */
    uint64_t sp;
    uint64_t last_syscall;                /* 最近一次系统调用号 */
    uint64_t last_arg0;
    uint64_t last_ipc_tag;                /* 最近一次 IPC 的 tag */
    uint64_t regs[16];                    /* 关键寄存器快照 */
    char     msg[CRASH_MSG_LEN];          /* 人类可读的一行 */
};

/* 服务状态（结构化查询契约，不做文本解析） */
enum crash_svc_state {
    CRASH_SVC_RUN       = 0,              /* 运行中 */
    CRASH_SVC_DEGRADED  = 1,              /* 降级 */
    CRASH_SVC_RESTARTING= 2,              /* 重启中 */
    CRASH_SVC_DEAD      = 3,              /* 已退出/崩溃 */
};

/* 最近一次崩溃快照（持久保留在服务记录上，ctx 本身随进程回收） */
struct crash_last_crash {
    uint64_t pc;
    uint64_t last_syscall;
    char     msg[CRASH_MSG_LEN];
    uint64_t ts;                          /* 单调时钟，单位 ms */
};

/* 结构化状态查询返回的二进制记录（syscall 出参，按结构体直拷） */
struct svc_query {
    char                  name[CRASH_NAME_LEN];
    uint32_t              state;          /* enum crash_svc_state */
    uint32_t              restarts;        /* 崩溃/重启次数 */
    uint64_t              uptime_ms;       /* 本次运行时长（dead 时为死亡时刻） */
    struct crash_last_crash last_crash;
};

/* 模块初始化（启动早期调用一次） */
void crash_ctx_init(void);

/* 按 pid 分配/复用一个现场槽位，绑定服务名；返回 ctx 指针（失败返回 NULL）。
 * 槽位是内核静态表项，不走 kheap，不存在逐进程堆泄漏。 */
struct crash_ctx *crash_ctx_alloc(uint32_t pid, const char *name);

/* 进程退出：标记 dead、停表；槽位记录保留供查询（不擦除 last_crash）。 */
void crash_ctx_release(uint32_t pid);

/* 按 pid 取现场指针（未分配返回 NULL）。 */
struct crash_ctx *crash_ctx_by_pid(uint32_t pid);

/* 快路径记录：syscall 入口调用，仅两次内存写，不查表、不加锁。 */
void crash_ctx_record_syscall(struct crash_ctx *c, uint32_t num, uint32_t arg0);
void crash_ctx_record_ipc(struct crash_ctx *c, uint32_t tag);

/* 异常入口：填 pc/sp/regs/msg，并把现场快照进服务记录 last_crash。
 * regs 可为 NULL；nregs 为有效寄存器个数（≤16）。 */
void crash_ctx_fill_fault(struct crash_ctx *c, uint64_t pc, uint64_t sp,
                          const uint32_t *regs, int nregs, const char *msg);

/* sys_service_register 时把服务名绑定到 pid 对应槽位。 */
void crash_ctx_bind_name(uint32_t pid, const char *name);

/* 重启点（services.c 自增 restart_count 之前）调用：
 * 把当前现场快照进 last_crash，状态置 restarting。 */
void crash_ctx_snapshot_for_restart(uint32_t pid);

/* 结构化查询：按名字 / 按 pid 取二进制记录。0 成功，<0 未找到。 */
int crash_query_by_name(const char *name, struct svc_query *out);
int crash_query_by_pid(uint32_t pid, struct svc_query *out);

/* syscall 实现：a1=服务名用户指针(0=查自己), a2=struct svc_query 出参缓冲 */
uint32_t sys_crash_query(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);

/* 调试/验收：把所有槽位记录打到串口与 VGA（固定格式，逐二进制记录消费） */
void crash_ctx_dump_all(void);

/* 崩溃时刻：按 pid 取出与 syscall 同构的 struct svc_query 二进制记录并打印 */
void crash_ctx_dump_query_by_pid(uint32_t pid);

#endif /* CRASH_CTX_H */
