/* ============================================================================
 * 赫普OS - 系统调用：HEPU
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "vga.h"
#include "kheap.h"
#include "sched.h"
#include "hepu.h"
#include "fingerprint.h"
#include "vfs.h"
#include "ipc.h"
extern const char *hepu_kernel_version;

u32 sys_hepu_info(u32 arg1, u32 arg2,
                              u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    /* V7.66安全修复: 不返回内核指针，防止内核地址泄露
     * 返回版本号编码: 主版本<<24 | 次版本<<16 | 修订版本<<8
     * "1.0.2" -> 0x01000200 */
    extern const char *hepu_kernel_version;
    u32 major = 0, minor = 0, patch = 0;
    const char *p = hepu_kernel_version;
    if (p) {
        while (*p >= '0' && *p <= '9') { major = major * 10 + (*p - '0'); p++; }
        if (*p == '.') p++;
        while (*p >= '0' && *p <= '9') { minor = minor * 10 + (*p - '0'); p++; }
        if (*p == '.') p++;
        while (*p >= '0' && *p <= '9') { patch = patch * 10 + (*p - '0'); p++; }
    }
    return (major << 24) | (minor << 16) | (patch << 8);
}

u32 sys_hepu_fp_verify(u32 arg1, u32 arg2,
                                   u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    extern hepu_fingerprint_t hepu_fingerprint;
    fingerprint_result_t r = fingerprint_verify(&hepu_fingerprint);
    return r.valid ? 0 : 1;
}

u32 sys_print(u32 str_ptr, u32 arg2,
                          u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (str_ptr && !user_ptr_valid((const char *)str_ptr, 1)) {
        vga_printf("[HEPU] sys_print: invalid str ptr 0x%x\n", str_ptr);
        return (u32)-1;
    }
    const char *s = (const char *)str_ptr;
    if (s) {
        vga_puts(s);
        /* 同时输出到串口, 方便通过串口日志验证用户态程序运行 */
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, s);
    }
    return 0;
}

