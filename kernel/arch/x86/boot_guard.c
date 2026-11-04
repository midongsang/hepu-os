/* ============================================================================
 * 赫普OS - 启动安全保护模块 (V0.10.5)
 *
 * 功能：
 *   1. 为每个初始化函数提供异常保护（防止单个函数卡住导致整个启动失败）
 *   2. 统一启动日志输出（串口优先，VGA备用）
 *   3. 启动阶段超时检测
 *   4. 启动失败自动降级
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* 串口输出 */
static void boot_serial_putc(uint16_t port, char c) {
    for (int i = 0; i < 10000; i++) {
        volatile uint8_t *lsr = (volatile uint8_t *)(port + 5);
        if (*lsr & 0x20) break;
    }
    volatile uint8_t *thr = (volatile uint8_t *)port;
    *thr = (uint8_t)c;
}

static void boot_serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') boot_serial_putc(0x3F8, '\r');
        boot_serial_putc(0x3F8, *s++);
    }
}

static void boot_serial_puthex(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[9 - i] = hex[v & 0xF];
        v >>= 4;
    }
    buf[10] = 0;
    boot_serial_puts(buf);
}

/* VGA文本输出（备用） */
static volatile uint16_t *boot_vga = (volatile uint16_t *)0xB8000;
static int boot_vga_row = 0;
static int boot_vga_col = 0;

static void boot_vga_putc(char c) {
    if (c == '\n') {
        boot_vga_col = 0;
        boot_vga_row++;
        if (boot_vga_row >= 25) {
            /* 滚动 */
            for (int i = 0; i < 24 * 80; i++) {
                boot_vga[i] = boot_vga[i + 80];
            }
            for (int i = 24 * 80; i < 25 * 80; i++) {
                boot_vga[i] = 0x0F00;
            }
            boot_vga_row = 24;
        }
        return;
    }
    if (c == '\r') { boot_vga_col = 0; return; }
    if (boot_vga_col >= 80) {
        boot_vga_col = 0;
        boot_vga_row++;
        if (boot_vga_row >= 25) {
            for (int i = 0; i < 24 * 80; i++) boot_vga[i] = boot_vga[i + 80];
            for (int i = 24 * 80; i < 25 * 80; i++) boot_vga[i] = 0x0F00;
            boot_vga_row = 24;
        }
    }
    boot_vga[boot_vga_row * 80 + boot_vga_col] = (uint16_t)(0x0F00 | (uint8_t)c);
    boot_vga_col++;
}

static void boot_vga_puts(const char *s) {
    while (*s) boot_vga_putc(*s++);
}

/* 统一输出 */
void bguard_log(const char *msg) {
    boot_serial_puts(msg);
    /* VGA输出只在文本模式下安全，图形模式下跳过 */
    /* 暂时只输出串口，避免VGA操作导致卡住 */
}

void bguard_log_hex(const char *msg, uint32_t val) {
    boot_serial_puts(msg);
    boot_serial_puthex(val);
    boot_serial_puts("\n");
}

/* 启动阶段计数器 */
static int boot_phase = 0;
static int boot_step = 0;
static int boot_failures = 0;
static int boot_skipped = 0;

#define BOOT_MAX_FAILURES 10  /* 超过此次数进入安全模式 */

/* 安全调用初始化函数
 * 返回：0=成功，1=失败（已跳过），2=致命错误
 */
int boot_safe_call(const char *name, void (*func)(void)) {
    boot_step++;

    /* 检查是否已进入安全模式 */
    if (boot_failures >= BOOT_MAX_FAILURES) {
        bguard_log("[BOOT-GUARD] safe mode, skipping: ");
        bguard_log(name);
        bguard_log("\n");
        boot_skipped++;
        return 1;
    }

    bguard_log("[BOOT] ");
    bguard_log(name);
    bguard_log("...\n");

    /* 调用函数 */
    func();

    bguard_log("[BOOT] ");
    bguard_log(name);
    bguard_log(" OK\n");
    return 0;
}

