/*
 * HepuBoot UEFI 引导器 - 基础框架（基于gnu-efi标准库）
 *
 * 学习GRUB和UEFI的优点：C语言开发、原生保护模式、GOP高分辨率、ESP分区配置
 *
 * 版本: v0.04 (内核加载到0x100000 + 魔数校验)
 * 日期: 2026-09-23
 */

#include <efi.h>
#include <efilib.h>

/* 自定义字符串函数 (UEFI freestanding环境不用标准C库) */
static int my_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}
static void my_strcpy(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

/* V0.10.5: 直接写VGA文本缓冲区（ExitBootServices后也能输出）
 * 用于真机启动调试，不依赖UEFI服务 */
static UINTN vga_text_pos = 0;
static void vga_puts(const char *s)
{
    volatile UINT16 *vga = (volatile UINT16 *)0xB8000;
    while (*s) {
        if (*s == '\n') {
            vga_text_pos = (vga_text_pos / 80 + 1) * 80;
        } else if (*s == '\r') {
            vga_text_pos = (vga_text_pos / 80) * 80;
        } else {
            if (vga_text_pos < 80 * 25) {
                vga[vga_text_pos] = (UINT16)(*s) | 0x0F00; /* 白字黑底 */
            }
            vga_text_pos++;
        }
        s++;
    }
    if (vga_text_pos >= 80 * 25) vga_text_pos = 0;
}
static void vga_put_hex(UINT64 v)
{
    char buf[17];
    const char *hex = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[16] = 0;
    vga_puts(buf);
}

/* V0.10.6: 串口输出（COM1, 0x3F8）
 * 用于QEMU调试，不依赖VGA显示 */
static void serial_putc(char c)
{
    volatile UINT8 *uart = (volatile UINT8 *)0x3F8;
    /* 等待发送缓冲区为空 */
    int timeout = 100000;
    while ((uart[5] & 0x20) == 0 && timeout-- > 0) {}
    uart[0] = (UINT8)c;
}
static void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s);
        s++;
    }
}
static void serial_put_hex(UINT64 v)
{
    char buf[17];
    const char *hex = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[16] = 0;
    serial_puts(buf);
}

/* 统一输出函数：同时输出到VGA和串口 */
static void debug_puts(const char *s)
{
    vga_puts(s);
    serial_puts(s);
}
static void debug_put_hex(UINT64 v)
{
    vga_put_hex(v);
    serial_put_hex(v);
}

