/* ============================================================================
 * 赫普OS - 3D图形演示应用
 * V0.9.0: P5-5 硬件加速深化 - 3D应用示例
 * 通过系统调用SYS_GFX_3D_DEMO(221)触发内核3D渲染器演示
 * ========================================================================== */

typedef unsigned char  uint8_t;
typedef unsigned int   uint32_t;
typedef int            int32_t;

/* 系统调用号 */
#define SYS_GFX_3D_DEMO  221
#define SYS_EXIT         1
#define SYS_WRITE        4

/* 内联系统调用 */
static inline uint32_t syscall1(uint32_t num, uint32_t arg1) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
    return ret;
}

/* 简单的字符串长度 */
static int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* 打印字符串 */
static void print_str(const char *s) {
    syscall3(SYS_WRITE, 1, (uint32_t)s, my_strlen(s));
}

/* 整数转字符串 */
static void int_to_str(int val, char *buf) {
    char tmp[16];
    int i = 0, neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) tmp[i++] = '0';
    while (val > 0) { tmp[i++] = '0' + val % 10; val /= 10; }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    print_str("HepuOS 3D Graphics Demo\n");
    print_str("========================\n");
    print_str("通过系统调用触发内核3D渲染器演示\n");
    print_str("系统调用号: SYS_GFX_3D_DEMO = 221\n\n");

    /* 解析帧数参数 */
    int frames = 120;
    if (argc > 1) {
        /* 简单解析数字参数 */
        const char *arg = argv[1];
        int val = 0;
        while (*arg >= '0' && *arg <= '9') {
            val = val * 10 + (*arg - '0');
            arg++;
        }
        if (val > 0) frames = val;
    }

    char buf[64];
    print_str("渲染帧数: ");
    int_to_str(frames, buf);
    print_str(buf);
    print_str("\n\n");

    print_str("启动3D演示...\n");

    /* 调用内核3D演示系统调用 */
    uint32_t result = syscall1(SYS_GFX_3D_DEMO, (uint32_t)frames);

    print_str("3D演示完成，返回码: ");
    int_to_str((int)result, buf);
    print_str(buf);
    print_str("\n");

    if (result == 0) {
        print_str("3D渲染器运行正常！\n");
    } else {
        print_str("3D渲染器初始化失败，请检查图形驱动。\n");
    }

    return (int)result;
}

/* 入口点 */
void _start() {
    main(0, 0);
    syscall1(SYS_EXIT, 0);
}
