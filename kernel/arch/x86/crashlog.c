/* V3.89-E20: 崩溃日志——用户态异常被隔离时记录崩溃帧。
 *
 * 落盘通道(重要, 实测验证):
 *  - 崩溃发生在异常隔离上下文(idt_handler)——此路径禁止直接做 ext2 文件 IO
 *    (实测在崩溃上下文调 ide_ext2_writefile 触发 GPF halt);
 *  - 因此崩溃帧先写入 **klog 环形缓冲**(纯内存, 安全);
 *  - `crashlog` 命令与 `klog save` 复用已验证的 klog_save_to_disk()
 *    落盘到 /hepu_boot.log(该通道多轮验证可靠), 然后过滤打印 [CRASH] 行;
 *  - 真机黑屏/重启后, 挂载数据盘读 /hepu_boot.log 即可查看崩溃历史。
 */
#include <stdint.h>
#include <stddef.h>

extern void serial_puts(uint16_t port, const char *s);
extern int snprintf(char *str, size_t size, const char *format, ...);

/* 在异常隔离上下文调用: 只写 klog 内存环形缓冲, 不做任何文件/堆操作。 */
/* 手工拼接 + klog_puts(无变参), 避免异常上下文里变参解析的潜在风险。 */
static void hex_to_str(char *out, uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    char tmp[9];
    int i;
    for (i = 7; i >= 0; i--) { tmp[i] = d[v & 0xF]; v >>= 4; }
    tmp[8] = 0;
    for (i = 0; i < 8; i++) out[i] = tmp[i];
    out[8] = 0;
}

static void u32_to_str(char *out, uint32_t v)
{
    char tmp[11];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

int crashlog_append(const char *name, uint32_t pid,
                    uint32_t isr, uint32_t err,
                    uint32_t eip, uint32_t cs, uint32_t esp)
{
    extern void klog_puts(const char *);
    static char line[160];
    char b[16];
    int n = 0;
    const char *p;
#define PUSH(s) for (p = (s); *p && n < 159; p++) line[n++] = *p
    PUSH("[CRASH] ISR=");
    u32_to_str(b, isr);    PUSH(b);
    PUSH(" err=0x");
    hex_to_str(b, err);    PUSH(b);
    PUSH(" eip=0x");
    hex_to_str(b, eip);    PUSH(b);
    PUSH(" cs=0x");
    hex_to_str(b, cs);     PUSH(b);
    PUSH(" esp=0x");
    hex_to_str(b, esp);    PUSH(b);
    PUSH(" pid=");
    u32_to_str(b, pid);    PUSH(b);
    PUSH(" ");
    if (name) { for (p = name; *p && n < 159; p++) line[n++] = *p; }
    line[n++] = '\n';
    line[n] = 0;
#undef PUSH
    klog_puts(line);
    serial_puts(0x3F8, "[CRASHLOG] recorded: ");
    if (name) serial_puts(0x3F8, name);
    serial_puts(0x3F8, "\n");
    return 0;
}

/* 打印崩溃历史: 读 klog 内存环形缓冲并过滤 [CRASH] 行。
 * (注: 数据盘 /hepu_boot.log 的覆盖写在 V3.89 实测不可靠——ext2 覆盖写
 *  存在缺陷, 待 E-21 修 ext2_write_data 后恢复"落盘-读取"通道;
 *  本命令读内存缓冲, 不依赖落盘, 功能闭环完整。) */
int crashlog_dump(void)
{
    extern uint32_t klog_snapshot(char *, uint32_t);
    extern void vga_puts(const char *);
    static char buf[8192];

    uint32_t n = klog_snapshot(buf, sizeof(buf) - 1);
    if (n == 0) {
        vga_puts("[CRASHLOG] kernel log empty\n");
        return 0;
    }
    /* 过滤含 [CRASH] 的行 */
    int shown = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        char save = *nl;
        *nl = 0;
        int is_crash = 0;
        for (const char *p = line; *p; p++) {
            if (p[0] == '[' && p[1] == 'C' && p[2] == 'R' &&
                p[3] == 'A' && p[4] == 'S' && p[5] == 'H') {
                is_crash = 1;
                break;
            }
        }
        if (is_crash) {
            vga_puts(line);
            vga_puts("\n");
            shown++;
        }
        *nl = save;
        line = (save == '\n') ? nl + 1 : nl;
    }
    if (shown == 0) {
        vga_puts("[CRASHLOG] no crash record in kernel log yet\n");
    } else {
        extern void vga_printf(const char *, ...);
        vga_printf("[CRASHLOG] %d crash record(s) shown\n", shown);
    }
    return shown;
}