/* V6.70: CRC32哈希校验（多项式0xEDB88320，查表法） */
static UINT32 crc32_table[256];
static int crc32_table_ready = 0;
static void crc32_init_table(void)
{
    for (UINT32 i = 0; i < 256; i++) {
        UINT32 c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}
static UINT32 crc32_calc(const void *data, UINTN len)
{
    if (!crc32_table_ready) crc32_init_table();
    UINT32 crc = 0xFFFFFFFF;
    const UINT8 *p = (const UINT8 *)data;
    for (UINTN i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* V6.71: 启动耗时统计（基于UEFI GetTime，毫秒精度） */
static UINT64 boot_start_ms = 0;
static UINT64 get_time_ms(void)
{
    EFI_TIME t;
    uefi_call_wrapper(RT->GetTime, 2, &t, NULL);
    /* 简化：只计算当天的毫秒数（足够启动耗时统计） */
    UINT64 ms = (UINT64)t.Hour * 3600000ULL +
                (UINT64)t.Minute * 60000ULL +
                (UINT64)t.Second * 1000ULL +
                (UINT64)t.Nanosecond / 1000000ULL;
    return ms;
}
static void boot_timer_start(void)
{
    boot_start_ms = get_time_ms();
}
static UINT64 boot_timer_elapsed(void)
{
    UINT64 now = get_time_ms();
    if (now < boot_start_ms) now += 24 * 3600000ULL;  /* 跨天处理 */
    return now - boot_start_ms;
}

/* gnu-efi全局变量（ST/BS由libefi定义，gImageHandle需自定义） */
EFI_HANDLE gImageHandle;

/* 前向声明 */
static void set_color(UINTN fg, UINTN bg);
static void clear_screen(void);
static void print_str(const CHAR16 *str);
static void print_hex(UINT64 val);

/* 引导配置 */
typedef struct {
    int timeout;
    int default_mode;
    char cmdline[256];
    int has_config;
    UINT32 expected_crc;  /* V6.71: 期望的内核CRC32，0表示跳过校验 */
    int kernel_arch;      /* V0.10.6: 0=32位, 1=64位 */
} hepboot_config_t;
static hepboot_config_t g_config = {3, 0, "", 0, 0, 0};

/* 外部汇编函数: x64→32位切换并跳转内核 */
extern void jump_to_kernel_32(UINT64 kernel_entry, UINT64 stack_ptr, UINT64 low_mem_addr, UINT64 mb_info);

/* V0.10.6: 外部汇编函数: 64位长模式直接跳转内核 */
extern void jump_to_kernel_64(UINT64 kernel_entry, UINT64 mb_info_addr, UINT64 stack_ptr);

/* 写I/O端口 */
static inline void outb(UINT16 port, UINT8 val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline UINT8 inb(UINT16 port)
{
    UINT8 val;
    __asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Multiboot 结构定义 (简化版) */
typedef struct {
    UINT32 size;
    UINT64 base_addr;
    UINT64 length;
    UINT32 type;
} __attribute__((packed)) mb_mmap_entry_t;

typedef struct {
    UINT32 flags;
    UINT32 mem_lower;
    UINT32 mem_upper;
    UINT32 boot_device;
    UINT32 cmdline;
    UINT32 mods_count;
    UINT32 mods_addr;
    UINT32 syms[4];
    UINT32 mmap_length;
    UINT32 mmap_addr;
    UINT32 drives_length;
    UINT32 drives_addr;
    UINT32 config_table;
    UINT32 boot_loader_name;
    UINT32 apm_table;
    UINT32 vbe_control_info;
    UINT32 vbe_mode_info;
    UINT16 vbe_mode;
    UINT16 vbe_interface_seg;
    UINT16 vbe_interface_off;
    UINT16 vbe_interface_len;
    UINT64 framebuffer_addr;
    UINT32 framebuffer_pitch;
    UINT32 framebuffer_width;
    UINT32 framebuffer_height;
    UINT8  framebuffer_bpp;
    UINT8  framebuffer_type;
    UINT8  color_info[6];  /* V6.96: 前2字节存储刷新率 */
} __attribute__((packed)) multiboot_info_t;

/* GOP图形模式信息 */
static struct {
    int available;
    UINT64 framebuffer_base;
    UINT32 framebuffer_size;
    UINT32 pixels_per_scanline;
    UINT32 width;
    UINT32 height;
    UINT32 bpp;
    UINT32 refresh_rate;  /* V6.96: 刷新率(Hz) */
} g_gop_info = {0, 0, 0, 0, 0, 0, 0, 60};

/* 构建multiboot info结构 (放在低内存0x90000) */
static multiboot_info_t *build_multiboot_info(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size, UINTN desc_size)
{
    multiboot_info_t *mb_info = (multiboot_info_t *)0x90000;
    mb_mmap_entry_t *mb_mmap = (mb_mmap_entry_t *)0x90200;
    UINT32 mmap_count = 0;
    UINT64 mem_upper = 0;

    /* 遍历UEFI内存映射, 转换为multiboot格式 */
    UINTN count = map_size / desc_size;
    for (UINTN i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
        /* EfiConventionalMemory=1 才是可用内存 */
        UINT32 mb_type = (desc->Type == EfiConventionalMemory) ? 1 : 2;
        mb_mmap[mmap_count].size = 20;
        mb_mmap[mmap_count].base_addr = desc->PhysicalStart;
        mb_mmap[mmap_count].length = desc->NumberOfPages * 4096;
        mb_mmap[mmap_count].type = mb_type;
        if (mb_type == 1 && desc->PhysicalStart >= 0x100000) {
            mem_upper += desc->NumberOfPages * 4; /* KB */
        }
        mmap_count++;
    }

    /* flags: bit0=mem_info, bit2=cmdline, bit6=mmap, bit12=framebuffer */
    mb_info->flags = 0x1045;
    if (g_gop_info.available) {
        mb_info->flags |= (1 << 12); /* framebuffer有效 */
    }
    mb_info->mem_lower = 639; /* 640KB低端内存 */
    mb_info->mem_upper = (UINT32)(mem_upper > 0 ? mem_upper : 131072); /* 至少128MB */
    mb_info->mmap_length = mmap_count * sizeof(mb_mmap_entry_t);
    mb_info->mmap_addr = 0x90200;
    mb_info->boot_device = 0x80000000;

    /* framebuffer信息 (GOP高分辨率) */
    if (g_gop_info.available) {
        mb_info->framebuffer_addr = g_gop_info.framebuffer_base;
        mb_info->framebuffer_pitch = g_gop_info.pixels_per_scanline * (g_gop_info.bpp / 8);
        mb_info->framebuffer_width = g_gop_info.width;
        mb_info->framebuffer_height = g_gop_info.height;
        mb_info->framebuffer_bpp = (UINT8)g_gop_info.bpp;
        mb_info->framebuffer_type = 1; /* RGB真彩色 */
        /* V6.96: 在color_info字段存储刷新率 */
        mb_info->color_info[0] = (UINT8)(g_gop_info.refresh_rate & 0xFF);
        mb_info->color_info[1] = (UINT8)((g_gop_info.refresh_rate >> 8) & 0xFF);
    } else {
        mb_info->framebuffer_addr = 0;
        mb_info->framebuffer_pitch = 0;
        mb_info->framebuffer_width = 0;
        mb_info->framebuffer_height = 0;
        mb_info->framebuffer_bpp = 0;
        mb_info->framebuffer_type = 0;
    }

    /* 命令行参数 (放在0x91000) */
    char *cmdline_buf = (char *)0x91000;
    if (g_config.cmdline[0]) {
        int i = 0;
        while (g_config.cmdline[i] && i < 255) {
            cmdline_buf[i] = g_config.cmdline[i];
            i++;
        }
        cmdline_buf[i] = 0;
        mb_info->cmdline = 0x91000;
    } else {
        cmdline_buf[0] = 0;
        mb_info->cmdline = 0x91000;
    }

    return mb_info;
}

/* 初始化GOP高分辨率图形模式 (V6.96 智能分辨率+刷新率选择) */
static int init_gop(void)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3,
        &gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gop);
    if (EFI_ERROR(status) || !gop || !gop->Mode) {
        print_str(L"    [GOP] 不支持Graphics Output Protocol\r\n");
        return 0;
    }

    /* 尝试获取EDID信息 (用于刷新率检测) */
    UINT32 refresh_rate = 60; /* 默认60Hz */
    {
        EFI_EDID_DISCOVERED_PROTOCOL *edid = NULL;
        status = uefi_call_wrapper(BS->LocateProtocol, 3,
            &gEfiEdidDiscoveredProtocolGuid, NULL, (VOID **)&edid);
        if (!EFI_ERROR(status) && edid && edid->Edid && edid->SizeOfEdid >= 128) {
            /* 从EDID解析首选刷新率 (简化版: 从详细时序描述符) */
            UINT8 *e = (UINT8*)edid->Edid;
            /* EDID详细时序描述符从偏移36开始, 每个18字节 */
            for (int d = 0; d < 4; d++) {
                int off = 36 + d * 18;
                if (off + 17 >= edid->SizeOfEdid) break;
                UINT16 pixel_clock = e[off] | (e[off+1] << 8);
                if (pixel_clock == 0) continue;
                UINT8 h_active = e[off+2] | ((e[off+4] & 0xF0) << 4);
                UINT8 v_active = e[off+5] | ((e[off+7] & 0xF0) << 4);
                UINT16 h_total = e[off+8] | ((e[off+9] & 0xF0) << 4);
                UINT16 v_total = e[off+10] | ((e[off+11] & 0xF0) << 4);
                if (h_active > 0 && v_active > 0 && h_total > 0 && v_total > 0) {
                    refresh_rate = (pixel_clock * 10000) / (h_total * v_total);
                    if (refresh_rate >= 30 && refresh_rate <= 240) break;
                }
            }
            print_str(L"    [EDID] 显示器刷新率: ");
            print_hex(refresh_rate);
            print_str(L"Hz\r\n");
        }
    }

    /* 枚举所有模式，智能选择最佳模式 */
    UINTN best_mode = gop->Mode->Mode;
    UINT32 best_score = 0;
    UINT32 best_w = 0, best_h = 0, best_bpp = 0;
    UINT32 mode_count = 0;

    for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN info_size = 0;
        status = uefi_call_wrapper(gop->QueryMode, 4, gop, i, &info_size, &info);
        if (!EFI_ERROR(status) && info) {
            UINT32 w = info->HorizontalResolution;
            UINT32 h = info->VerticalResolution;
            UINT32 bpp = 32;
            mode_count++;

            /* 检测色深 */
            if (info->PixelFormat == PixelBitMask) {
                UINT32 mask = info->PixelInformation.RedMask |
                              info->PixelInformation.GreenMask |
                              info->PixelInformation.BlueMask |
                              info->PixelInformation.ReservedMask;
                UINT32 bits = 0;
                while (mask) { bits++; mask >>= 1; }
                bpp = ((bits + 7) / 8) * 8;
                if (bpp < 16) bpp = 32;
            }

            /* 智能评分系统 (V6.96) */
            UINT32 score = 0;

            /* 1. 标准分辨率优先级 (兼顾清晰度和UI可用性) */
            if (w == 1920 && h == 1080)  score += 10000000;  /* FHD - 最佳平衡 */
            if (w == 2560 && h == 1440)  score += 9500000;   /* QHD - 高分辨率 */
            if (w == 1280 && h == 720)   score += 8000000;   /* HD - 兼容性好 */
            if (w == 1366 && h == 768)   score += 7500000;   /* HD+ - 笔记本常见 */
            if (w == 1600 && h == 900)   score += 7000000;   /* HD+ */
            if (w == 1680 && h == 1050)  score += 6500000;   /* WSXGA+ */
            if (w == 1440 && h == 900)   score += 6000000;   /* WXGA+ */
            if (w == 1024 && h == 768)   score += 5000000;   /* XGA - 保底 */
            if (w == 3840 && h == 2160)  score += 4500000;   /* 4K - UI可能过小 */
            if (w == 1280 && h == 1024)  score += 4000000;   /* SXGA - 4:3 */
            if (w == 800 && h == 600)    score += 3000000;   /* SVGA - 低分辨率 */

            /* 2. 宽屏比例优先 */
            if (w * 9 == h * 16)  score += 1500000;  /* 16:9 */
            if (w * 10 == h * 16) score += 1000000;  /* 16:10 */
            if (w * 3 == h * 4)   score += 500000;   /* 4:3 */

            /* 3. 色深优先 (32bpp > 24bpp > 16bpp) */
            score += bpp * 10000;

            /* 4. 分辨率适中加分 (避免过高导致UI过小) */
            UINT32 pixels = w * h;
            if (pixels >= 1000000 && pixels <= 4000000) score += 500000;  /* 1M-4M像素 */

            /* 5. 限制最大分辨率 (UI缩放上限200%) */
            if (w > 2560 || h > 1440) score -= 2000000;

            /* 6. 当前模式轻微加分 (避免不必要的模式切换) */
            if (i == gop->Mode->Mode) score += 10000;

            if (score > best_score) {
                best_score = score;
                best_mode = i;
                best_w = w;
                best_h = h;
                best_bpp = bpp;
            }
            FreePool(info);
        }
    }

    print_str(L"    [GOP] 枚举 ");
    print_hex(mode_count);
    print_str(L" 个模式, 最佳: ");
    print_hex(best_w);
    print_str(L"x");
    print_hex(best_h);
    print_str(L" @ ");
    print_hex(best_bpp);
    print_str(L"bpp ");
    print_hex(refresh_rate);
    print_str(L"Hz\r\n");

    /* 设置选定模式 */
    if (best_mode != gop->Mode->Mode) {
        status = uefi_call_wrapper(gop->SetMode, 2, gop, best_mode);
        if (EFI_ERROR(status)) {
            print_str(L"    [GOP] SetMode失败, 使用当前模式\r\n");
        } else {
            print_str(L"    [GOP] 模式切换成功\r\n");
        }
    }

    /* 获取当前模式信息 */
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    if (!info) {
        print_str(L"    [GOP] 无法获取模式信息\r\n");
        return 0;
    }

    g_gop_info.available = 1;
    g_gop_info.framebuffer_base = gop->Mode->FrameBufferBase;
    g_gop_info.framebuffer_size = (UINT32)gop->Mode->FrameBufferSize;
    g_gop_info.pixels_per_scanline = info->PixelsPerScanLine;
    g_gop_info.width = info->HorizontalResolution;
    g_gop_info.height = info->VerticalResolution;

    /* 计算bpp - 支持高色深 */
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
        info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        g_gop_info.bpp = 32;
    } else if (info->PixelFormat == PixelBitMask) {
        UINT32 mask = info->PixelInformation.RedMask |
                      info->PixelInformation.GreenMask |
                      info->PixelInformation.BlueMask |
                      info->PixelInformation.ReservedMask;
        UINT32 bits = 0;
        while (mask) { bits++; mask >>= 1; }
        g_gop_info.bpp = ((bits + 7) / 8) * 8;
        if (g_gop_info.bpp < 16) g_gop_info.bpp = 32;
    } else {
        g_gop_info.bpp = 32;
    }
    g_gop_info.refresh_rate = refresh_rate;

    print_str(L"    [GOP] 分辨率: ");
    print_hex(g_gop_info.width);
    print_str(L"x");
    print_hex(g_gop_info.height);
    print_str(L" bpp=");
    print_hex(g_gop_info.bpp);
    print_str(L" pitch=");
    print_hex(g_gop_info.pixels_per_scanline * (g_gop_info.bpp / 8));
    print_str(L" fb=0x");
    print_hex(g_gop_info.framebuffer_base);
    print_str(L"\r\n");

    return 1;
}

/* 设置VGA 80x25文本模式 (直接写寄存器, 不依赖BIOS) */
static void set_vga_text_mode(void)
{
    /* Misc Output Register */
    outb(0x3C2, 0x67);

    /* Sequence Registers */
    static const UINT8 seq[] = {0x03,0x00,0x03,0x00,0x02};
    for (int i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, seq[i]);
    }

    /* CRTC Registers (先解锁) */
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);
    static const UINT8 crtc[] = {
        0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,
        0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,0xFF
    };
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    /* Graphics Registers */
    static const UINT8 gc[] = {0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF};
    for (int i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, gc[i]);
    }

    /* Attribute Registers */
    inb(0x3DA); /* 重置地址/数据翻转 */
    static const UINT8 attr[] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x41,0x00,0x0F,0x00,0x00
    };
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, i);
        outb(0x3C0, attr[i]);
    }
    outb(0x3C0, 0x20); /* 启用显示 */

    /* 清屏 */
    volatile UINT16 *vga = (UINT16 *)0xB8000;
    for (int i = 0; i < 80*25; i++) vga[i] = 0x0F20;
}

