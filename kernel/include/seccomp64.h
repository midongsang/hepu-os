/* ============================================================================
 * 赫普OS 64位 - seccomp 沙箱（P0-4/P0-5）
 * 每进程槽位维护一套白名单；strict 模式仅放行最小系统调用集合；
 * filter 模式执行用户提供的 cBPF 程序（SECCOMP_SET_MODE_FILTER）。
 * ========================================================================== */

#ifndef SECCOMP64_H
#define SECCOMP64_H

#include <stdint.h>

#define SECCOMP_MODE_DISABLED 0
#define SECCOMP_MODE_STRICT   1
#define SECCOMP_MODE_FILTER   2

/* 系统调用号上限（覆盖 Linux seccomp=317 等号段） */
#define SECCOMP_MAX_NR 512
#define SECCOMP_ALLOW_WORDS (SECCOMP_MAX_NR / 64)

/* Linux seccomp(2) op 取值 */
#define SECCOMP_SET_MODE_STRICT   0
#define SECCOMP_SET_MODE_FILTER   1
#define SECCOMP_GET_ACTION_AVAIL  2

/* Linux seccomp(2) filter flags（本实现只接受 0，其余明确返回 EINVAL） */
#define SECCOMP_FILTER_FLAG_TSYNC         (1u << 0)
#define SECCOMP_FILTER_FLAG_LOG           (1u << 1)
#define SECCOMP_FILTER_FLAG_SPEC_ALLOW    (1u << 2)
#define SECCOMP_FILTER_FLAG_NEW_LISTENER  (1u << 3)

/* seccomp 返回动作（高 16 位为动作，低 16 位为数据） */
#define SECCOMP_RET_KILL_THREAD  0x00000000u
#define SECCOMP_RET_KILL_PROCESS 0x80000000u
#define SECCOMP_RET_TRAP         0x00030000u
#define SECCOMP_RET_ERRNO        0x00050000u
#define SECCOMP_RET_USER_NOTIF   0x7FC00000u
#define SECCOMP_RET_TRACE        0x7FF00000u
#define SECCOMP_RET_LOG          0x7FFC0000u
#define SECCOMP_RET_ALLOW        0x7FFF0000u
#define SECCOMP_RET_ACTION_FULL  0xFFFF0000u
#define SECCOMP_RET_DATA         0x0000FFFFu

/* seccomp_data.arch：x86_64 审计架构号 */
#define AUDIT_ARCH_X86_64 0xC000003Eu

/* 单进程过滤器指令数上限；每槽位静态存储 512 × 8B = 4KB */
#define SECCOMP_FILTER_MAX_INSNS 512

/* cBPF 指令：与 Linux struct sock_filter 布局一致（8 字节） */
typedef struct {
    uint16_t code;
    uint8_t  jt;
    uint8_t  jf;
    uint32_t k;
} seccomp_insn_t;

/* Linux struct sock_fprog：len 之后 4 字节填充，指针位于偏移 8（共 16 字节） */
typedef struct {
    uint16_t len;
    uint16_t pad0;
    uint32_t pad1;
    uint64_t filter;
} seccomp_fprog_t;

/* 过滤器视角的系统调用数据（64 字节，字段布局与 Linux 一致） */
struct seccomp_data {
    int32_t  nr;
    uint32_t arch;
    uint64_t instruction_pointer;
    uint64_t args[6];
};

/* 初始化（清零全部槽位） */
void seccomp64_init(void);

/* 启用 strict 模式（仅放行 read/write/exit/exit_group/rt_sigreturn/seccomp） */
void seccomp64_enable_strict(int slot);

/* 安装 cBPF 过滤器；成功返回 0，程序非法返回 -1（调用方转 -EINVAL） */
int seccomp64_set_filter(int slot, const seccomp_insn_t *insns, unsigned int len);

/* 当前模式 */
int seccomp64_get_mode(int slot);

/* 白名单增删（slot 越界时归入内核上下文槽） */
void seccomp64_allow(int slot, int nr);
void seccomp64_deny(int slot, int nr);

/* 判定：0=允许，-1=拒绝 */
int seccomp64_check(int slot, int nr);
int seccomp64_check_current(int nr);

/* 系统调用分发强制点：
 *   返回 0 = 放行（继续执行原调用）；
 *   返回 1 = 拦截，*ret 为应返回给用户态的值（负 errno；kill 时 -EPERM）。
 * strict 与 filter 两种模式在此统一处理。 */
int seccomp64_guard_current(int nr, uint64_t ip, const uint64_t args[6], int64_t *ret);

/* 进程槽位释放时重置 */
void seccomp64_reset(int slot);

#endif /* SECCOMP64_H */
