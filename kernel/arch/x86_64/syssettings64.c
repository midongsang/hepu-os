/* ============================================================================
 * 赫普OS 64位 - 系统设置（增强版）
 * 显示真实系统信息、配置选项，图形化界面
 * V0.10.156: 使用真实系统信息，改进图形化界面
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
extern uint32_t smp_get_cpu_count(void);
extern uint32_t smp_get_current_cpu(void);

#define SETTING_MAX 64

typedef struct {
    const char *category;
    const char *name;
    const char *value;
    const char *description;
} setting_t;

static setting_t settings[SETTING_MAX];
static int setting_count = 0;

/* 数字转字符串 */
static void itoa_s(int value, char *buf)
{
    int i = 0;
    int n = value;
    if (n == 0) { buf[i++] = '0'; buf[i] = 0; return; }
    if (n < 0) { buf[i++] = '-'; n = -n; }
    char tmp[16];
    int j = 0;
    while (n > 0) { tmp[j++] = '0' + n % 10; n /= 10; }
    while (j > 0) buf[i++] = tmp[--j];
    buf[i] = 0;
}

/* 静态字符串缓冲区（用于动态生成的设置值） */
static char str_buf[16][32];
static int str_buf_idx = 0;

static char *alloc_str(void)
{
    char *p = str_buf[str_buf_idx % 16];
    str_buf_idx++;
    return p;
}

/* 简单字符串比较 */
static int strcmp_simple(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* 添加设置项 */
static void add_setting(const char *category, const char *name,
                        const char *value, const char *desc)
{
    if (setting_count >= SETTING_MAX) return;
    settings[setting_count].category = category;
    settings[setting_count].name = name;
    settings[setting_count].value = value;
    settings[setting_count].description = desc;
    setting_count++;
}

/* CPUID指令 */
static inline void cpuid(uint32_t eax, uint32_t *a, uint32_t *b,
                         uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(eax) : "memory");
}