/* HepuBoot UEFI 版本信息 */
#define HEPBOOT_UEFI_VERSION "0.08"
#define HEPBOOT_UEFI_DATE    "2026-09-23"

/* 内核文件路径（ESP分区根目录） */
#define KERNEL_PATH L"\\hepu-kernel.bin"
#define KERNEL64_PATH L"\\KERNEL64.BIN"
#define KERNEL32_PATH L"\\KERNEL32.BIN"
#define CONFIG_PATH L"\\HEPBOOT.CFG"

/* 从ESP分区读取文件到缓冲区 */
static EFI_STATUS read_esp_file(CHAR16 *path, VOID **buf, UINTN *size)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_FILE_IO_INTERFACE *fs = NULL;
    EFI_FILE_HANDLE root = NULL, file = NULL;
    EFI_FILE_INFO *file_info = NULL;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256;
    UINTN read_size;

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               gImageHandle, &LoadedImageProtocol,
                               (VOID**)&loaded_image);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               loaded_image->DeviceHandle, &FileSystemProtocol,
                               (VOID**)&fs);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(root->Open, 5, root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(root->Close, 1, root);
        return status;
    }

    file_info = AllocateZeroPool(info_size);
    status = uefi_call_wrapper(file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        FreePool(file_info);
        file_info = AllocateZeroPool(info_size);
        status = uefi_call_wrapper(file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, file_info);
    }
    if (EFI_ERROR(status)) {
        FreePool(file_info);
        uefi_call_wrapper(file->Close, 1, file);
        uefi_call_wrapper(root->Close, 1, root);
        return status;
    }

    *size = file_info->FileSize;
    *buf = AllocatePool(*size + 1);
    if (!*buf) {
        FreePool(file_info);
        return EFI_OUT_OF_RESOURCES;
    }
    read_size = *size;
    status = uefi_call_wrapper(file->Read, 3, file, &read_size, *buf);
    ((UINT8 *)*buf)[*size] = 0;

    FreePool(file_info);
    uefi_call_wrapper(file->Close, 1, file);
    uefi_call_wrapper(root->Close, 1, root);
    return status;
}

/* 解析配置文件 */
static void parse_config(char *buf, UINTN size)
{
    char *p = buf;
    char *end = buf + size;
    g_config.has_config = 1;

    while (p < end) {
        char *line_start = p;
        char *line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;

        /* 跳过注释和空行 */
        if (line_start < line_end && *line_start != '#') {
            char *eq = line_start;
            while (eq < line_end && *eq != '=') eq++;
            if (eq < line_end) {
                *eq = 0;
                char *key = line_start;
                char *val = eq + 1;
                /* 去除首尾空格 */
                while (*key == ' ') key++;
                while (val < line_end && *val == ' ') val++;

                if (my_strcmp(key, "timeout") == 0) {
                    g_config.timeout = 0;
                    while (*val >= '0' && *val <= '9') {
                        g_config.timeout = g_config.timeout * 10 + (*val - '0');
                        val++;
                    }
                } else if (my_strcmp(key, "default") == 0) {
                    if (my_strcmp(val, "safe") == 0) g_config.default_mode = 1;
                    else if (my_strcmp(val, "recovery") == 0) g_config.default_mode = 2;
                    else g_config.default_mode = 0;
                } else if (my_strcmp(key, "cmdline") == 0) {
                    int i = 0;
                    while (val < line_end && i < 255) {
                        g_config.cmdline[i++] = *val++;
                    }
                    g_config.cmdline[i] = 0;
                } else if (my_strcmp(key, "expected_crc") == 0) {
                    /* 解析十六进制CRC32值（支持0x前缀） */
                    if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) val += 2;
                    UINT32 crc = 0;
                    while (*val) {
                        char c = *val;
                        int d;
                        if (c >= '0' && c <= '9') d = c - '0';
                        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                        else break;
                        crc = crc * 16 + d;
                        val++;
                    }
                    g_config.expected_crc = crc;
                } else if (my_strcmp(key, "kernel_arch") == 0) {
                    /* V0.10.6: 内核架构 0=32位, 1=64位 */
                    if (my_strcmp(val, "64") == 0 || my_strcmp(val, "x86_64") == 0) {
                        g_config.kernel_arch = 1;
                    } else {
                        g_config.kernel_arch = 0;
                    }
                }
            }
        }
        p = line_end + 1;
        if (p < end && *p == '\n') p++;
    }
}

