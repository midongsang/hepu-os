/* ============================================================================
 * 赫普OS - 服务崩溃现场 + 结构化状态查询（内核侧实现）
 *
 * 取舍说明（详见 docs/design/CRASH_CTX_LANDING.md）：
 * 32 位原型内核尚未实现"一服务一用户页"的真实内存隔离，现场槽位放在内核
 * 静态固定表里（BSS，不走 kheap），按 pid / 服务名索引；用户态只能通过
 * 查询 syscall 只读导出二进制记录，不能直接映射写该表。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "crash_ctx.h"
#include "pit.h"
#include "sched.h"
#include "vga.h"

struct crash_slot {
    int                    used;
    char                   name[CRASH_NAME_LEN];
    uint32_t               pid;
    struct crash_ctx       ctx;
    uint32_t               state;
    uint32_t               restarts;
    uint64_t               start_tick;
    uint64_t               uptime_ms;
    struct crash_last_crash last;
};

static struct crash_slot slots[CRASH_SLOT_MAX];

static uint64_t now_ms(void)
{
    /* PIT 100Hz: 1 tick = 10ms */
    return (uint64_t)pit_get_ticks() * 10u;
}

static struct crash_slot *slot_by_pid(uint32_t pid)
{
    for (int i = 0; i < CRASH_SLOT_MAX; i++) {
        if (slots[i].used && slots[i].pid == pid) return &slots[i];
    }
    return NULL;
}

static struct crash_slot *slot_by_name(const char *name)
{
    if (!name || !*name) return NULL;
    for (int i = 0; i < CRASH_SLOT_MAX; i++) {
        if (slots[i].used &&
            strncmp(slots[i].name, name, CRASH_NAME_LEN) == 0) {
            return &slots[i];
        }
    }
    return NULL;
}

static void ctx_reset(struct crash_ctx *c, uint32_t pid, const char *name)
{
    memset(c, 0, sizeof(*c));
    c->magic = CRASH_CTX_MAGIC;
    c->pid   = pid;
    if (name) strncpy(c->msg, "", CRASH_MSG_LEN - 1);
    (void)name;
}

void crash_ctx_init(void)
{
    memset(slots, 0, sizeof(slots));
    vga_puts("[CRASHCTX] crash_ctx table init (max 32 slots)\n");
}

struct crash_ctx *crash_ctx_alloc(uint32_t pid, const char *name)
{
    struct crash_slot *s = slot_by_pid(pid);

    if (!s) s = slot_by_name(name);   /* 同名服务被重新拉起 → 复用历史槽位 */

    if (!s) {
        for (int i = 0; i < CRASH_SLOT_MAX; i++) {
            if (!slots[i].used) { s = &slots[i]; break; }
        }
    }

    if (!s) return NULL;

    int reused_history = s->used;
    s->used    = 1;
    s->pid     = pid;
    if (name) {
        strncpy(s->name, name, CRASH_NAME_LEN - 1);
        s->name[CRASH_NAME_LEN - 1] = 0;
    }
    ctx_reset(&s->ctx, pid, name);
    s->state       = CRASH_SVC_RUN;
    s->start_tick  = pit_get_ticks();
    s->uptime_ms   = 0;
    /* reused_history 时保留 last_crash 与 restarts 历史 */
    if (!reused_history) {
        s->restarts = 0;
        memset(&s->last, 0, sizeof(s->last));
    }
    return &s->ctx;
}

void crash_ctx_release(uint32_t pid)
{
    struct crash_slot *s = slot_by_pid(pid);
    if (!s) return;
    if (s->state == CRASH_SVC_RUN) s->state = CRASH_SVC_DEAD;
    s->uptime_ms = (uint64_t)(pit_get_ticks() - s->start_tick) * 10u;
    /* 槽位与 last_crash 记录保留，供结构化查询；不擦除、不 kfree。 */
}

struct crash_ctx *crash_ctx_by_pid(uint32_t pid)
{
    struct crash_slot *s = slot_by_pid(pid);
    return s ? &s->ctx : NULL;
}

