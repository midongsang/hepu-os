/* ============================================================================
 * 赫普OS 64位 - seccomp 沙箱（P0-4/P0-5）
 * 设计：
 *   - 每个用户进程槽位（MAX_USER_PROCS=8）加一个内核上下文槽，共 9 个槽位；
 *   - 每槽位一套模式（disabled / strict / filter）与 512 位白名单；
 *   - strict 模式与 Linux SECCOMP_SET_MODE_STRICT 对齐：仅放行
 *     read/write/exit/exit_group/rt_sigreturn，另放行 seccomp 自身便于查询；
 *   - filter 模式保存用户提供的 cBPF 程序（≤512 条指令），在系统调用分发处
 *     按 seccomp_data 求值，动作支持 ALLOW / LOG（放行）、ERRNO（返回 errno）、
 *     KILL_THREAD / KILL_PROCESS（终止进程）；
 *   - 强制点在系统调用分发处（syscall64_full.c），命中拒绝返回 -EPERM 等；
 *   - 槽位复用时由 user_process_release() 调用 seccomp64_reset() 清除，
 *     避免新进程继承上一个进程的沙箱模式；
 *   - 为防死循环，只接受**向前跳转**的 cBPF 程序（与 Linux classic BPF 校验一致），
 *     跳转目标越界、最后一条不是 RET、未知 opcode 均在安装时拒绝。
 * 已知限制：无 TSYNC（flags 非 0 一律 EINVAL）、无 no_new_privs 跟踪（以 euid==0
 * 作为安装条件）、不提供用户态可见的 instruction_pointer（恒为 0）、
 * TRAP/TRACE/USER_NOTIF 未实现（明确拒绝，不静默放行）。
 * ========================================================================== */

#include <stdint.h>
#include "seccomp64.h"

extern int user_process_get_current_slot(void);

#define SECCOMP_SLOTS 9   /* 8 个进程槽 + 1 个内核上下文槽 */

typedef struct {
    int mode;
    uint64_t allow[SECCOMP_ALLOW_WORDS];
    unsigned int filter_len;
    seccomp_insn_t filter[SECCOMP_FILTER_MAX_INSNS];
} seccomp_slot_t;

static seccomp_slot_t g_slots[SECCOMP_SLOTS];

/* classic BPF 编码位（与 Linux <linux/filter.h> 一致） */
#define BPF_CLASS(c) ((c) & 0x07)
#define BPF_LD    0x00
#define BPF_LDX   0x01
#define BPF_ST    0x02
#define BPF_STX   0x03
#define BPF_ALU   0x04
#define BPF_JMP   0x05
#define BPF_RET   0x06
#define BPF_MISC  0x07

#define BPF_SZ(c)   ((c) & 0x18)
#define BPF_W       0x00
#define BPF_H       0x08
#define BPF_B       0x10

#define BPF_MODE(c) ((c) & 0xe0)
#define BPF_IMM     0x00
#define BPF_ABS     0x20
#define BPF_MEM     0x60
#define BPF_LEN     0x80
#define BPF_MSH     0xa0

#define BPF_OP(c)   ((c) & 0xf0)
#define BPF_ADD     0x00
#define BPF_SUB     0x10
#define BPF_MUL     0x20
#define BPF_DIV     0x30
#define BPF_OR      0x40
#define BPF_AND     0x50
#define BPF_LSH     0x60
#define BPF_RSH     0x70
#define BPF_NEG     0x80
#define BPF_MOD     0x90
#define BPF_XOR     0xa0

#define BPF_JA      0x00
#define BPF_JEQ     0x10
#define BPF_JGT     0x20
#define BPF_JGE     0x30
#define BPF_JSET    0x40

#define BPF_SRC(c)  ((c) & 0x08)
#define BPF_K       0x00
#define BPF_X       0x08

#define BPF_A       0x10
#define BPF_TAX     0x00
#define BPF_TXA     0x80

#define SECCOMP_SCRATCH_WORDS 16

static int normalize_slot(int slot)
{
    if (slot >= 0 && slot < SECCOMP_SLOTS - 1) return slot;
    return SECCOMP_SLOTS - 1;
}

static void allow_set(seccomp_slot_t *s, int nr)
{
    if (nr < 0 || nr >= SECCOMP_MAX_NR) return;
    s->allow[nr / 64] |= (1ULL << (nr % 64));
}

static void allow_clear(seccomp_slot_t *s, int nr)
{
    if (nr < 0 || nr >= SECCOMP_MAX_NR) return;
    s->allow[nr / 64] &= ~(1ULL << (nr % 64));
}