/* UEFI变量GUID (HepuBoot专用) */
static EFI_GUID gHepuBootVendorGuid = {
    0x48657075, 0x426F, 0x6F74, {0x55, 0x45, 0x46, 0x49, 0x56, 0x61, 0x72, 0x31}
};

/* 读取引导失败计数 */
static UINT32 read_boot_fail_count(void)
{
    UINT32 count = 0;
    UINTN size = sizeof(UINT32);
    EFI_STATUS status = uefi_call_wrapper(RT->GetVariable, 5,
        L"HepuBootFailCount", &gHepuBootVendorGuid,
        NULL, &size, &count);
    if (EFI_ERROR(status)) return 0;
    return count;
}

/* 写入引导失败计数 */
static void write_boot_fail_count(UINT32 count)
{
    uefi_call_wrapper(RT->SetVariable, 5,
        L"HepuBootFailCount", &gHepuBootVendorGuid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        sizeof(UINT32), &count);
}

/* 清除引导失败计数 */
static void clear_boot_fail_count(void)
{
    uefi_call_wrapper(RT->SetVariable, 5,
        L"HepuBootFailCount", &gHepuBootVendorGuid,
        0, 0, NULL);
}

/* 加载配置文件 */
static void load_config(void)
{
    VOID *buf = NULL;
    UINTN size = 0;
    EFI_STATUS status = read_esp_file(CONFIG_PATH, &buf, &size);
    if (!EFI_ERROR(status) && buf && size > 0) {
        parse_config((char *)buf, size);
        print_str(L"    [OK] 配置文件已加载 (timeout=");
        /* 打印timeout数字 */
        CHAR16 numbuf[8];
        int t = g_config.timeout, i = 6;
        numbuf[7] = 0;
        if (t == 0) numbuf[--i] = '0';
        while (t > 0 && i > 0) { numbuf[--i] = '0' + (t % 10); t /= 10; }
        print_str(&numbuf[i]);
        print_str(L"s)\r\n");
        FreePool(buf);
    } else {
        print_str(L"    [INFO] 无配置文件，使用默认设置\r\n");
    }
}

/* 引导菜单 */
static int show_boot_menu(void)
{
    int selected = g_config.default_mode;
    int countdown = g_config.timeout;
    UINT32 fail_count = read_boot_fail_count();
    const CHAR16 *items[] = {
        L"  1. Hepu OS 64-bit (Default)",
        L"  2. Hepu OS 32-bit (Stable)"
    };

    /* 如果失败次数>=2，自动选择32位稳定版（仅当 ESP 中确实存在 32 位内核） */
    if (fail_count >= 2) {
        VOID *probe = NULL;
        UINTN probe_size = 0;
        if (!EFI_ERROR(read_esp_file(KERNEL32_PATH, &probe, &probe_size))) {
            selected = 1;
        }
        if (probe) FreePool(probe);
    }

    clear_screen();
    set_color(EFI_LIGHTCYAN, EFI_BLACK);
    print_str(L"\r\n  ========================================\r\n");
    print_str(L"  |       HepuBoot UEFI Boot Menu        |\r\n");
    print_str(L"  ========================================\r\n\r\n");

    /* 显示失败计数警告 */
    if (fail_count > 0) {
        set_color(EFI_RED, EFI_BLACK);
        print_str(L"  [!] Previous boot failures: ");
        CHAR16 fbuf[8];
        UINT32 fc = fail_count;
        int fi = 6;
        fbuf[7] = 0;
        if (fc == 0) fbuf[--fi] = '0';
        while (fc > 0 && fi > 0) { fbuf[--fi] = '0' + (fc % 10); fc /= 10; }
        print_str(&fbuf[fi]);
        if (fail_count >= 2) {
            print_str(L" -> Auto 32-bit Stable\r\n");
        } else {
            print_str(L"\r\n");
        }
        set_color(EFI_LIGHTGRAY, EFI_BLACK);
        print_str(L"  Press C to clear failure count\r\n\r\n");
    }

    while (countdown >= 0) {
        /* 显示菜单项 */
        for (int i = 0; i < 2; i++) {
            if (i == selected) {
                set_color(EFI_BLACK, EFI_WHITE);
                print_str(L">");
            } else {
                set_color(EFI_LIGHTGRAY, EFI_BLACK);
                print_str(L" ");
            }
            print_str(items[i]);
            if (i == selected) set_color(EFI_LIGHTGRAY, EFI_BLACK);
            print_str(L"\r\n");
        }

        set_color(EFI_YELLOW, EFI_BLACK);
        print_str(L"\r\n  Auto-boot in ");
        CHAR16 numbuf[4];
        int t = countdown, i = 2;
        numbuf[3] = 0;
        if (t == 0) numbuf[--i] = '0';
        while (t > 0 && i >= 0) { numbuf[i--] = '0' + (t % 10); t /= 10; }
        print_str(&numbuf[i+1]);
        print_str(L"s. \x18\x19 select, Enter boot, C clear count\r\n");

        /* 等待按键（1秒超时） */
        EFI_EVENT timer;
        uefi_call_wrapper(BS->CreateEvent, 5, EVT_TIMER, 0, NULL, NULL, &timer);
        uefi_call_wrapper(BS->SetTimer, 3, timer, TimerRelative, 10000000);
        EFI_EVENT events[2] = { ST->ConIn->WaitForKey, timer };
        UINTN index;
        UINTN num_events = 2;
        EFI_STATUS ks = uefi_call_wrapper(BS->WaitForEvent, 3, num_events, events, &index);
        uefi_call_wrapper(BS->CloseEvent, 1, timer);

        if (!EFI_ERROR(ks) && index == 0) {
            /* 按键事件 */
            EFI_INPUT_KEY key;
            uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
            if (key.ScanCode == 0x01) { /* Up */
                selected = (selected + 2) % 3;
                countdown = g_config.timeout;
            } else if (key.ScanCode == 0x02) { /* Down */
                selected = (selected + 1) % 3;
                countdown = g_config.timeout;
            } else if (key.UnicodeChar == 0x0D || key.UnicodeChar == 0x0A) { /* Enter */
                break;
            } else if (key.UnicodeChar == '1') { selected = 0; break; }
            else if (key.UnicodeChar == '2') { selected = 1; break; }
            else if (key.UnicodeChar == '3') { selected = 2; break; }
            else if (key.UnicodeChar == 'c' || key.UnicodeChar == 'C') {
                clear_boot_fail_count();
                fail_count = 0;
                set_color(EFI_GREEN, EFI_BLACK);
                print_str(L"\r\n  [OK] Failure count cleared\r\n");
                set_color(EFI_LIGHTGRAY, EFI_BLACK);
                countdown = g_config.timeout;
                /* 重新显示菜单 */
                clear_screen();
                set_color(EFI_LIGHTCYAN, EFI_BLACK);
                print_str(L"\r\n  ========================================\r\n");
                print_str(L"  |       HepuBoot UEFI Boot Menu        |\r\n");
                print_str(L"  ========================================\r\n\r\n");
            }
        } else {
            countdown--;
        }

        /* 清除菜单区域（光标回到菜单顶部） */
        print_str(L"\x1b[6A"); /* 光标上移6行 */
    }

    clear_screen();
    return selected;
}

