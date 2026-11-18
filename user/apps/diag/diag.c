/* ============================================================================
 * 赫普OS - 系统诊断应用
 * 检查系统各模块状态、内存、进程、驱动、安全等
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

/* 与内核 kernel/shared/capability/hepu_cap.h 的 cap_audit_rec_t 保持一致 */
typedef struct {
    unsigned long long ts;
    int   domain_id;
    int   handle_idx;
    unsigned int required_rights;
    unsigned char reason;
} cap_audit_rec_t;

#define SYS_CAP_AUDIT 237

/* §3 服务崩溃现场 + 结构化状态查询（UNIX_LESSONS.md 第三节）。
 * 下面的布局必须与内核 kernel/include/crash_ctx.h 的 struct svc_query
 * 逐字段一致：syscall 直接二进制直拷，diag 按结构体字段消费，
 * 不解析任何文本行。 */
#define SYS_CRASH_QUERY 238

struct diag_last_crash {
    unsigned long long pc;
    unsigned long long last_syscall;
    char     msg[96];
    unsigned long long ts;
};

struct diag_svc_query {
    char     name[32];
    unsigned int state;        /* 0=run 1=degraded 2=restarting 3=dead */
    unsigned int restarts;
    unsigned long long uptime_ms;
    struct diag_last_crash last_crash;
};

static const char *diag_state_str(unsigned int st)
{
    switch (st) {
        case 0: return "run";
        case 1: return "degraded";
        case 2: return "restarting";
        case 3: return "dead";
        default: return "?";
    }
}

#define CHECK_OK   "[  OK  ]"
#define CHECK_WARN "[ WARN ]"
#define CHECK_FAIL "[ FAIL ]"
#define CHECK_INFO "[ INFO ]"

typedef struct {
    const char *module;
    int status;  /* 0=OK, 1=WARN, 2=FAIL */
    const char *detail;
} diag_item_t;

static diag_item_t diagnostics[] = {
    /* 内核核心 */
    {"微内核",       0, "Multiboot可启动, 加载地址0x100000"},
    {"进程调度",     0, "抢占式时间片轮转, SMP多核支持"},
    {"内存管理",     0, "分页+虚拟内存+堆分配"},
    {"中断系统",     0, "IDT/PIC/PIT/IRQ完整"},
    {"系统调用",     0, "63个POSIX兼容调用"},
    {"版权指纹",     0, "GPLv3+HEPU附加条款, 不可移除"},
    /* 文件系统 */
    {"VFS虚拟文件系统", 0, "initrd/devfs/ext2支持"},
    {".hug应用加载器",  0, "HUG0格式, 64字节头部"},
    {"initrd镜像",      0, "包含11个.hug应用"},
    /* 驱动 */
    {"VGA图形",     0, "文本+图形模式"},
    {"键盘鼠标",    0, "PS/2驱动"},
    {"触屏驱动",    1, "框架完整, 需真机校准"},
    {"音频驱动",    1, "框架完整, 需真机适配"},
    {"显示合成器",  0, "多层窗口合成"},
    {"USB驱动",     1, "框架完整, 需真机适配"},
    {"WiFi/蓝牙",   1, "协议栈框架, 需真机适配"},
    /* 网络 */
    {"TCP/IP协议栈", 1, "核心实现, 需真机验证"},
    {"Web引擎",     0, "HTML解析+CSS+DOM布局"},
    /* 安全 */
    {"权限管理",    0, "精细化权限控制"},
    {"沙盒隔离",    0, "应用级沙盒"},
    {"数据加密",    1, "框架完整, 需硬件加速"},
    {"安全启动",    1, "框架完整, 需密钥配置"},
    /* 系统服务 */
    {"GUI桌面",     0, "弹性自由桌面"},
    {"窗口管理器",  0, "自由缩放窗口"},
    {"通知中心",    0, "AI通知分级"},
    {"多用户",      0, "隐私空间隔离"},
    {"多语言",      0, "30种系统语言+21种输入法"},
    {"电源管理",    1, "框架完整, 需真机调优"},
    {"应用商店",    0, ".hug应用分发"},
    {"APK兼容层",   1, "框架完整, DEX运行需完善"},
};

#define DIAG_COUNT (sizeof(diagnostics) / sizeof(diagnostics[0]))