static int allow_test(const seccomp_slot_t *s, int nr)
{
    if (nr < 0 || nr >= SECCOMP_MAX_NR) return 0;
    return (int)((s->allow[nr / 64] >> (nr % 64)) & 1ULL);
}

static void slot_clear(seccomp_slot_t *s)
{
    s->mode = SECCOMP_MODE_DISABLED;
    for (int i = 0; i < SECCOMP_ALLOW_WORDS; i++) s->allow[i] = 0;
    s->filter_len = 0;
}

void seccomp64_init(void)
{
    for (int i = 0; i < SECCOMP_SLOTS; i++) slot_clear(&g_slots[i]);
}

void seccomp64_allow(int slot, int nr)
{
    allow_set(&g_slots[normalize_slot(slot)], nr);
}

void seccomp64_deny(int slot, int nr)
{
    allow_clear(&g_slots[normalize_slot(slot)], nr);
}

void seccomp64_enable_strict(int slot)
{
    seccomp_slot_t *s = &g_slots[normalize_slot(slot)];
    slot_clear(s);
    s->mode = SECCOMP_MODE_STRICT;

    /* Linux strict 允许集合：read/write/_exit/sigreturn。
     * 同时兼容赫普原生号（read=2, close=4, exit=6, rt_sigreturn=109）。 */
    allow_set(s, 0);    /* Linux read */
    allow_set(s, 1);    /* write（原生与 Linux 共用） */
    allow_set(s, 2);    /* 原生 read */
    allow_set(s, 4);    /* 原生 close */
    allow_set(s, 6);    /* 原生 exit */
    allow_set(s, 15);   /* Linux rt_sigreturn */
    allow_set(s, 60);   /* Linux exit */
    allow_set(s, 109);  /* 原生 rt_sigreturn */
    allow_set(s, 231);  /* Linux exit_group */
    allow_set(s, 317);  /* seccomp：允许查询动作可用性 */
}

/* ---------------------------------------------------------------------------
 * 过滤器安装校验
 *
 * 逐条检查 opcode 是否为本解释器支持的形式，并强制跳转只向前、目标在程序内。
 * 这样非法程序在安装阶段就返回 EINVAL，而不是等到某次系统调用才炸掉进程。
 * ------------------------------------------------------------------------- */
static int filter_insn_valid(const seccomp_insn_t *in, unsigned int pc, unsigned int len)
{
    unsigned int cls  = BPF_CLASS(in->code);
    unsigned int sz   = BPF_SZ(in->code);
    unsigned int mode = BPF_MODE(in->code);
    unsigned int op   = BPF_OP(in->code);
    unsigned int src  = BPF_SRC(in->code);

    switch (cls) {
    case BPF_LD:
        if (in->code == (BPF_LD | BPF_W | BPF_IMM)) return 1;   /* A = k */
        if (in->code == (BPF_LD | BPF_W | BPF_LEN)) return 1;   /* A = 指令数 */
        if (mode == BPF_ABS) {
            if (sz == BPF_W) return in->k <= 60 && (in->k & 3u) == 0;
            if (sz == BPF_H) return in->k <= 62 && (in->k & 1u) == 0;
            if (sz == BPF_B) return in->k <= 63;
            return 0;
        }
        if (mode == BPF_MEM && sz == BPF_W) return in->k < SECCOMP_SCRATCH_WORDS;
        return 0;

    case BPF_LDX:
        if (in->code == (BPF_LDX | BPF_W | BPF_IMM)) return 1;  /* X = k */
        if (in->code == (BPF_LDX | BPF_W | BPF_LEN)) return 1;  /* X = 指令数 */
        if (in->code == (BPF_LDX | BPF_B | BPF_MSH)) return in->k <= 63;
        if (mode == BPF_MEM && sz == BPF_W) return in->k < SECCOMP_SCRATCH_WORDS;
        return 0;

    case BPF_ST:
    case BPF_STX:
        /* classic BPF：ST=0x02 / STX=0x03，k 为暂存槽下标 */
        return in->code == cls && in->k < SECCOMP_SCRATCH_WORDS;

    case BPF_ALU:
        if (in->code == (BPF_ALU | BPF_NEG)) return 1;
        switch (op) {
        case BPF_ADD: case BPF_SUB: case BPF_MUL: case BPF_DIV:
        case BPF_OR:  case BPF_AND: case BPF_LSH: case BPF_RSH:
        case BPF_MOD: case BPF_XOR:
            break;
        default:
            return 0;
        }
        /* X 形式要求 k 为 0（classic BPF 约定） */
        if (src == BPF_X && in->k != 0) return 0;
        return 1;

    case BPF_JMP:
        if (in->code == (BPF_JMP | BPF_JA)) {
            /* 只允许向前跳，且落点必须是程序内指令 */
            return in->jt == 0 && in->jf == 0 && (pc + 1 + in->k) < len;
        }
        switch (op) {
        case BPF_JEQ: case BPF_JGT: case BPF_JGE: case BPF_JSET:
            break;
        default:
            return 0;
        }
        if (src == BPF_X && in->k != 0) return 0;
        return (pc + 1 + in->jt) < len && (pc + 1 + in->jf) < len;

    case BPF_RET:
        return in->code == (BPF_RET | BPF_K) || in->code == (BPF_RET | BPF_A);

    case BPF_MISC:
        return in->code == (BPF_MISC | BPF_TAX) || in->code == (BPF_MISC | BPF_TXA);

    default:
        return 0;
    }
}