/* 设置文本颜色 */
static void set_color(UINTN fg, UINTN bg)
{
    uefi_call_wrapper(ST->ConOut->SetAttribute, 2,
                      ST->ConOut, EFI_TEXT_ATTR(fg, bg));
}

/* 清屏 */
static void clear_screen(void)
{
    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
}

/* 打印字符串 */
static void print_str(const CHAR16 *str)
{
    uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, str);
}

/* 等待按键 */
static void wait_key(void)
{
    UINTN index;
    uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
}

/* 打印十六进制数 */
static void print_hex(UINT64 val)
{
    CHAR16 buf[17];
    int i;
    for (i = 15; i >= 0; i--) {
        int d = (int)(val & 0xF);
        buf[i] = (d < 10) ? (CHAR16)('0' + d) : (CHAR16)('A' + d - 10);
        val >>= 4;
    }
    buf[16] = 0;
    print_str(buf);
}

/*
 * 从ESP分区读取内核文件并加载到物理地址0x100000
 * 输出: kernel_addr - 内核加载地址(0x100000), kernel_size - 内核大小
 * 返回: EFI_SUCCESS=成功
 */

/* 从 EFI 配置表取 ACPI RSDP 物理地址。
 * UEFI 下 ACPI 表由固件通过 ConfigurationTable 传递(ACPI 2.0 GUID), 地址通常在
 * 0xE0000-0xFFFFF 之外 —— 内核沿用 BIOS 方式扫 EBDA/BIOS 区必然扫不到。
 * 这里在 ExitBootServices 之前取好地址, 之后写到固定槽位 0x6020, 供内核优先使用。 */
static UINT64 find_acpi_rsdp_from_table(void)
{
    EFI_GUID acpi20 = {0x8868e871,0xe4f1,0x11d3,
                       {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
    EFI_GUID acpi10 = {0xeb9d2d30,0x2d88,0x11d3,
                       {0x9a,0x16,0x00,0x90,0xef,0xf2,0xb8,0xcd}};
    UINT64 found = 0;
    if (!ST || !ST->ConfigurationTable) return 0;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_GUID *g = &ST->ConfigurationTable[i].VendorGuid;
        if (!CompareGuid(g, &acpi20) || !CompareGuid(g, &acpi10)) {
            UINT64 cand = (UINT64)ST->ConfigurationTable[i].VendorTable;
            /* 校验签名, 避免把非 RSDP 表当地址 */
            char *sig = (char *)cand;
            if (sig[0]=='R' && sig[1]=='S' && sig[2]=='D' && sig[3]==' '
                && sig[4]=='P' && sig[5]=='T' && sig[6]=='R' && sig[7]==' ') {
                found = cand;
                if (CompareGuid(g, &acpi20)) break;  /* 优先 ACPI 2.0 */
            }
        }
    }
    return found;
}

static EFI_STATUS load_kernel_to_memory(CHAR16 *kernel_path, UINT64 *kernel_addr, UINT64 *kernel_size)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_FILE_IO_INTERFACE *fs = NULL;
    EFI_FILE_HANDLE root = NULL;
    EFI_FILE_HANDLE kernel = NULL;
    EFI_FILE_INFO *file_info = NULL;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256;
    UINTN read_size;
    UINT8 magic[5];
    EFI_HANDLE device_handle = NULL;
    UINT64 target_addr = 0x100000;
    UINTN pages;

    /* 1. 获取LoadedImage协议，得到设备句柄（ESP分区） */
    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               gImageHandle, &LoadedImageProtocol,
                               (VOID**)&loaded_image);
    if (EFI_ERROR(status)) {
        print_str(L"    [ERROR] LoadedImage协议获取失败\r\n");
        return status;
    }
    device_handle = loaded_image->DeviceHandle;

    /* 2. 获取文件系统协议 */
    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               device_handle, &FileSystemProtocol,
                               (VOID**)&fs);
    if (EFI_ERROR(status)) {
        print_str(L"    [ERROR] 文件系统协议获取失败\r\n");
        return status;
    }

    /* 3. 打开卷根目录 */
    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) {
        print_str(L"    [ERROR] OpenVolume失败\r\n");
        return status;
    }

    /* 4. 打开内核文件 */
    status = uefi_call_wrapper(root->Open, 5, root, &kernel,
                               kernel_path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        print_str(L"    [ERROR] 内核文件打开失败: ");
        print_hex(status);
        print_str(L"\r\n");
        return status;
    }
    print_str(L"    [OK] 打开内核文件: ");
    print_str(kernel_path);
    print_str(L"\r\n");

    /* 5. 获取文件大小 */
    file_info = AllocateZeroPool(info_size);
    if (!file_info) {
        print_str(L"    [ERROR] 内存分配失败\r\n");
        return EFI_OUT_OF_RESOURCES;
    }
    status = uefi_call_wrapper(kernel->GetInfo, 4, kernel,
                               &gEfiFileInfoGuid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        FreePool(file_info);
        file_info = AllocateZeroPool(info_size);
        if (!file_info) return EFI_OUT_OF_RESOURCES;
        status = uefi_call_wrapper(kernel->GetInfo, 4, kernel,
                                   &gEfiFileInfoGuid, &info_size, file_info);
    }
    if (EFI_ERROR(status)) {
        print_str(L"    [ERROR] 文件信息获取失败\r\n");
        FreePool(file_info);
        return status;
    }

    *kernel_size = file_info->FileSize;
    print_str(L"    [OK] 内核文件大小: ");
    print_hex(*kernel_size);
    print_str(L" 字节\r\n");

    /* 6. 加载内核到0x100000
     * V7.64优化: 直接读取到0x100000，不需要临时缓冲区。
     * 原"临时缓冲+CopyMem"方式需要2倍内存（4MB内核需8MB），
     * 在内存受限环境下可能失败。直接读取可支持更大内核。
     * 注意: 0x100000区域在ExitBootServices后由内核完全控制。 */
    pages = (UINTN)((*kernel_size + 0xFFF) / 0x1000);
    print_str(L"    [INFO] 内核页数: ");
    print_hex(pages);
    print_str(L" (4KB/页)\r\n");

    /* 6a. 检查0x100000区域是否在可用内存中 */
    {
        UINTN map_size = 0;
        EFI_MEMORY_DESCRIPTOR *map = NULL;
        UINTN map_key = 0, desc_size = 0;
        UINT32 desc_version = 0;
        uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, map, &map_key, &desc_size, &desc_version);
        map_size += 2 * desc_size;
        map = AllocatePool(map_size);
        if (map) {
            uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, map, &map_key, &desc_size, &desc_version);
            UINTN count = map_size / desc_size;
            int addr_ok = 0;
            for (UINTN i = 0; i < count; i++) {
                EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
                UINT64 start = desc->PhysicalStart;
                UINT64 end = start + desc->NumberOfPages * 4096;
                if (start <= 0x100000 && end >= 0x100000 + pages * 4096) {
                    print_str(L"    [INFO] 0x100000 region: type=");
                    print_hex(desc->Type);
                    print_str(L" (");
                    if (desc->Type == EfiConventionalMemory) print_str(L"available");
                    else if (desc->Type == EfiLoaderData) print_str(L"loader data");
                    else if (desc->Type == EfiBootServicesData) print_str(L"boot services");
                    else if (desc->Type == EfiRuntimeServicesData) print_str(L"runtime services");
                    else if (desc->Type == EfiMemoryMappedIO) print_str(L"MMIO");
                    else print_str(L"reserved");
                    print_str(L")\r\n");
                    if (desc->Type == EfiConventionalMemory || desc->Type == EfiLoaderData) {
                        addr_ok = 1;
                    }
                    break;
                }
            }
            if (!addr_ok) {
                print_str(L"    [WARN] 0x100000 not in available memory!\r\n");
            }
            FreePool(map);
        }
    }

    /* 6b. 尝试分配0x100000（真机可能失败，失败则直接使用） */
    target_addr = 0x100000;
    status = uefi_call_wrapper(BS->AllocatePages, 4,
                               AllocateAddress, EfiLoaderData,
                               pages, &target_addr);
    if (EFI_ERROR(status)) {
        print_str(L"    [WARN] AllocateAddress(0x100000)失败，直接使用: ");
        print_hex(status);
        print_str(L"\r\n");
        target_addr = 0x100000;
    } else {
        print_str(L"    [OK] 已分配 0x100000\r\n");
    }

    /* 6b. 分块直接读取内核到0x100000
     * V7.64: 增大分块大小到256KB，减少读取次数，提高大内核加载速度 */
    {
        uefi_call_wrapper(kernel->SetPosition, 2, kernel, 0);
        UINTN total_read = 0;
        UINTN chunk_size = 64 * 1024; /* V0.10.5: 减小到64KB提高ISO兼容性 */  /* V7.64: 从64KB增大到256KB */
        UINT8 *buf_ptr = (UINT8*)(UINTN)target_addr;
        while (total_read < *kernel_size) {
            UINTN this_read = (*kernel_size - total_read < chunk_size) ?
                              (*kernel_size - total_read) : chunk_size;
            read_size = this_read;
            status = uefi_call_wrapper(kernel->Read, 3, kernel, &read_size,
                                       buf_ptr + total_read);
            if (EFI_ERROR(status) || read_size != this_read) {
                print_str(L"    [ERROR] 内核读取失败 @ offset=");
                print_hex(total_read);
                print_str(L"\r\n");
                FreePool(file_info);
                return EFI_LOAD_ERROR;
            }
            total_read += read_size;
        }
    }
    print_str(L"    [OK] 内核已加载到 0x100000\r\n");

    /* 8. 验证内核入口魔数
     * 32位内核以 multiboot header 开头: 02 B0 AD 1B
     * 64位内核以入口指令开头: BF 02 B0 AD 2B (mov $0x2badb002,%edi) */
    CopyMem(magic, (VOID*)(UINTN)target_addr, 5);
    print_str(L"    [OK] 内核魔数: ");
    print_hex(magic[0]); print_str(L" ");
    print_hex(magic[1]); print_str(L" ");
    print_hex(magic[2]); print_str(L" ");
    print_hex(magic[3]); print_str(L" ");
    print_hex(magic[4]);
    print_str(L"\r\n");

    int is_mb32 = (magic[0] == 0x02 && magic[1] == 0xB0 &&
                   magic[2] == 0xAD && magic[3] == 0x1B);
    int is_x64  = (magic[0] == 0xBF && magic[1] == 0x02 &&
                   magic[2] == 0xB0 && magic[3] == 0xAD && magic[4] == 0x2B);
    if (!is_mb32 && !is_x64) {
        print_str(L"    [ERROR] 内核魔数校验失败!\r\n");
        FreePool(file_info);
        return EFI_LOAD_ERROR;
    }
    print_str(L"    [OK] 内核魔数校验通过\r\n");

    *kernel_addr = target_addr;
    FreePool(file_info);
    uefi_call_wrapper(kernel->Close, 1, kernel);

    return EFI_SUCCESS;
}

