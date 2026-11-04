/* ============================================================================
 * 赫普OS - 日历应用
 * 显示月历、日期计算
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

static int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
static const char *month_names[] = {
    "一月","二月","三月","四月","五月","六月",
    "七月","八月","九月","十月","十一月","十二月"
};
static const char *week_names[] = {"日","一","二","三","四","五","六"};

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int get_days(int year, int month)
{
    if (month == 1 && is_leap(year)) return 29;
    return days_in_month[month];
}

/* 蔡勒公式计算星期几 (0=周日) */
static int day_of_week(int year, int month, int day)
{
    if (month < 3) { month += 12; year--; }
    int k = year % 100;
    int j = year / 100;
    int h = (day + 13*(month+1)/5 + k + k/4 + j/4 + 5*j) % 7;
    return (h + 6) % 7;  /* 转换为0=周日 */
}

static void print_calendar(int year, int month)
{
    printf("\n  %s %d\n", month_names[month], year);
    printf("  ---------------------------\n");
    printf("  ");
    for (int i = 0; i < 7; i++) printf("%s ", week_names[i]);
    printf("\n  ");

    int first_day = day_of_week(year, month + 1, 1);
    int days = get_days(year, month);

    for (int i = 0; i < first_day; i++) printf("   ");
    for (int d = 1; d <= days; d++) {
        printf("%2d ", d);
        if ((first_day + d) % 7 == 0) printf("\n  ");
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("      赫普OS 日历\n");
    printf("========================================\n");

    /* 显示2026年8月（当前月） */
    print_calendar(2026, 7);  /* 7=八月(0-indexed) */

    printf("\n--- 日期工具 ---\n");

    /* 今天是星期几 */
    int wd = day_of_week(2026, 8, 23);
    printf("  2026年8月23日 星期%s\n", week_names[wd]);

    /* 距离年底天数 */
    int days_left = 0;
    for (int m = 7; m < 12; m++) days_left += get_days(2026, m);
    days_left -= 23;  /* 已过23天 */
    printf("  距离2026年底还有 %d 天\n", days_left);

    /* 2026年是否闰年 */
    printf("  2026年%s闰年\n", is_leap(2026) ? "是" : "不是");

    /* 一年中的第几天 */
    int day_of_year = 0;
    for (int m = 0; m < 7; m++) day_of_year += get_days(2026, m);
    day_of_year += 23;
    printf("  今天是2026年第 %d 天\n", day_of_year);

    printf("\n--- 全年月历速览 ---\n");
    for (int m = 0; m < 12; m += 3) {
        printf("  %s: %d天  ", month_names[m], get_days(2026, m));
        printf("%s: %d天  ", month_names[m+1], get_days(2026, m+1));
        printf("%s: %d天\n", month_names[m+2], get_days(2026, m+2));
    }

    printf("\n========================================\n");
    printf("  日历运行完毕\n");
    printf("========================================\n");

    return 0;
}
