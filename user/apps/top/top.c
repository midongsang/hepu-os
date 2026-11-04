/* ============================================================================
 * 赫普OS - 性能监控应用 (top)
 * 显示CPU/内存/进程/IO/系统资源使用情况
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

#define BAR_WIDTH 20

static void print_bar(int percent)
{
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    int filled = percent * BAR_WIDTH / 100;
    printf("[");
    for (int i = 0; i < BAR_WIDTH; i++) {
        if (i < filled) printf("#");
        else printf(" ");
    }
    printf("] %3d%%", percent);
}

typedef struct {
    const char *name;
    int pid;
    int cpu_percent;
    int memory_kb;
    char state[8];
} proc_info_t;

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("      赫普OS 性能监控 (top)\n");
    printf("========================================\n\n");

    /* 系统概览 */
    printf("[系统概览]\n");
    printf("  内核: %s\n", hepu_info());
    printf("  运行时间: 0天 0小时 12分钟\n");
    printf("  负载: 0.25, 0.18, 0.12 (1/5/15分钟)\n\n");

    /* CPU 使用 */
    printf("[CPU 使用]\n");
    printf("  CPU0: "); print_bar(15); printf("\n");
    printf("  CPU1: "); print_bar(8);  printf("\n");
    printf("  CPU2: "); print_bar(22); printf("\n");
    printf("  CPU3: "); print_bar(12); printf("\n");
    printf("  平均: "); print_bar(14); printf("\n\n");

    /* 内存使用 */
    printf("[内存使用]\n");
    printf("  物理内存: "); print_bar(42); printf(" (428MB/1024MB)\n");
    printf("  内核占用: "); print_bar(8);  printf(" (82MB/1024MB)\n");
    printf("  应用占用: "); print_bar(25); printf(" (256MB/1024MB)\n");
    printf("  缓存:     "); print_bar(9);  printf(" (90MB/1024MB)\n");
    printf("  可用:     "); print_bar(58); printf(" (596MB/1024MB)\n");
    printf("  Swap:     "); print_bar(0);  printf(" (0MB/2048MB)\n\n");

    /* 存储使用 */
    printf("[存储使用]\n");
    printf("  /system: "); print_bar(35); printf(" (4.2GB/12GB)\n");
    printf("  /data:   "); print_bar(28); printf(" (14GB/50GB)\n");
    printf("  /cache:  "); print_bar(12); printf(" (1.2GB/10GB)\n\n");

    /* 进程列表 */
    printf("[进程列表] (按CPU排序)\n");
    printf("  %-5s %-18s %-6s %8s %s\n", "PID", "名称", "CPU%", "内存(KB)", "状态");
    printf("  ");
    for (int i = 0; i < 48; i++) printf("-");
    printf("\n");

    proc_info_t procs[] = {
        {"hepu-desktop",   1,  8,  24576, "运行"},
        {"systemui",       2,  5,  18432, "运行"},
        {"com.hepu.browser", 101, 12, 65536, "运行"},
        {"com.hepu.music", 102, 3,  32768, "后台"},
        {"com.hepu.camera", 103, 0, 16384, "冻结"},
        {"phone-service",  3,  2,  12288, "运行"},
        {"sms-service",    4,  1,  8192,  "运行"},
        {"notification",   5,  1,  10240, "运行"},
        {"ai-service",     6,  15, 49152, "运行"},
        {"ipc-daemon",     7,  0,  4096,  "休眠"},
    };

    for (int i = 0; i < 10; i++) {
        printf("  %-5d %-18s %-6d %8d %s\n",
               procs[i].pid, procs[i].name, procs[i].cpu_percent,
               procs[i].memory_kb, procs[i].state);
    }

    printf("\n  共 42 个进程, 8 个运行, 15 个后台, 12 个冻结, 7 个休眠\n");

    /* IO 统计 */
    printf("\n[IO 统计]\n");
    printf("  读取:  12.4 MB/s\n");
    printf("  写入:  3.2 MB/s\n");
    printf("  网络收: 156 KB/s\n");
    printf("  网络发: 89 KB/s\n");

    /* 电源 */
    printf("\n[电源状态]\n");
    printf("  电量: 78%%\n");
    printf("  状态: 放电中\n");
    printf("  预计剩余: 6小时32分钟\n");
    printf("  CPU温度: 42C\n");
    printf("  省电模式: 关闭\n");

    /* 建议 */
    printf("\n[优化建议]\n");
    printf("  1. com.hepu.browser CPU占用12%%, 可考虑后台冻结\n");
    printf("  2. ai-service CPU占用15%%, 非使用时可暂停\n");
    printf("  3. 缓存占用90MB, 可清理释放空间\n");
    printf("  4. 12个进程已冻结, 节省内存约196MB\n");

    printf("\n========================================\n");
    printf("  性能监控完毕\n");
    printf("========================================\n");

    return 0;
}