static int filter_validate(const seccomp_insn_t *p, unsigned int len)
{
    if (!p || len == 0 || len > SECCOMP_FILTER_MAX_INSNS) return 0;
    /* 最后一条必须是 RET（与 Linux 一致），否则程序可能执行到末尾无结果 */
    if (BPF_CLASS(p[len - 1].code) != BPF_RET) return 0;
    for (unsigned int i = 0; i < len; i++) {
        if (!filter_insn_valid(&p[i], i, len)) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------------------
 * cBPF 解释器：对 seccomp_data 求值，成功返回 0 并写出 A 寄存器结果。
 * 返回 -1 表示程序执行异常（越界/未知指令/无 RET），调用方按 KILL 处理。
 * ------------------------------------------------------------------------- */
static uint32_t load_u32(const void *p)
{
    uint32_t v;
    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}

static uint16_t load_u16(const void *p)
{
    uint16_t v;
    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}

static uint8_t load_u8(const void *p)
{
    return *(const volatile uint8_t *)p;
}

static int filter_run(const seccomp_insn_t *p, unsigned int len,
                      const struct seccomp_data *d, uint32_t *result)
{
    uint32_t A = 0, X = 0;
    uint32_t M[SECCOMP_SCRATCH_WORDS];
    const uint8_t *db = (const uint8_t *)d;
    unsigned int pc = 0;

    for (int i = 0; i < SECCOMP_SCRATCH_WORDS; i++) M[i] = 0;

    /* 安装时已保证只向前跳转、落点在程序内，循环最多执行 len 次；
     * 这里再加一道计数保险，任何异常都按拒绝处理。 */
    for (unsigned int steps = 0; steps <= len; steps++) {
        if (pc >= len) return -1;

        const seccomp_insn_t *in = &p[pc];
        unsigned int cls  = BPF_CLASS(in->code);
        unsigned int sz   = BPF_SZ(in->code);
        unsigned int mode = BPF_MODE(in->code);
        unsigned int op   = BPF_OP(in->code);

        switch (cls) {
        case BPF_LD:
            if (in->code == (BPF_LD | BPF_W | BPF_IMM)) {
                A = in->k;
            } else if (in->code == (BPF_LD | BPF_W | BPF_LEN)) {
                A = len;
            } else if (mode == BPF_ABS) {
                if (sz == BPF_W) A = load_u32(db + in->k);
                else if (sz == BPF_H) A = load_u16(db + in->k);
                else A = load_u8(db + in->k);
            } else if (mode == BPF_MEM && sz == BPF_W) {
                A = M[in->k];
            } else {
                return -1;
            }
            pc++;
            break;

        case BPF_LDX:
            if (in->code == (BPF_LDX | BPF_W | BPF_IMM)) {
                X = in->k;
            } else if (in->code == (BPF_LDX | BPF_W | BPF_LEN)) {
                X = len;
            } else if (in->code == (BPF_LDX | BPF_B | BPF_MSH)) {
                X = ((uint32_t)load_u8(db + in->k) & 0x0fu) << 2;
            } else if (mode == BPF_MEM && sz == BPF_W) {
                X = M[in->k];
            } else {
                return -1;
            }
            pc++;
            break;

        case BPF_ST:
            M[in->k] = A;
            pc++;
            break;

        case BPF_STX:
            M[in->k] = X;
            pc++;
            break;

        case BPF_ALU: {
            uint32_t v = (in->code & BPF_X) ? X : in->k;
            switch (op) {
            case BPF_ADD: A += v; break;
            case BPF_SUB: A -= v; break;
            case BPF_MUL: A *= v; break;
            case BPF_DIV:
                if (v == 0) { *result = 0; return 0; }  /* 除零 → RET 0（KILL），不崩溃 */
                A /= v;
                break;
            case BPF_MOD:
                if (v == 0) { *result = 0; return 0; }
                A %= v;
                break;
            case BPF_OR:  A |= v; break;
            case BPF_AND: A &= v; break;
            case BPF_LSH: A <<= (v & 31u); break;
            case BPF_RSH: A >>= (v & 31u); break;
            case BPF_NEG: A = (uint32_t)(-(int32_t)A); break;
            case BPF_XOR: A ^= v; break;
            default: return -1;
            }
            pc++;
            break;
        }

        case BPF_JMP: {
            uint32_t v;
            int cond;
            if (in->code == (BPF_JMP | BPF_JA)) {
                pc += 1 + in->k;
                break;
            }
            v = (in->code & BPF_X) ? X : in->k;
            switch (op) {
            case BPF_JEQ:  cond = (A == v); break;
            case BPF_JGT:  cond = (A > v); break;
            case BPF_JGE:  cond = (A >= v); break;
            case BPF_JSET: cond = ((A & v) != 0); break;
            default: return -1;
            }
            pc += 1 + (cond ? in->jt : in->jf);
            break;
        }

        case BPF_RET:
            *result = (in->code == (BPF_RET | BPF_A)) ? A : in->k;
            return 0;

        case BPF_MISC:
            if (in->code == (BPF_MISC | BPF_TAX)) {
                X = A;
            } else if (in->code == (BPF_MISC | BPF_TXA)) {
                A = X;
            } else {
                return -1;
            }
            pc++;
            break;

        default:
            return -1;
        }
    }
    return -1;
}

int seccomp64_set_filter(int slot, const seccomp_insn_t *insns, unsigned int len)
{
    if (!filter_validate(insns, len)) return -1;
    seccomp_slot_t *s = &g_slots[normalize_slot(slot)];
    if (s->mode != SECCOMP_MODE_DISABLED) return -1;
    for (unsigned int i = 0; i < len; i++) s->filter[i] = insns[i];
    s->filter_len = len;
    s->mode = SECCOMP_MODE_FILTER;
    return 0;
}

int seccomp64_get_mode(int slot)
{
    return g_slots[normalize_slot(slot)].mode;
}

void seccomp64_reset(int slot)
{
    slot_clear(&g_slots[normalize_slot(slot)]);
}

int seccomp64_check(int slot, int nr)
{
    const seccomp_slot_t *s = &g_slots[normalize_slot(slot)];
    if (s->mode != SECCOMP_MODE_STRICT) return 0;
    return allow_test(s, nr) ? 0 : -1;
}

int seccomp64_check_current(int nr)
{
    return seccomp64_check(user_process_get_current_slot(), nr);
}

/* 终止当前用户进程；不在用户态时该函数返回 -1，忽略即可。 */
static void seccomp_kill_current(void)
{
    extern int user_process_kill_current(void);
    user_process_kill_current();
}

int seccomp64_guard_current(int nr, uint64_t ip, const uint64_t args[6], int64_t *ret)
{
    const seccomp_slot_t *s = &g_slots[normalize_slot(user_process_get_current_slot())];

    if (s->mode == SECCOMP_MODE_DISABLED) return 0;

    if (s->mode == SECCOMP_MODE_STRICT) {
        if (allow_test(s, nr)) return 0;
        if (ret) *ret = -1;   /* -EPERM */
        return 1;
    }

    if (s->mode != SECCOMP_MODE_FILTER || s->filter_len == 0) return 0;

    struct seccomp_data d;
    d.nr = nr;
    d.arch = AUDIT_ARCH_X86_64;
    d.instruction_pointer = ip;
    for (int i = 0; i < 6; i++) d.args[i] = args ? args[i] : 0;

    uint32_t res = 0;
    if (filter_run(s->filter, s->filter_len, &d, &res) != 0) {
        /* 程序执行异常：按最严格动作终止进程，绝不放行 */
        seccomp_kill_current();
        if (ret) *ret = -1;
        return 1;
    }

    uint32_t action = res & SECCOMP_RET_ACTION_FULL;
    uint32_t data   = res & SECCOMP_RET_DATA;

    switch (action) {
    case SECCOMP_RET_ALLOW:
    case SECCOMP_RET_LOG:      /* 无审计后端：按放行处理，语义与 Linux RET_LOG 一致 */
        return 0;

    case SECCOMP_RET_ERRNO:
        if (ret) *ret = -(int64_t)data;   /* data=0 时用户看到成功返回但调用未执行 */
        return 1;

    case SECCOMP_RET_KILL_PROCESS:
    case SECCOMP_RET_KILL_THREAD:
        seccomp_kill_current();
        if (ret) *ret = -1;   /* -EPERM */
        return 1;

    default:
        /* TRAP / TRACE / USER_NOTIF 尚未实现：明确拒绝，不静默放行 */
        seccomp_kill_current();
        if (ret) *ret = -1;
        return 1;
    }
}
