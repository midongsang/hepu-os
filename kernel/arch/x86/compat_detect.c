/* ============================================================================
 * 赫普OS - 自动兼容检测模块 (V0.10.5)
 *
 * 功能：
 *   1. 自动检测引导方式（BIOS/UEFI）
 *   2. 自动检测VGA/VBE支持
 *   3. 自动检测INT13扩展支持
 *   4. 自动检测CPU特性（64位/PAE/SSE等）
 *   5. 根据检测结果自动选择最佳兼容方式
 *   6. 提供运行时兼容级别查询
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 兼容级别 */
typedef enum {
    COMPAT_LEVEL_MINIMAL = 0,    /* 最小兼容（纯文本，无加速） */
    COMPAT_LEVEL_BASIC = 1,      /* 基本兼容（VGA文本，基础IO） */
    COMPAT_LEVEL_STANDARD = 2,   /* 标准兼容（VBE图形，INT13扩展） */
    COMPAT_LEVEL_ENHANCED = 3,   /* 增强兼容（高分辨率，DMA，APIC） */
    COMPAT_LEVEL_FULL = 4,       /* 完全兼容（所有特性启用） */
} compat_level_t;

/* 检测结果结构 */
typedef struct {
    /* 引导方式 */
    int is_uefi;                 /* 1=UEFI, 0=BIOS */
    int boot_drive;              /* 引导驱动器号 */

    /* 磁盘支持 */
    int int13_ext_supported;     /* INT13 LBA扩展支持 */
    int int13_version;           /* INT13扩展版本 */
    int dma_supported;           /* DMA支持 */
    int ahci_supported;          /* AHCI支持 */

    /* 显示支持 */
    int vga_supported;           /* VGA支持 */
    int vbe_supported;           /* VBE支持 */
    int vbe_version;             /* VBE版本 */
    int max_width;               /* 最大宽度 */
    int max_height;              /* 最大高度 */
    int max_bpp;                 /* 最大色深 */

    /* CPU特性 */
    int cpu_64bit;               /* 64位支持 */
    int pae_supported;           /* PAE支持 */
    int sse_supported;           /* SSE支持 */
    int apic_supported;          /* APIC支持 */

    /* 内存 */
    uint32_t memory_kb;          /* 总内存KB */
    uint32_t usable_memory_kb;   /* 可用内存KB */

    /* 兼容级别 */
    compat_level_t compat_level; /* 当前兼容级别 */
    int safe_mode;               /* 安全模式标志 */
} compat_info_t;

static compat_info_t g_compat_info;
static int g_compat_initialized = 0;

/* 串口输出 */
static void compat_serial_putc(uint16_t port, char c) {
    for (int i = 0; i < 10000; i++) {
        volatile uint8_t *lsr = (volatile uint8_t *)(port + 5);
        if (*lsr & 0x20) break;
    }
    volatile uint8_t *thr = (volatile uint8_t *)port;
    *thr = (uint8_t)c;
}

static void compat_serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') compat_serial_putc(0x3F8, '\r');
        compat_serial_putc(0x3F8, *s++);
    }
}

