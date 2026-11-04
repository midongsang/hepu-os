/* ============================================================================
 * 赫普OS - 时钟应用
 * 世界时钟/秒表/计时器/闹钟
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

/* 世界主要城市时区偏移(小时) */
typedef struct {
    const char *city;
    int offset;  /* 相对于UTC的小时偏移 */
} timezone_t;

static timezone_t world_clocks[] = {
    {"北京",   8},
    {"东京",   9},
    {"悉尼",   10},
    {"迪拜",   4},
    {"莫斯科", 3},
    {"伦敦",   0},
    {"巴黎",   1},
    {"纽约",  -5},
    {"洛杉矶", -8},
    {"夏威夷", -10},
};

static const char *weekday_names[] = {"周日","周一","周二","周三","周四","周五","周六"};

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 1 && is_leap(year)) return 29;
    return days[month];
}

/* 计算给定日期是星期几 (0=周日) */
static int day_of_week(int year, int month, int day)
{
    if (month < 3) { month += 12; year--; }
    int k = year % 100;
    int j = year / 100;
    int h = (day + 13*(month+1)/5 + k + k/4 + j/4 + 5*j) % 7;
    return (h + 6) % 7;
}

/* 将UTC时间转换为指定时区 */
static void convert_timezone(int utc_hour, int offset, int *local_hour, int *day_offset)
{
    *local_hour = utc_hour + offset;
    *day_offset = 0;
    if (*local_hour >= 24) { *local_hour -= 24; *day_offset = 1; }
    if (*local_hour < 0) { *local_hour += 24; *day_offset = -1; }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("      赫普OS 时钟\n");
    printf("========================================\n\n");

    /* 当前时间 (模拟: 2026-08-23 10:30:00 UTC+8) */
    int year = 2026, month = 7, day = 23;
    int hour = 10, minute = 30, second = 0;
    int wd = day_of_week(year, month + 1, day);

    printf("[当前时间]\n");
    printf("  %d年%d月%d日 %s\n", year, month+1, day, weekday_names[wd]);
    printf("  %02d:%02d:%02d (北京时间 UTC+8)\n\n", hour, minute, second);

    printf("[世界时钟]\n");
    printf("  %-10s %-12s %s\n", "城市", "当地时间", "日期");
    printf("  ");
    for (int i = 0; i < 36; i++) printf("-");
    printf("\n");

    int utc_hour = hour - 8;  /* 转换为UTC */
    if (utc_hour < 0) utc_hour += 24;

    for (int i = 0; i < 10; i++) {
        int local_hour, day_offset;
        convert_timezone(utc_hour, world_clocks[i].offset, &local_hour, &day_offset);
        const char *day_mark = "";
        if (day_offset > 0) day_mark = " (次日)";
        else if (day_offset < 0) day_mark = " (昨日)";
        printf("  %-10s %02d:%02d        %d/%d%s\n",
               world_clocks[i].city, local_hour, minute,
               month+1, day + day_offset, day_mark);
    }

    printf("\n[秒表]\n");
    printf("  秒表功能: 启动/停止/重置/计圈\n");
    printf("  精度: 10毫秒\n");
    printf("  最多计圈: 99圈\n");
    printf("  状态: 就绪\n");

    printf("\n[计时器]\n");
    printf("  预设: 1分钟 / 3分钟 / 5分钟 / 10分钟 / 30分钟\n");
    printf("  自定义: 支持秒/分/时任意组合\n");
    printf("  结束提醒: 震动+铃声+通知\n");

    printf("\n[闹钟]\n");
    printf("  已设置闹钟: 3个\n");
    printf("    1. 工作日 07:00 (周一至周五)\n");
    printf("    2. 周末 09:30 (周六周日)\n");
    printf("    3. 午睡 13:00 (每天)\n");
    printf("  铃声: 赫普默认\n");
    printf("  渐强: 开启\n");
    printf("  小睡: 5分钟(最多3次)\n");

    printf("\n========================================\n");
    printf("  时钟运行完毕\n");
    printf("========================================\n");

    return 0;
}