void crash_ctx_record_syscall(struct crash_ctx *c, uint32_t num, uint32_t arg0)
{
    if (!c) return;
    c->last_syscall = (uint64_t)num;
    c->last_arg0     = (uint64_t)arg0;
}

void crash_ctx_record_ipc(struct crash_ctx *c, uint32_t tag)
{
    if (!c) return;
    c->last_ipc_tag = (uint64_t)tag;
}

void crash_ctx_fill_fault(struct crash_ctx *c, uint64_t pc, uint64_t sp,
                           const uint32_t *regs, int nregs, const char *msg)
{
    if (!c) return;
    c->pc = pc;
    c->sp = sp;
    for (int i = 0; i < 16; i++) {
        c->regs[i] = (uint64_t)(i < nregs && regs ? regs[i] : 0u);
    }
    if (msg) {
        strncpy(c->msg, msg, CRASH_MSG_LEN - 1);
        c->msg[CRASH_MSG_LEN - 1] = 0;
    }

    struct crash_slot *s = slot_by_pid(c->pid);
    if (!s) return;

    s->last.pc            = pc;
    s->last.last_syscall  = c->last_syscall;
    strncpy(s->last.msg, c->msg, CRASH_MSG_LEN - 1);
    s->last.msg[CRASH_MSG_LEN - 1] = 0;
    s->last.ts            = now_ms();
    s->state              = CRASH_SVC_DEAD;
    s->uptime_ms          = (uint64_t)(pit_get_ticks() - s->start_tick) * 10u;
    /* 原型阶段：supervisor 尚未自动拉起用户服务，每次崩溃事件计一次重启。 */
    s->restarts++;
}

void crash_ctx_bind_name(uint32_t pid, const char *name)
{
    struct crash_slot *s = slot_by_pid(pid);
    if (!s || !name) return;
    strncpy(s->name, name, CRASH_NAME_LEN - 1);
    s->name[CRASH_NAME_LEN - 1] = 0;
}

void crash_ctx_snapshot_for_restart(uint32_t pid)
{
    struct crash_slot *s = slot_by_pid(pid);
    if (!s) return;

    s->last.pc           = s->ctx.pc;
    s->last.last_syscall = s->ctx.last_syscall;
    strncpy(s->last.msg, s->ctx.msg, CRASH_MSG_LEN - 1);
    s->last.msg[CRASH_MSG_LEN - 1] = 0;
    s->last.ts           = now_ms();
    s->state             = CRASH_SVC_RESTARTING;
    s->restarts++;
}

int crash_query_by_name(const char *name, struct svc_query *out)
{
    struct crash_slot *s = slot_by_name(name);
    if (!s || !out) return -1;

    memset(out, 0, sizeof(*out));
    strncpy(out->name, s->name, CRASH_NAME_LEN - 1);
    out->state      = s->state;
    out->restarts   = s->restarts;
    out->uptime_ms = s->uptime_ms;
    memcpy(&out->last_crash, &s->last, sizeof(s->last));
    return 0;
}

int crash_query_by_pid(uint32_t pid, struct svc_query *out)
{
    struct crash_slot *s = slot_by_pid(pid);
    if (!s || !out) return -1;

    memset(out, 0, sizeof(*out));
    strncpy(out->name, s->name, CRASH_NAME_LEN - 1);
    out->state      = s->state;
    out->restarts   = s->restarts;
    out->uptime_ms  = s->uptime_ms;
    memcpy(&out->last_crash, &s->last, sizeof(s->last));
    return 0;
}

uint32_t sys_crash_query(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a3; (void)a4;
    extern int copy_from_user(void *dst, const void *src, size_t size);
    extern int copy_to_user(void *dst, const void *src, size_t size);

    if (a2 == 0) return (uint32_t)-1;

    char name[CRASH_NAME_LEN];
    name[0] = 0;
    uint32_t pid = 0;

    if (a1 != 0) {
        if (copy_from_user(name, (const void *)(uintptr_t)a1,
                           CRASH_NAME_LEN - 1) != 0) {
            return (uint32_t)-2;
        }
        name[CRASH_NAME_LEN - 1] = 0;
    } else {
        pid = sched_current_pid();
    }

    struct svc_query q;
    int rc = (name[0] != 0) ? crash_query_by_name(name, &q)
                            : crash_query_by_pid(pid, &q);
    if (rc < 0) return (uint32_t)-1;

    if (copy_to_user((void *)(uintptr_t)a2, &q, sizeof(q)) != 0) {
        return (uint32_t)-2;
    }
    return 0;
}

