/* ============================================================================
 * 赫普OS 64位 - 用户程序加载器（P3-6 §5-3 第一阶段拆分）
 *
 * 背景：历史上「从 ext2 读 ELF → 校验 → ABI 判定 → PT_INTERP 处理 →
 * 偏置加载」整段揉在 syscall 路径的 uproc_exec_impl() 里。本模块把这段
 * 「纯加载」逻辑摘出来，对外只暴露一个窄接口；syscall 路径只保留
 * 「建槽 / 切态 / pid 回传 / 错误映射」。
 *
 * 分工边界（第一阶段）：
 *   loader 负责：文件读取、ELF 校验、ABI 判定、PIE/解释器 VA 预留、
 *                主程序与解释器的偏置加载，产出一份加载结果结构。
 *   进程模块（user_process.c）负责：建槽、用户页表、初始栈/argv/envp/auxv、
 *                TLS 预留、brk 初始页、IRELATIVE 重定位、解释器镜像映射进
 *                进程地址空间、VA 账本移交、切用户态、pid 回传。
 *
 * 本阶段刻意不动：binfmt 派发（services/compat）、PE/Mach-O 路径、
 * uproc32、初始栈构造（仍在 user_process_create_elf 内，属第二阶段）。
 * ========================================================================== */

#ifndef HEPU_UPROC_LOADER64_H
#define HEPU_UPROC_LOADER64_H

#include <stdint.h>
#include "elf64.h"   /* ENV_NATIVE / ENV_LINUX_COMPAT */

/* 用户地址空间 VA 区间划分（与 user_process.c 的 VA 账本共用同一套边界）。
 *   [0x50000000,0x60000000)  兼容程序区：Linux PIE 按加载偏置摆进来
 *   [0x60000000,0x70000000)  共享库区：PT_INTERP 解释器 / DT_NEEDED .so
 */
#define VA_COMPAT_BASE 0x50000000ULL
#define VA_COMPAT_END  0x60000000ULL
#define VA_LIB_BASE    0x60000000ULL
#define VA_LIB_END     0x70000000ULL

/* loader 产出的加载结果。成功后所有权归 syscall 路径：
 *   - loaded / interp_loaded / elf_buffer：内核堆缓冲，用完由进程模块 kfree；
 *   - map_base / interp_code_vaddr：exec 前的 PENDING VA 预留，建槽成功后
 *     由进程模块移交（adopt），建槽失败由进程模块释放。 */
typedef struct {
    void     *loaded;            /* 主程序偏置加载缓冲（elf64_load_biased 返回） */
    uint64_t  code_vaddr;        /* 主程序镜像首字节对应的虚拟地址 */
    uint64_t  entry_vaddr;       /* CPU 实际入口（带解释器时 = 解释器入口） */
    uint64_t  exec_code_size;    /* 主程序映像大小（≠ 文件大小） */
    uint64_t  at_entry_vaddr;    /* AT_ENTRY：主程序 _start（动态程序仍指向主程序） */
    int       env_type;          /* ENV_NATIVE / ENV_LINUX_COMPAT（按 ELF 自身判定） */
    int       has_interp;        /* 是否带 PT_INTERP */

    void     *interp_loaded;     /* 解释器偏置加载缓冲（无解释器时为 NULL） */
    uint64_t  interp_load_size;
    uint64_t  interp_code_vaddr;

    uint64_t  map_base;          /* PIE 预留基址（0 = 按链接地址原样映射） */
    uint8_t  *elf_buffer;        /* 原始 ELF 文件缓冲 */
    uint64_t  file_size;         /* 文件实际读到的字节数 */
} uproc_image_t;

/* 把 path 指向的 ELF 读进内核、校验、判定 ABI、摆好偏置（含解释器）。
 *
 * path/argc/argv 契约与 hepu_exec 一致；argc/argv 本阶段仅透传保留，
 * 初始栈构造仍在进程模块（第二阶段再下沉到本接口）。
 *
 * 返回 0 = 成功，img 各字段填好；< 0 = HEPU_E_NOENT / HEPU_E_LOAD /
 * HEPU_E_NOMEM（口径同 uproc.h）。失败时本函数已自清理内部缓冲与预留 VA，
 * 调用方无需回滚。 */
int uproc64_load_image(const char *path, int argc, const char *const argv[],
                       uproc_image_t *img);

/* 建槽失败时回滚 loader 预留的 PENDING VA（主程序 PIE 区间）。
 * 解释器区间沿用历史口径：建槽失败路径不在此释放（见 user_process.c 原实现）。 */
void uproc64_image_release_pending(uproc_image_t *img);

/* ---- PENDING VA 账本接口（实现留在 user_process.c，loader 经此调用） ---- */

/* 在 [pool_base, pool_end) 预留一段 PENDING VA，返回起始地址（0 = 失败）。 */
uint64_t uproc_va_alloc_pending(uint64_t pool_base, uint64_t pool_end, uint64_t size);
/* 释放一段 PENDING VA 预留（base = 之前 alloc 返回的起始地址）。 */
void     uproc_va_free_pending(uint64_t base);
/* 建槽成功：把 PENDING 预留区间移交给 pid 对应的进程槽位。 */
void     uproc_va_adopt_pending(int pid);

#endif /* HEPU_UPROC_LOADER64_H */