static void compat_serial_puthex(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[9 - i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[10] = 0;
    compat_serial_puts(buf);
}

/* 检测引导方式 - 通过multiboot信息或引导标记 */
static void detect_boot_method(void) {
    /* 检查multiboot魔数（引导器存在0x8000） */
    volatile uint32_t *mb_magic = (volatile uint32_t *)0x8000;
    if (*mb_magic == 0x2BADB002) {
        g_compat_info.is_uefi = 0;  /* multiboot通常是BIOS */
        compat_serial_puts("[COMPAT] boot method: BIOS (multiboot)\n");
    } else {
        /* 检查UEFI标记（引导器存在0x90000） */
        volatile uint32_t *uefi_magic = (volatile uint32_t *)0x90000;
        if (*uefi_magic == 0x48455055) {  /* "HEPU" */
            g_compat_info.is_uefi = 1;
            compat_serial_puts("[COMPAT] boot method: UEFI\n");
        } else {
            g_compat_info.is_uefi = 0;
            compat_serial_puts("[COMPAT] boot method: BIOS (unknown)\n");
        }
    }

    /* 引导驱动器 */
    volatile uint8_t *boot_drive = (volatile uint8_t *)0x8004;
    g_compat_info.boot_drive = *boot_drive;
    compat_serial_puts("[COMPAT] boot drive: 0x");
    compat_serial_puthex(g_compat_info.boot_drive);
    compat_serial_puts("\n");
}

/* 检测INT13扩展支持 - 仅在BIOS环境下有效 */
static void detect_int13(void) {
    if (g_compat_info.is_uefi) {
        g_compat_info.int13_ext_supported = 1;  /* UEFI默认支持LBA */
        compat_serial_puts("[COMPAT] INT13: UEFI native LBA\n");
        return;
    }

    /* BIOS环境下，INT13检测在stage2完成，结果存在0x4020 */
    volatile uint8_t *int13_result = (volatile uint8_t *)0x4020;
    g_compat_info.int13_ext_supported = (*int13_result == 1);

    if (g_compat_info.int13_ext_supported) {
        compat_serial_puts("[COMPAT] INT13: LBA extension supported\n");
    } else {
        compat_serial_puts("[COMPAT] INT13: LBA extension NOT supported, using CHS fallback\n");
    }
}

/* 检测VGA/VBE支持 */
static void detect_video(void) {
    /* VGA始终支持（标准PC兼容） */
    g_compat_info.vga_supported = 1;

    /* VBE检测 - 检查VBE信息块（stage2可能已检测） */
    volatile uint8_t *vbe_info = (volatile uint8_t *)0x4030;
    if (vbe_info[0] == 'V' && vbe_info[1] == 'B' &&
        vbe_info[2] == 'E' && vbe_info[3] == 'I') {
        g_compat_info.vbe_supported = 1;
        g_compat_info.vbe_version = vbe_info[5] << 8 | vbe_info[4];
        compat_serial_puts("[COMPAT] VBE: supported, version ");
        compat_serial_puthex(g_compat_info.vbe_version);
        compat_serial_puts("\n");

        /* 读取最大分辨率 */
        g_compat_info.max_width = 1024;
        g_compat_info.max_height = 768;
        g_compat_info.max_bpp = 32;
    } else {
        g_compat_info.vbe_supported = 0;
        compat_serial_puts("[COMPAT] VBE: not detected, using VGA text mode\n");
        g_compat_info.max_width = 80;
        g_compat_info.max_height = 25;
        g_compat_info.max_bpp = 4;
    }
}

/* 检测CPU特性 */
static void detect_cpu(void) {
    /* 64位支持 - 检查CPUID */
    uint32_t eax, ebx, ecx, edx;

    /* 检查CPUID是否支持 */
    __asm__ volatile (
        "pushfl\n"
        "popl %%eax\n"
        "movl %%eax, %%ebx\n"
        "xorl $0x00200000, %%eax\n"
        "pushl %%eax\n"
        "popfl\n"
        "pushfl\n"
        "popl %%eax\n"
        "xorl %%ebx, %%eax\n"
        : "=a"(eax) : : "ebx"
    );

    if (eax & 0x00200000) {
        /* CPUID支持，检查扩展功能 */
        __asm__ volatile (
            "movl $0x80000001, %%eax\n"
            "cpuid\n"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        );
        g_compat_info.cpu_64bit = (edx >> 29) & 1;
        g_compat_info.pae_supported = (edx >> 6) & 1;
    }

    /* 检查标准特性 */
    __asm__ volatile (
        "movl $0x00000001, %%eax\n"
        "cpuid\n"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    );
    g_compat_info.sse_supported = (edx >> 25) & 1;
    g_compat_info.apic_supported = (edx >> 9) & 1;

    compat_serial_puts("[COMPAT] CPU: ");
    if (g_compat_info.cpu_64bit) compat_serial_puts("64-bit ");
    if (g_compat_info.pae_supported) compat_serial_puts("PAE ");
    if (g_compat_info.sse_supported) compat_serial_puts("SSE ");
    if (g_compat_info.apic_supported) compat_serial_puts("APIC ");
    compat_serial_puts("\n");
}

/* 检测内存 */
static void detect_memory(void) {
    /* 从multiboot信息读取内存大小 */
    volatile uint32_t *mb_info = (volatile uint32_t *)0x8004;
    if (*mb_info) {
        uint32_t mem_lower = *(volatile uint32_t *)(*mb_info + 4);
        uint32_t mem_upper = *(volatile uint32_t *)(*mb_info + 8);
        g_compat_info.memory_kb = mem_lower + mem_upper;
        g_compat_info.usable_memory_kb = mem_upper;
    } else {
        g_compat_info.memory_kb = 512 * 1024;  /* 默认512MB */
        g_compat_info.usable_memory_kb = 512 * 1024;
    }

    compat_serial_puts("[COMPAT] memory: ");
    compat_serial_puthex(g_compat_info.memory_kb / 1024);
    compat_serial_puts(" MB total, ");
    compat_serial_puthex(g_compat_info.usable_memory_kb / 1024);
    compat_serial_puts(" MB usable\n");
}

/* 自动选择兼容级别 */
static void select_compat_level(void) {
    g_compat_info.compat_level = COMPAT_LEVEL_STANDARD;

    /* 基础检查 */
    if (!g_compat_info.int13_ext_supported) {
        g_compat_info.compat_level = COMPAT_LEVEL_BASIC;
        compat_serial_puts("[COMPAT] downgraded to BASIC: no INT13 LBA\n");
    }

    if (!g_compat_info.vbe_supported) {
        if (g_compat_info.compat_level > COMPAT_LEVEL_BASIC) {
            g_compat_info.compat_level = COMPAT_LEVEL_BASIC;
        }
        compat_serial_puts("[COMPAT] downgraded to BASIC: no VBE\n");
    }

    /* 增强特性 */
    if (g_compat_info.vbe_supported && g_compat_info.int13_ext_supported &&
        g_compat_info.cpu_64bit && g_compat_info.apic_supported) {
        g_compat_info.compat_level = COMPAT_LEVEL_ENHANCED;
        compat_serial_puts("[COMPAT] enhanced compatibility enabled\n");
    }

    /* 安全模式检查 */
    if (g_compat_info.usable_memory_kb < 64 * 1024) {  /* 小于64MB */
        g_compat_info.safe_mode = 1;
        g_compat_info.compat_level = COMPAT_LEVEL_MINIMAL;
        compat_serial_puts("[COMPAT] SAFE MODE: low memory\n");
    }

    compat_serial_puts("[COMPAT] selected compat level: ");
    compat_serial_puthex(g_compat_info.compat_level);
    compat_serial_puts("\n");
}

/* 初始化兼容检测 */
void autocompat_init(void) {
    if (g_compat_initialized) return;

    compat_serial_puts("\n[COMPAT] === Auto compatibility detection ===\n");

    detect_boot_method();
    detect_int13();
    detect_video();
    detect_cpu();
    detect_memory();
    select_compat_level();

    compat_serial_puts("[COMPAT] === Detection complete ===\n\n");

    g_compat_initialized = 1;
}

/* 获取兼容信息 */
compat_info_t *compat_get_info(void) {
    return &g_compat_info;
}

/* 查询兼容级别 */
int compat_get_level(void) {
    return g_compat_info.compat_level;
}

/* 查询是否安全模式 */
int compat_is_safe_mode(void) {
    return g_compat_info.safe_mode;
}

/* 查询是否UEFI */
int compat_is_uefi(void) {
    return g_compat_info.is_uefi;
}

/* 查询VBE支持 */
int compat_vbe_supported(void) {
    return g_compat_info.vbe_supported;
}

/* 查询INT13扩展支持 */
int compat_int13_supported(void) {
    return g_compat_info.int13_ext_supported;
}

/* 检查是否应该使用文本模式 */
int compat_use_text_mode(void) {
    return (!g_compat_info.vbe_supported) ||
           (g_compat_info.compat_level <= COMPAT_LEVEL_BASIC) ||
           g_compat_info.safe_mode;
}