/* 初始化系统设置 */
void syssettings64_init(void)
{
    setting_count = 0;
    str_buf_idx = 0;

    /* === 系统信息 === */
    add_setting("System", "OS Name", "HepuOS", "Operating system name");
    add_setting("System", "Version", "0.56.1", "Current kernel version");
    add_setting("System", "Architecture", "x86_64", "CPU architecture");
    add_setting("System", "Kernel Mode", "Long Mode", "64-bit protected mode");
    add_setting("System", "Boot Loader", "HepuBoot", "Boot loader name");

    /* === CPU信息（真实检测） === */
    uint32_t eax, ebx, ecx, edx;
    char vendor[13] = {0};
    cpuid(0, &eax, &ebx, &ecx, &edx);
    vendor[0] = ebx & 0xFF; vendor[1] = (ebx >> 8) & 0xFF;
    vendor[2] = (ebx >> 16) & 0xFF; vendor[3] = (ebx >> 24) & 0xFF;
    vendor[4] = edx & 0xFF; vendor[5] = (edx >> 8) & 0xFF;
    vendor[6] = (edx >> 16) & 0xFF; vendor[7] = (edx >> 24) & 0xFF;
    vendor[8] = ecx & 0xFF; vendor[9] = (ecx >> 8) & 0xFF;
    vendor[10] = (ecx >> 16) & 0xFF; vendor[11] = (ecx >> 24) & 0xFF;

    char *vendor_str = alloc_str();
    for (int i = 0; i < 12; i++) vendor_str[i] = vendor[i];
    vendor_str[12] = 0;
    add_setting("CPU", "Vendor", vendor_str, "CPU vendor ID");

    cpuid(1, &eax, &ebx, &ecx, &edx);
    uint8_t family = ((eax >> 8) & 0xF) + ((eax >> 20) & 0xFF);
    uint8_t model = ((eax >> 4) & 0xF) | (((eax >> 16) & 0xF) << 4);
    uint8_t stepping = eax & 0xF;

    char *cpu_str = alloc_str();
    cpu_str[0] = 'i'; cpu_str[1] = '5'; cpu_str[2] = '8'; cpu_str[3] = '6';
    cpu_str[4] = '-';
    itoa_s(family * 100 + model, cpu_str + 5);
    add_setting("CPU", "Model", cpu_str, "CPU family/model");

    char *step_str = alloc_str();
    itoa_s(stepping, step_str);
    add_setting("CPU", "Stepping", step_str, "CPU stepping");

    /* SMP信息 */
    uint32_t cpu_cnt = smp_get_cpu_count();
    char *core_str = alloc_str();
    itoa_s(cpu_cnt, core_str);
    add_setting("CPU", "Cores Online", core_str, "Active CPU cores (SMP)");

    char *cur_cpu_str = alloc_str();
    itoa_s(smp_get_current_cpu(), cur_cpu_str);
    add_setting("CPU", "Current CPU", cur_cpu_str, "Currently running CPU");

    /* === 内存信息（真实检测） === */
    uint64_t total, used, free;
    pmm64_get_stats(&total, &used, &free);

    char *mem_total_str = alloc_str();
    itoa_s(total / (1024 * 1024), mem_total_str);
    add_setting("Memory", "Total", mem_total_str, "Total memory (MB)");

    char *mem_used_str = alloc_str();
    itoa_s(used / (1024 * 1024), mem_used_str);
    add_setting("Memory", "Used", mem_used_str, "Used memory (MB)");

    char *mem_free_str = alloc_str();
    itoa_s(free / (1024 * 1024), mem_free_str);
    add_setting("Memory", "Free", mem_free_str, "Free memory (MB)");

    /* === 显示设置 === */
    add_setting("Display", "Graphics", "VBE/VGA", "Graphics backend");
    add_setting("Display", "Resolution", "1024x768", "Screen resolution");
    add_setting("Display", "Color Depth", "32 bpp", "Bits per pixel");
    add_setting("Display", "Font", "TrueType", "System font (DejaVuSans)");

    /* === 存储设置 === */
    add_setting("Storage", "Root FS", "ext2", "Root filesystem type");
    add_setting("Storage", "IDE Driver", "LBA28/48", "IDE disk driver");
    add_setting("Storage", "AHCI", "Supported", "SATA AHCI driver");

    /* === 安全设置 === */
    add_setting("Security", "Secure Boot", "Supported", "UEFI Secure Boot");
    add_setting("Security", "Memory Isolation", "Enabled", "User/kernel isolation");
    add_setting("Security", "Syscall Check", "Enabled", "User pointer validation");

    /* === 网络设置 === */
    add_setting("Network", "Driver", "RTL8139", "Ethernet driver");
    add_setting("Network", "Protocol", "TCP/IPv4", "Network protocols");
    add_setting("Network", "DHCP", "Enabled", "Auto IP configuration");
    add_setting("Network", "DNS", "Enabled", "Domain name resolution");

    /* === 进程设置 === */
    add_setting("Process", "Max Processes", "32", "Maximum process count");
    add_setting("Process", "Priorities", "5 levels", "Priority levels");
    add_setting("Process", "Scheduler", "Preemptive", "Preemptive scheduler");

    vga_puts("[SETTINGS] System settings initialized (");
    char buf[8];
    int n = setting_count, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" items)\n");
}

/* 文本模式显示所有设置 */
void syssettings64_show(void)
{
    vga_puts("\n=== System Settings ===\n");
    const char *last_cat = "";
    for (int i = 0; i < setting_count; i++) {
        if (strcmp_simple(settings[i].category, last_cat) != 0) {
            vga_puts("\n[");
            vga_puts(settings[i].category);
            vga_puts("]\n");
            last_cat = settings[i].category;
        }
        vga_puts("  ");
        vga_puts(settings[i].name);
        vga_puts(": ");
        vga_puts(settings[i].value);
        if (settings[i].description) {
            vga_puts("  (");
            vga_puts(settings[i].description);
            vga_puts(")");
        }
        vga_puts("\n");
    }
    vga_puts("========================\n");
}