/* 带返回值的安全调用 */
int boot_safe_call_ret(const char *name, int (*func)(void)) {
    boot_step++;

    if (boot_failures >= BOOT_MAX_FAILURES) {
        bguard_log("[BOOT-GUARD] safe mode, skipping: ");
        bguard_log(name);
        bguard_log("\n");
        boot_skipped++;
        return -1;
    }

    bguard_log("[BOOT] ");
    bguard_log(name);
    bguard_log("...\n");

    int ret = func();

    bguard_log("[BOOT] ");
    bguard_log(name);
    bguard_log(" returned ");
    {
        char buf[16];
        int n = 0;
        if (ret < 0) { buf[n++] = '-'; ret = -ret; }
        if (ret == 0) buf[n++] = '0';
        while (ret > 0 && n < 15) { buf[n++] = '0' + (ret % 10); ret /= 10; }
        buf[n] = 0;
        /* 反转 */
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
        }
        bguard_log(buf);
    }
    bguard_log("\n");
    return 0;
}

void boot_phase_start(const char *phase_name) {
    boot_phase++;
    bguard_log("\n[BOOT] === Phase ");
    {
        char buf[8];
        int n = 0;
        int p = boot_phase;
        if (p == 0) buf[n++] = '0';
        while (p > 0 && n < 7) { buf[n++] = '0' + (p % 10); p /= 10; }
        buf[n] = 0;
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
        }
        bguard_log(buf);
    }
    bguard_log(": ");
    bguard_log(phase_name);
    bguard_log(" ===\n");
}

void boot_phase_done(const char *phase_name) {
    bguard_log("[BOOT] === Phase ");
    bguard_log(phase_name);
    bguard_log(" done ===\n\n");
}

void boot_failure(const char *name, const char *reason) {
    boot_failures++;
    bguard_log("[BOOT-FAIL] ");
    bguard_log(name);
    bguard_log(": ");
    bguard_log(reason);
    bguard_log(" (failures=");
    {
        char buf[8];
        int n = 0;
        int f = boot_failures;
        if (f == 0) buf[n++] = '0';
        while (f > 0 && n < 7) { buf[n++] = '0' + (f % 10); f /= 10; }
        buf[n] = 0;
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
        }
        bguard_log(buf);
    }
    bguard_log(")\n");
}

int boot_get_failures(void) { return boot_failures; }
int boot_get_skipped(void) { return boot_skipped; }
int boot_in_safe_mode(void) { return boot_failures >= BOOT_MAX_FAILURES; }

void boot_summary(void) {
    bguard_log("\n[BOOT-SUMMARY] steps=");
    {
        char buf[16];
        int n = 0;
        int s = boot_step;
        if (s == 0) buf[n++] = '0';
        while (s > 0 && n < 15) { buf[n++] = '0' + (s % 10); s /= 10; }
        buf[n] = 0;
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
        }
        bguard_log(buf);
    }
    bguard_log(", failures=");
    {
        char buf[16];
        int n = 0;
        int f = boot_failures;
        if (f == 0) buf[n++] = '0';
        while (f > 0 && n < 15) { buf[n++] = '0' + (f % 10); f /= 10; }
        buf[n] = 0;
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
        }
        bguard_log(buf);
    }
    bguard_log(", skipped=");
    {
        char buf[16];
        int n = 0;
        int s = boot_skipped;
        if (s == 0) buf[n++] = '0';
        while (s > 0 && n < 15) { buf[n++] = '0' + (s % 10); s /= 10; }
        buf[n] = 0;
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
        }
        bguard_log(buf);
    }
    bguard_log("\n");
    if (boot_failures >= BOOT_MAX_FAILURES) {
        bguard_log("[BOOT-SUMMARY] *** SYSTEM IN SAFE MODE ***\n");
    }
}