static const char *state_name(uint32_t st)
{
    switch (st) {
        case CRASH_SVC_RUN:        return "run";
        case CRASH_SVC_DEGRADED:  return "degraded";
        case CRASH_SVC_RESTARTING:return "restarting";
        case CRASH_SVC_DEAD:      return "dead";
        default:                  return "?";
    }
}

void crash_ctx_dump_query_by_pid(uint32_t pid)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    struct svc_query q;
    if (crash_query_by_pid(pid, &q) < 0) return;

    vga_printf("[CRASHCTX] query %s state=%s restarts=%u uptime_ms=%lu\n",
               q.name, state_name(q.state), q.restarts,
               (unsigned long)q.uptime_ms);
    vga_printf("[CRASHCTX]   last_crash pc=0x%x syscall=%u ts=%lu msg=%s\n",
               (uint32_t)q.last_crash.pc, (uint32_t)q.last_crash.last_syscall,
               (unsigned long)q.last_crash.ts, q.last_crash.msg);

    serial_puts(0x3F8, "[CRASHCTX] query ");
    serial_puts(0x3F8, q.name);
    serial_puts(0x3F8, " state=");
    serial_puts(0x3F8, state_name(q.state));
    serial_puts(0x3F8, " restarts=");
    serial_puthex(0x3F8, q.restarts);
    serial_puts(0x3F8, "\n");
    serial_puts(0x3F8, "[CRASHCTX]   pc=0x");
    serial_puthex(0x3F8, (uint32_t)q.last_crash.pc);
    serial_puts(0x3F8, " syscall=");
    serial_puthex(0x3F8, (uint32_t)q.last_crash.last_syscall);
    serial_puts(0x3F8, " ts=");
    serial_puthex(0x3F8, (uint32_t)q.last_crash.ts);
    serial_puts(0x3F8, " msg=");
    serial_puts(0x3F8, q.last_crash.msg);
    serial_puts(0x3F8, "\n");
}

void crash_ctx_dump_all(void)
{    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);

    for (int i = 0; i < CRASH_SLOT_MAX; i++) {
        if (!slots[i].used) continue;
        const struct crash_slot *s = &slots[i];
        vga_printf("[CRASHCTX] %s pid=%u state=%s restarts=%u uptime_ms=%lu\n",
                   s->name, s->pid, state_name(s->state),
                   s->restarts, (unsigned long)s->uptime_ms);
        vga_printf("[CRASHCTX]   last_crash pc=0x%x syscall=%lu ts=%lu msg=%s\n",
                   (uint32_t)s->last.pc,
                   (unsigned long)s->last.last_syscall,
                   (unsigned long)s->last.ts, s->last.msg);

        serial_puts(0x3F8, "[CRASHCTX] ");
        serial_puts(0x3F8, s->name);
        serial_puts(0x3F8, " pid=");
        serial_puthex(0x3F8, s->pid);
        serial_puts(0x3F8, " state=");
        serial_puts(0x3F8, state_name(s->state));
        serial_puts(0x3F8, " restarts=");
        serial_puthex(0x3F8, s->restarts);
        serial_puts(0x3F8, "\n");
        serial_puts(0x3F8, "[CRASHCTX]   pc=0x");
        serial_puthex(0x3F8, (uint32_t)s->last.pc);
        serial_puts(0x3F8, " syscall=");
        serial_puthex(0x3F8, (uint32_t)s->last.last_syscall);
        serial_puts(0x3F8, " ts=");
        serial_puthex(0x3F8, (uint32_t)s->last.ts);
        serial_puts(0x3F8, " msg=");
        serial_puts(0x3F8, s->last.msg);
        serial_puts(0x3F8, "\n");
    }
}