/* 图形模式绘制设置界面（增强版） */
void syssettings64_draw(void *fb, int fb_w, int fb_h,
                        int x, int y, int w, int h)
{
    if (!fb) return;

    extern void font64_draw_string(void *fb, int fw, int fh,
                                   int x, int y, const char *str,
                                   uint32_t color, uint16_t size);
    extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);

    /* 背景 */
    gfx64_fill_rect(x, y, w, h, 0xF5F5F5);

    /* 标题栏 */
    gfx64_fill_rect(x, y, w, 36, 0x2C5F8D);
    font64_draw_string(fb, fb_w, fb_h, x + 12, y + 10,
                       "System Settings", 0xFFFFFF, 16);

    /* 分类标签颜色 */
    uint32_t cat_colors[] = {
        0x2C5F8D,  /* System - 蓝 */
        0xD35400,  /* CPU - 橙 */
        0x27AE60,  /* Memory - 绿 */
        0x8E44AD,  /* Display - 紫 */
        0xC0392B,  /* Storage - 红 */
        0x16A085,  /* Security - 青 */
        0x2980B9,  /* Network - 蓝 */
        0xF39C12,  /* Process - 黄 */
    };
    const char *cat_names[] = {
        "System", "CPU", "Memory", "Display",
        "Storage", "Security", "Network", "Process"
    };

    /* 显示设置项，按分类分组 */
    int line_y = y + 50;
    const char *last_cat = "";
    for (int i = 0; i < setting_count && line_y < y + h - 30; i++) {
        /* 分类标题 */
        if (strcmp_simple(settings[i].category, last_cat) != 0) {
            /* 找分类颜色 */
            uint32_t cat_color = 0x555555;
            for (int c = 0; c < 8; c++) {
                if (strcmp_simple(settings[i].category, cat_names[c]) == 0) {
                    cat_color = cat_colors[c];
                    break;
                }
            }
            /* 分类标签背景 */
            gfx64_fill_rect(x + 8, line_y - 2, w - 16, 20, cat_color);
            font64_draw_string(fb, fb_w, fb_h, x + 14, line_y,
                               settings[i].category, 0xFFFFFF, 14);
            line_y += 24;
            last_cat = settings[i].category;
        }

        /* 设置项：名称 + 值 */
        font64_draw_string(fb, fb_w, fb_h, x + 20, line_y,
                           settings[i].name, 0x333333, 14);
        font64_draw_string(fb, fb_w, fb_h, x + 140, line_y,
                           settings[i].value, 0x2C5F8D, 14);
        line_y += 18;
    }

    /* 底部状态栏 */
    gfx64_fill_rect(x, y + h - 28, w, 28, 0xE0E0E0);
    char status[64];
    status[0] = 0;
    char *p = status;
    p[0] = 0;
    /* 简单拼接：Items: N | CPU: M cores */
    const char *s1 = "Items: ";
    while (*s1) *p++ = *s1++;
    char nbuf[8];
    itoa_s(setting_count, nbuf);
    char *np = nbuf;
    while (*np) *p++ = *np++;
    const char *s2 = " | SMP: ";
    while (*s2) *p++ = *s2++;
    char cbuf[8];
    itoa_s(smp_get_cpu_count(), cbuf);
    np = cbuf;
    while (*np) *p++ = *np++;
    const char *s3 = " cores";
    while (*s3) *p++ = *s3++;
    *p = 0;
    font64_draw_string(fb, fb_w, fb_h, x + 12, y + h - 20,
                       status, 0x666666, 12);
}

/* 获取设置项数量 */
int syssettings64_get_count(void)
{
    return setting_count;
}

/* 获取设置项 */
int syssettings64_get(int index, const char **category,
                      const char **name, const char **value)
{
    if (index < 0 || index >= setting_count) return -1;
    if (category) *category = settings[index].category;
    if (name) *name = settings[index].name;
    if (value) *value = settings[index].value;
    return 0;
}

/* 按名称查找设置项 */
const char *syssettings64_get_value(const char *name)
{
    for (int i = 0; i < setting_count; i++) {
        if (strcmp_simple(settings[i].name, name) == 0) {
            return settings[i].value;
        }
    }
    return NULL;
}
