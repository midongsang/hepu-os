/* ============================================================================
 * 赫普OS - 64位系统调用支持（V8.80新增，P5-1）
 * SYSCALL/SYSRET、MSR配置、64位系统调用表
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "x86_64.h"
#include "../../include/vga.h"
#include "../../include/kheap.h"

/* MSR地址 */
#define MSR_STAR        0xC0000081   /* 系统调用段选择子 */
#define MSR_LSTAR       0xC0000082   /* 64位系统调用入口 */
#define MSR_CSTAR       0xC0000083   /* 兼容模式系统调用入口 */
#define MSR_SFMASK      0xC0000084   /* 系统调用标志掩码 */

/* 段选择子 */
#define KERNEL_CODE64   0x08
#define KERNEL_DATA     0x10
#define USER_CODE64     0x18
#define USER_DATA       0x20

/* 系统调用表 */
#define MAX_SYSCALLS64  256

typedef uint64_t (*syscall64_handler_t)(uint64_t, uint64_t, uint64_t,
                                        uint64_t, uint64_t, uint64_t);

static syscall64_handler_t syscall_table64[MAX_SYSCALLS64];
static int syscall64_initialized = 0;

/* 系统调用统计 */
static struct {
    uint64_t total_calls;
    uint64_t per_syscall[MAX_SYSCALLS64];
    uint64_t last_error;
} syscall64_stats;

/* ============================================================================
 * 系统调用入口（汇编实现）
 * 保存寄存器、调用C处理函数、恢复寄存器、SYSRET返回
 * ========================================================================== */

/* 系统调用处理函数 */
static uint64_t syscall64_dispatch(uint64_t nr, uint64_t arg1, uint64_t arg2,
                                   uint64_t arg3, uint64_t arg4,
                                   uint64_t arg5, uint64_t arg6)
{
    if (nr >= MAX_SYSCALLS64) {
        syscall64_stats.last_error = -38;  /* ENOSYS */
        return -38;
    }

    if (!syscall_table64[nr]) {
        syscall64_stats.last_error = -38;
        return -38;
    }

    syscall64_stats.total_calls++;
    syscall64_stats.per_syscall[nr]++;

    return syscall_table64[nr](arg1, arg2, arg3, arg4, arg5, arg6);
}

/* ============================================================================
 * MSR配置
 * ========================================================================== */

/* 配置STAR MSR（段选择子） */
static void syscall64_configure_star(void)
{
    /* STAR格式：
     * [63:48] - SYSRET的CS/SS（用户模式）
     * [47:32] - SYSCALL的CS/SS（内核模式）
     * [31:0]  - 保留（在AMD64中）
     *
     * SYSRET: CS = STAR[63:48] + 16, SS = STAR[63:48] + 8
     * SYSCALL: CS = STAR[47:32], SS = STAR[47:32] + 8
     */
    uint64_t star = 0;
    star |= (uint64_t)(USER_CODE64 - 16) << 48;  /* SYSRET CS base */
    star |= (uint64_t)KERNEL_CODE64 << 32;        /* SYSCALL CS */
    x86_64_write_msr(MSR_STAR, star);
    vga_puts("[SYSCALL64] STAR MSR configured\n");
}

/* 配置LSTAR MSR（64位系统调用入口） */
static void syscall64_configure_lstar(uint64_t entry)
{
    x86_64_write_msr(MSR_LSTAR, entry);
    vga_printf("[SYSCALL64] LSTAR set to 0x%llx\n",
               (unsigned long long)entry);
}

/* 配置CSTAR MSR（兼容模式入口） */
static void syscall64_configure_cstar(uint64_t entry)
{
    x86_64_write_msr(MSR_CSTAR, entry);
    vga_printf("[SYSCALL64] CSTAR set to 0x%llx\n",
               (unsigned long long)entry);
}

/* 配置SFMASK MSR（RFLAGS掩码） */
static void syscall64_configure_sfmask(void)
{
    /* 清除IF（中断标志），其他保持 */
    uint64_t mask = (1ULL << 9);  /* IF bit */
    x86_64_write_msr(MSR_SFMASK, mask);
    vga_puts("[SYSCALL64] SFMASK configured (IF cleared on syscall)\n");
}