/*
 * HepuBoot UEFI 主入口
 * 参数:
 *   ImageHandle - 镜像句柄
 *   SystemTable - UEFI系统表指针
 */
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    /* V0.10.5: 最早期直接写显存调试 - 不依赖任何库调用
     * 真机启动失败时，看到这行说明引导器已运行；看不到说明Secure Boot阻止或固件未加载 */
    {
        volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
        const char *msg = "HepuBoot UEFI loaded...";
        int i = 0;
        while (msg[i]) {
            vga[160 + i] = (uint16_t)msg[i] | 0x0F00;
            i++;
        }
        /* V0.10.6: 同时输出到串口（COM1） */
        const char *s = "HepuBoot UEFI loaded...\r\n";
        while (*s) {
            volatile uint8_t *uart = (volatile uint8_t *)0x3F8;
            int t = 100000;
            while ((uart[5] & 0x20) == 0 && t-- > 0) {}
            uart[0] = (uint8_t)*s++;
        }
    }
    
    /* 初始化gnu-efi库，并设置ImageHandle */
    InitializeLib(ImageHandle, SystemTable);
    gImageHandle = ImageHandle;

    /* V0.10.5: 最早期调试输出 - 真机启动失败时可判断引导器是否运行 */
    Print(L"HepuBoot UEFI v%s starting...\r\n", HEPBOOT_UEFI_VERSION);
    Print(L"ImageHandle=0x%lx SystemTable=0x%lx\r\n", (UINT64)ImageHandle, (UINT64)SystemTable);

    /* V6.71: 启动耗时统计开始 */
    boot_timer_start();

    /* 清屏并设置颜色 */
    clear_screen();
    set_color(EFI_LIGHTCYAN, EFI_BLACK);

    /* 显示HepuBoot标识（边框40字符，内容行40字符，严格对齐） */
    print_str(L"\r\n");
    print_str(L"  ========================================\r\n");
    print_str(L"  |                                      |\r\n");
    print_str(L"  |     HepuBoot UEFI v" HEPBOOT_UEFI_VERSION L"              |\r\n");
    print_str(L"  |     Hepu OS UEFI Bootloader          |\r\n");
    print_str(L"  |                                      |\r\n");
    print_str(L"  ========================================\r\n");
    print_str(L"\r\n");

    set_color(EFI_LIGHTGRAY, EFI_BLACK);
    print_str(L"  UEFI Firmware: ");
    if (ST->FirmwareVendor) {
        print_str(ST->FirmwareVendor);
    }
    print_str(L"\r\n");
    print_str(L"  Firmware Version: ");
    print_hex(ST->FirmwareRevision);
    print_str(L"\r\n");

    /* V0.10.5: Secure Boot状态检查 */
    {
        UINT8 secure_boot = 0;
        UINTN sb_size = sizeof(secure_boot);
        EFI_STATUS sb_status = uefi_call_wrapper(
            RT->GetVariable, 5,
            (CHAR16 *)L"SecureBoot",
            &gEfiGlobalVariableGuid,
            NULL, &sb_size, &secure_boot);
        if (!EFI_ERROR(sb_status) && secure_boot == 1) {
            set_color(EFI_YELLOW, EFI_BLACK);
            print_str(L"  [WARN] Secure Boot is ENABLED\r\n");
            print_str(L"         If boot fails, import Hepu OS db certificate\r\n");
            print_str(L"         or disable Secure Boot in BIOS setup\r\n");
        } else {
            print_str(L"  Secure Boot: Disabled\r\n");
        }
    }
    print_str(L"\r\n");

    /* 从ESP分区加载内核到0x100000 */
    set_color(EFI_YELLOW, EFI_BLACK);
    print_str(L"  [Loading kernel to 0x100000...]\r\n");
    set_color(EFI_LIGHTGRAY, EFI_BLACK);
    {
        UINT64 kaddr = 0, ksize = 0;
        /* 先加载64位内核（默认） */
        EFI_STATUS ks = load_kernel_to_memory(KERNEL64_PATH, &kaddr, &ksize);
        if (EFI_ERROR(ks)) {
            set_color(EFI_RED, EFI_BLACK);
            print_str(L"  [FATAL] 64位内核加载失败，尝试32位...\r\n");
            set_color(EFI_LIGHTGRAY, EFI_BLACK);
            ks = load_kernel_to_memory(KERNEL32_PATH, &kaddr, &ksize);
            if (EFI_ERROR(ks)) {
                print_str(L"  [FATAL] 内核加载失败，无法启动\r\n");
                wait_key();
                return EFI_LOAD_ERROR;
            }
        }

        /* 内核加载成功，准备跳转 */
        set_color(EFI_GREEN, EFI_BLACK);
        print_str(L"  [OK] 内核就绪 @ 0x100000\r\n");

        /* 加载配置文件 */
        set_color(EFI_YELLOW, EFI_BLACK);
        print_str(L"  [Loading config...]\r\n");
        set_color(EFI_LIGHTGRAY, EFI_BLACK);
        load_config();

        /* 显示引导菜单 */
        int boot_mode = show_boot_menu();

        /* 如果选择32位，重新加载32位内核 */
        if (boot_mode == 1) {
            set_color(EFI_YELLOW, EFI_BLACK);
            print_str(L"  [Loading 32-bit kernel...]\r\n");
            set_color(EFI_LIGHTGRAY, EFI_BLACK);
            ks = load_kernel_to_memory(KERNEL32_PATH, &kaddr, &ksize);
            if (EFI_ERROR(ks)) {
                set_color(EFI_RED, EFI_BLACK);
                print_str(L"  [WARN] 32位内核加载失败，使用64位\r\n");
                set_color(EFI_LIGHTGRAY, EFI_BLACK);
                boot_mode = 0;
            }
        }

        /* 根据选择设置命令行参数 */
        if (boot_mode == 0) {
            my_strcpy(g_config.cmdline, "hepu.arch=x86_64");
        } else {
            my_strcpy(g_config.cmdline, "hepu.arch=i386");
        }
        set_color(EFI_GREEN, EFI_BLACK);
        print_str(L"  [BOOT] Mode: ");
        if (boot_mode == 0) print_str(L"64-bit x86_64\r\n");
        else print_str(L"32-bit i386\r\n");
        set_color(EFI_LIGHTGRAY, EFI_BLACK);

        /* V6.86真机兼容: GOP初始化移到GetMemoryMap之前，
         * 避免GOP操作分配内存改变内存映射导致ExitBootServices失败 */
        set_color(EFI_YELLOW, EFI_BLACK);
        print_str(L"  [Initializing GOP graphics...]\r\n");
        set_color(EFI_LIGHTGRAY, EFI_BLACK);
        int gop_ok = init_gop();
        if (!gop_ok) {
            set_color(EFI_YELLOW, EFI_BLACK);
            print_str(L"  [GOP not available, using UEFI text mode]\r\n");
            set_color(EFI_LIGHTGRAY, EFI_BLACK);
        } else {
            set_color(EFI_GREEN, EFI_BLACK);
            print_str(L"  [OK] GOP high resolution mode enabled\r\n");
            set_color(EFI_LIGHTGRAY, EFI_BLACK);

            /* 方案A启动画面 */
            extern void gop_init(EFI_GRAPHICS_OUTPUT_PROTOCOL *);
            extern void gop_clear(UINT32);
            extern void gop_draw_string(int, int, const char *, UINT32);
            extern void gop_printf(int, int, UINT32, const char *, ...);
            extern void gop_progress_bar(int, int, int, int, int, UINT32);
            extern UINT32 gop_get_width(void);
            extern UINT32 gop_get_height(void);

            EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
            uefi_call_wrapper(BS->LocateProtocol, 3,
                              &gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gop);
            if (gop) {
                gop_init(gop);
                gop_clear(0x00120A0A);

                UINT32 w = gop_get_width();
                UINT32 h = gop_get_height();
                int cx = (w - 240) / 2;
                int cy = h / 2 - 80;

                gop_draw_string(cx, cy, "HepuBoot v0.08", 0x00FFFFFF);
                gop_draw_string(cx, cy + 20, "Hepu OS UEFI Bootloader", 0x00808080);
                gop_draw_string(cx, cy + 50, "Loading kernel...", 0x00FFFFFF);
                gop_progress_bar(cx, cy + 70, 300, 12, 100, 0x00FF9E4A);

                UINT64 boot_ms = boot_timer_elapsed();
                gop_printf(cx, cy + 90, 0x00808080, "Boot time: %d ms", (UINT32)boot_ms);

                UINT32 kernel_crc = crc32_calc((const void *)(UINTN)kaddr, ksize);
                gop_printf(cx, cy + 110, 0x00808080, "Kernel size: %d bytes", ksize);
                gop_printf(cx, cy + 130, 0x00FFFFFF, "CRC32: 0x%08X", kernel_crc);

                if (g_config.expected_crc != 0 && g_config.expected_crc != kernel_crc) {
                    gop_printf(cx, cy + 150, 0x00FF0000, "EXPECTED: 0x%08X", g_config.expected_crc);
                    gop_draw_string(cx, cy + 170, "HASH MISMATCH!", 0x00FF0000);
                    gop_draw_string(cx, cy + 190, "Kernel corrupted!", 0x00FF0000);
                    gop_draw_string(cx, cy + 210, "Boot halted.", 0x00FF0000);
                    set_color(EFI_RED, EFI_BLACK);
                    print_str(L"  [FATAL] CRC32 mismatch! Kernel corrupted.\r\n");
                    set_color(EFI_LIGHTGRAY, EFI_BLACK);
                    uefi_call_wrapper(BS->Stall, 1, 3000000);
                    while (1) {
                        UINTN index;
                        uefi_call_wrapper(ST->ConIn->WaitForKey, 1, ST->ConIn);
                        uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &index);
                    }
                } else {
                    gop_draw_string(cx, cy + 150, "Verified OK", 0x0000FF00);
                }

                uefi_call_wrapper(BS->Stall, 1, 800000);
            }
        }

        /* 分配低内存用于跳转代码执行 */
        UINT64 low_addr = 0x80000;
        ks = uefi_call_wrapper(BS->AllocatePages, 4,
                               AllocateAddress, EfiLoaderData,
                               4, &low_addr);
        if (EFI_ERROR(ks)) {
            print_str(L"  [WARN] 低内存AllocateAddress失败，使用固定地址0x80000\r\n");
            low_addr = 0x80000;
        } else {
            print_str(L"  [OK] 低内存跳转区 @ 0x");
            print_hex(low_addr);
            print_str(L"\r\n");
        }

        set_color(EFI_YELLOW, EFI_BLACK);
        print_str(L"  [Exiting Boot Services...]\r\n");
        set_color(EFI_LIGHTGRAY, EFI_BLACK);

        /* 获取内存映射（ExitBootServices必需）
         * V6.86: 在此之后不再调用任何可能分配内存的函数 */
        UINTN map_size = 0;
        EFI_MEMORY_DESCRIPTOR *map = NULL;
        UINTN map_key = 0;
        UINTN desc_size = 0;
        UINT32 desc_version = 0;

        uefi_call_wrapper(BS->GetMemoryMap, 5,
                          &map_size, map, &map_key, &desc_size, &desc_version);
        map_size += 2 * desc_size;
        map = AllocatePool(map_size);
        if (!map) {
            print_str(L"  [ERROR] 内存映射分配失败\r\n");
            wait_key();
            return EFI_OUT_OF_RESOURCES;
        }

        ks = uefi_call_wrapper(BS->GetMemoryMap, 5,
                               &map_size, map, &map_key, &desc_size, &desc_version);
        if (EFI_ERROR(ks)) {
            print_str(L"  [ERROR] GetMemoryMap失败\r\n");
            wait_key();
            return ks;
        }

        print_str(L"  [OK] 内存映射获取成功, MapKey=");
        print_hex(map_key);
        print_str(L"\r\n");

        /* 构建multiboot info结构 (含内存映射和framebuffer, 内核需要此信息) */
        multiboot_info_t *mb_info = build_multiboot_info(map, map_size, desc_size);
        print_str(L"  [OK] Multiboot info @ 0x90000, mem_upper=");
        print_hex(mb_info->mem_upper);
        print_str(L"KB\r\n");

        /* V0.13.63: 与 stage2(BIOS路径) 一致, 把总内存(字节,64位)写 0x6010,
         * 内核 pmm64_init 读取 (UEFI 路径也摆脱硬编码 2GB) */
        {
            UINT64 total_bytes = ((UINT64)mb_info->mem_lower + mb_info->mem_upper) * 1024ULL;
            if (total_bytes > 0x80000000ULL) total_bytes = 0x80000000ULL; /* 上限 2GB */
            if (total_bytes < 0x10000000ULL) total_bytes = 0x10000000ULL; /* 下限 256MB */
            *(volatile UINT32 *)0x90100 = (UINT32)total_bytes;
            *(volatile UINT32 *)0x90104 = (UINT32)(total_bytes >> 32);
        }

        /* 递增引导失败计数 (内核启动成功后应清除此计数) */
        UINT32 cur_fail = read_boot_fail_count();
        write_boot_fail_count(cur_fail + 1);
        print_str(L"  [INFO] Boot fail count set to: ");
        print_hex(cur_fail + 1);
        print_str(L"\r\n");

        /* V0.10.5: 跳转前调试输出（必须在ExitBootServices之前） */
        print_str(L"  [JUMP] kernel_entry=0x101000 mb_info=0x");
        print_hex((UINT64)mb_info);
        print_str(L" low_mem=0x");
        print_hex(low_addr);
        print_str(L"\r\n");

        /* ACPI RSDP: 必须在 ExitBootServices 之前从配置表取(之后不能调 BS 服务) */
        UINT64 acpi_rsdp_addr = find_acpi_rsdp_from_table();
        print_str(L"  [ACPI] RSDP from config table: 0x");
        print_hex(acpi_rsdp_addr);
        print_str(L"\r\n");

        /* V6.86: ExitBootServices重试循环，真机上可能需要多次重试 */
        int ebs_retries = 0;
        while (ebs_retries < 5) {
            ks = uefi_call_wrapper(BS->ExitBootServices, 2,
                                   gImageHandle, map_key);
            if (!EFI_ERROR(ks)) break;
            /* 失败后重新获取内存映射 */
            uefi_call_wrapper(BS->GetMemoryMap, 5,
                              &map_size, map, &map_key, &desc_size, &desc_version);
            ebs_retries++;
        }
        if (EFI_ERROR(ks)) {
            /* ExitBootServices失败，返回错误让UEFI继续启动下一项 */
            return EFI_LOAD_ERROR;
        }

        /* V0.10.5: ExitBootServices成功，用VGA文本输出调试信息
         * （不依赖UEFI服务，真机启动失败时可看到卡在哪） */
        debug_puts("HepuBoot: ExitBootServices OK\n");
        /* 把 RSDP 地址交给内核: 0x6020(低内存恒等映射区, 内核 acpi_find_rsdp 优先读) */
        *(volatile UINT64 *)0x6020 = acpi_rsdp_addr;
        debug_puts("ACPI RSDP -> 0x6020: ");
        debug_put_hex(acpi_rsdp_addr);
        debug_puts("\n");
        debug_puts("kernel_entry=0x101000 mb_info=0x");
        debug_put_hex((UINT64)mb_info);
        debug_puts("\nlow_mem=0x");
        debug_put_hex(low_addr);
        debug_puts("\nJumping to kernel...\n");

        /* V0.13.59-fix(真机UEFI黑屏): 把GOP帧缓冲信息写到0x6000固定地址,
         * 内核 gfx64_init_from_bootloader 从0x6000读取(width/height/pitch/bpp/fb),
         * 否则UEFI路径下内核拿不到framebuffer, 图形初始化失败→黑屏。
         * 格式与stage2(vbe_info@0x6000)完全一致: w(2)+h(2)+pitch(2)+bpp(1)+fb(4) */
        if (g_gop_info.available) {
            volatile UINT16 *v = (volatile UINT16 *)0x6000;
            v[0] = (UINT16)g_gop_info.width;
            v[1] = (UINT16)g_gop_info.height;
            v[2] = (UINT16)(g_gop_info.pixels_per_scanline * (g_gop_info.bpp / 8));
            *(volatile UINT8 *)(0x6000 + 6) = (UINT8)g_gop_info.bpp;
            *(volatile UINT32 *)(0x6000 + 8) = (UINT32)(g_gop_info.framebuffer_base & 0xFFFFFFFF);
            /* 帧缓冲物理地址可能 >4GB (大显存独显/核显), 高32位写 0x600c */
            *(volatile UINT32 *)(0x6000 + 12) = (UINT32)(g_gop_info.framebuffer_base >> 32);
            debug_puts("FB info -> 0x6000: ");
            debug_put_hex((UINT64)g_gop_info.width);
            debug_puts("x");
            debug_put_hex((UINT64)g_gop_info.height);
            debug_puts(" pitch=");
            debug_put_hex((UINT64)(g_gop_info.pixels_per_scanline * (g_gop_info.bpp / 8)));
            debug_puts(" fb=");
            debug_put_hex(g_gop_info.framebuffer_base);
            debug_puts("\n");
        }

        /* V0.10.6: 根据boot_mode选择跳转函数和入口地址 */
        if (boot_mode == 0) {
            /* 64位内核: 直接在长模式下跳转 */
            debug_puts("Kernel arch: x86_64 (64-bit)\n");
            /* 64 位 flat 镜像由 load_kernel_to_memory() 原样铺到 0x100000,
             * 入口就是镜像首字节(与 BIOS 线 stage2_64 的 jmp 0x100000 一致)。
             * 旧值 0x10000c 是照抄"32 位壳在 0x10000"的写法, 而 UEFI 线并不
             * 加载那段壳, 跳过去必然落在空内存。 */
            debug_puts("Kernel entry: 0x100000\n");
            jump_to_kernel_64(0x100000, (UINT64)mb_info, 0x8F000);
        } else {
            /* 32位内核: 切换到32位保护模式再跳转 */
            debug_puts("Kernel arch: i386 (32-bit)\n");
            jump_to_kernel_32(0x101000, 0x8F000, low_addr, (UINT64)mb_info);
        }

        /* 不应到达这里 */
        debug_puts("ERROR: returned from kernel!\n");
        while (1) {}
    }

    return EFI_SUCCESS;
}