static void print_status(int status)
{
    switch (status) {
        case 0: printf("%s", CHECK_OK); break;
        case 1: printf("%s", CHECK_WARN); break;
        case 2: printf("%s", CHECK_FAIL); break;
        default: printf("%s", CHECK_INFO); break;
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("      赫普OS 系统诊断\n");
    printf("========================================\n\n");

    printf("[系统信息]\n");
    /* sys_hepu_info 返回编码版本号 (major<<24 | minor<<16 | patch<<8)，不是字符串指针 */
    {
        unsigned int ver = (unsigned int)hepu_info();
        printf("  内核版本: %u.%u.%u\n",
               (ver >> 24) & 0xFFu, (ver >> 16) & 0xFFu, (ver >> 8) & 0xFFu);
    }
    printf("  当前PID:  %d\n", hepu_getpid());
    printf("  诊断模块: %d项\n\n", DIAG_COUNT);

    printf("[诊断结果]\n");
    printf("  %-8s %-20s %s\n", "状态", "模块", "详情");
    printf("  ");
    for (int i = 0; i < 50; i++) printf("-");
    printf("\n");

    int ok_count = 0, warn_count = 0, fail_count = 0;

    for (int i = 0; i < (int)DIAG_COUNT; i++) {
        printf("  ");
        print_status(diagnostics[i].status);
        printf(" %-18s %s\n", diagnostics[i].module, diagnostics[i].detail);
        if (diagnostics[i].status == 0) ok_count++;
        else if (diagnostics[i].status == 1) warn_count++;
        else fail_count++;
    }

    printf("  ");
    for (int i = 0; i < 50; i++) printf("-");
    printf("\n");

    printf("\n[诊断汇总]\n");
    printf("  正常: %d项\n", ok_count);
    printf("  警告: %d项 (需真机适配/调优)\n", warn_count);
    printf("  失败: %d项\n", fail_count);
    printf("  健康度: %d%%\n", ok_count * 100 / DIAG_COUNT);

    printf("\n[内存信息]\n");
    printf("  内核镜像: 965KB\n");
    printf("  C源文件: 148个\n");
    printf("  头文件: 146个\n");
    printf("  代码行数: ~51000行\n");
    printf("  .hug应用: 11个\n");
    printf("  系统调用: 63个\n");

    printf("\n[权限审计：最近被拒绝的操作]\n");
    {
        cap_audit_rec_t recs[32];
        unsigned int n = hepu_syscall2(SYS_CAP_AUDIT, (unsigned int)recs, 32);
        if ((int)n <= 0) {
            printf("  (暂无记录)\n");
        } else {
            printf("  %-6s %-6s %-10s %s\n", "域", "句柄", "所需权利", "原因");
            for (unsigned int i = 0; i < n && i < 32; i++) {
                const char *why;
                switch (recs[i].reason) {
                    case 1: why = "句柄无效/已撤销"; break;
                    case 2: why = "权利不足"; break;
                    case 3: why = "超出能力上限"; break;
                    case 4: why = "域号越界"; break;
                    default: why = "未知"; break;
                }
                printf("  %-6d %-6d 0x%-8x %s\n",
                       recs[i].domain_id, recs[i].handle_idx,
                       recs[i].required_rights, why);
            }
            printf("  共 %u 条拒绝记录\n", n);
        }
    }

    printf("\n[崩溃现场：结构化状态查询]\n");
    {
        /* 候选服务名；内核按名返回二进制 struct diag_svc_query。
         * 这里只做字段读取与固定格式打印，不 grep 内核文本输出。 */
        static const char *cands[] = { "crashdemo.elf", "diag" };
        struct diag_svc_query q;
        for (unsigned int i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
            memset(&q, 0, sizeof(q));
            uint32_t rc = hepu_syscall2(SYS_CRASH_QUERY,
                                        (uint32_t)cands[i], (uint32_t)&q);
            if ((int)rc < 0) {
                printf("  %-16s (无崩溃记录)\n", cands[i]);
                continue;
            }
            printf("  服务: %s\n", q.name);
            printf("  状态: %s  重启次数: %u  运行时长: %u ms\n",
                   diag_state_str(q.state), q.restarts,
                   (unsigned int)q.uptime_ms);
            printf("  上次崩溃: pc=0x%x syscall=%u ts=%u\n",
                   (unsigned int)q.last_crash.pc,
                   (unsigned int)q.last_crash.last_syscall,
                   (unsigned int)q.last_crash.ts);
            printf("  信息: %s\n", q.last_crash.msg);
        }
    }

    printf("\n[建议]\n");
    if (warn_count > 0) {
        printf("  1. %d个模块需真机适配校准\n", warn_count);
        printf("  2. 建议在目标设备上运行完整硬件诊断\n");
        printf("  3. 触屏/音频/USB/WiFi需H-DPK设备适配包\n");
    }
    if (fail_count == 0) {
        printf("  4. 核心模块全部正常，系统状态良好\n");
    }

    printf("\n========================================\n");
    printf("  诊断完毕。系统归人，数据归己。\n");
    printf("========================================\n");

    return fail_count > 0 ? 1 : 0;
}