/* ============================================================================
 * 系统调用入口桩函数
 * 实际实现需要汇编，这里提供C级别的框架
 * ========================================================================== */

/* 系统调用入口（由汇编调用） */
void syscall64_entry(void)
{
    /* 这个函数应该由汇编代码调用
     * 汇编代码负责：
     * 1. 保存用户寄存器
     * 2. 从rdi/rsi/rdx/r10/r8/r9提取参数
     * 3. 调用syscall64_dispatch
     * 4. 恢复寄存器
     * 5. SYSRET返回
     */
    vga_puts("[SYSCALL64] Entry called (should be in assembly)\n");
}

/* ============================================================================
 * 系统调用注册
 * ========================================================================== */

int syscall64_register(uint32_t nr, syscall64_handler_t handler)
{
    if (nr >= MAX_SYSCALLS64 || !handler) return -1;
    syscall_table64[nr] = handler;
    return 0;
}

int syscall64_unregister(uint32_t nr)
{
    if (nr >= MAX_SYSCALLS64) return -1;
    syscall_table64[nr] = NULL;
    return 0;
}

/* ============================================================================
 * 示例系统调用
 * ========================================================================== */

/* sys_write64 - 写入文件描述符 */
static uint64_t sys_write64(uint64_t fd, uint64_t buf, uint64_t count,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    /* 简化：输出到串口 */
    if (fd == 1 || fd == 2) {  /* stdout/stderr */
        char *str = (char *)buf;
        for (uint64_t i = 0; i < count && i < 1024; i++) {
            vga_putchar(str[i]);
        }
        return count;
    }
    return -9;  /* EBADF */
}

/* sys_getpid64 - 获取进程ID */
static uint64_t sys_getpid64(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    return 1;  /* init进程 */
}

/* sys_hepu_info64 - 系统信息（安全修复：使用copy_to_user） */
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t size);
static uint64_t sys_hepu_info64(uint64_t buf, uint64_t size, uint64_t arg3,
                                uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    const char *info = "Hepu OS x86_64 V8.80\n";
    uint64_t len = strlen(info);
    if (size < len) len = size;
    /* 安全修复：使用copy_to_user校验用户地址，避免任意地址写 */
    if (copy_to_user((void *)buf, info, len) != 0) {
        return -14;  /* EFAULT */
    }
    return len;
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

int syscall64_init(void)
{
    if (syscall64_initialized) return 0;

    vga_puts("[SYSCALL64] Initializing 64-bit syscall support...\n");

    /* 清空系统调用表 */
    memset(syscall_table64, 0, sizeof(syscall_table64));
    memset(&syscall64_stats, 0, sizeof(syscall64_stats));

    /* 注册示例系统调用 */
    syscall64_register(1, sys_write64);       /* write */
    syscall64_register(39, sys_getpid64);     /* getpid */
    syscall64_register(200, sys_hepu_info64); /* hepu_info */

    /* 配置MSR */
    syscall64_configure_star();
    syscall64_configure_lstar((uint64_t)syscall64_entry);
    syscall64_configure_cstar((uint64_t)syscall64_entry);
    syscall64_configure_sfmask();

    /* 启用SCE（系统调用扩展） */
    x86_64_enable_sce();

    syscall64_initialized = 1;
    vga_puts("[SYSCALL64] 64-bit syscall support initialized\n");
    vga_printf("[SYSCALL64] Registered %d syscalls (max %d)\n",
               3, MAX_SYSCALLS64);

    return 0;
}

/* ============================================================================
 * 统计信息
 * ========================================================================== */

void syscall64_get_stats(uint64_t *total, uint64_t *per_syscall,
                         uint64_t *last_error)
{
    if (total) *total = syscall64_stats.total_calls;
    if (per_syscall) {
        memcpy(per_syscall, syscall64_stats.per_syscall,
               sizeof(uint64_t) * MAX_SYSCALLS64);
    }
    if (last_error) *last_error = syscall64_stats.last_error;
}

int syscall64_is_initialized(void)
{
    return syscall64_initialized;
}
