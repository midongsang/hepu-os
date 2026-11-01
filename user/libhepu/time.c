/* ============================================================================
 * 赫普OS - 时间函数实现
 * V8.20: 初始实现
 * ========================================================================== */

#include <time.h>
#include <sys/types.h>
#include <string.h>

/* 系统调用 */
#define SYS_TIME    13
#define SYS_GETTIMEOFDAY 78

static inline uint32_t syscall0(uint32_t num)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall1(uint32_t num, uint32_t arg1)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

/* 获取当前时间（秒） */
time_t time(time_t *t)
{
    time_t now = (time_t)syscall0(SYS_TIME);
    if (t) *t = now;
    return now;
}

/* 获取当前时间（微秒精度） */
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    if (!tv) return -1;
    /* 简化：使用time() + 0微秒 */
    tv->tv_sec = time(NULL);
    tv->tv_usec = 0;
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

/* 获取指定时钟的时间 */
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (!tp) return -1;
    /* 简化：所有时钟都使用CLOCK_REALTIME */
    tp->tv_sec = time(NULL);
    tp->tv_nsec = 0;
    return 0;
}

/* 获取时钟分辨率 */
int clock_getres(clockid_t clk_id, struct timespec *res)
{
    if (!res) return -1;
    res->tv_sec = 0;
    res->tv_nsec = 1000000;  /* 1毫秒分辨率（简化） */
    return 0;
}

/* 获取进程CPU时间 */
clock_t clock(void)
{
    /* 简化：返回当前时间（微秒） */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (clock_t)(tv.tv_sec * 1000000 + tv.tv_usec);
}

/* 计算时间差 */
double difftime(time_t time1, time_t time0)
{
    return (double)(time1 - time0);
}

/* 月份天数（非闰年） */
static const int days_in_month[] = {31, 28, 31, 30, 31, 30,
                                      31, 31, 30, 31, 30, 31};

/* 判断是否为闰年 */
static int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* 将time_t转换为分解时间（UTC） */
struct tm *gmtime(const time_t *timep)
{
    static struct tm result;
    return gmtime_r(timep, &result);
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    if (!timep || !result) return NULL;

    time_t t = *timep;
    int days = t / 86400;
    int seconds = t % 86400;

    /* 计算时分秒 */
    result->tm_hour = seconds / 3600;
    seconds %= 3600;
    result->tm_min = seconds / 60;
    result->tm_sec = seconds % 60;

    /* 计算年月日（从1970年1月1日开始） */
    int year = 1970;
    while (days >= (is_leap_year(year) ? 366 : 365)) {
        days -= is_leap_year(year) ? 366 : 365;
        year++;
    }
    result->tm_year = year - 1900;
    result->tm_yday = days;

    /* 计算月日 */
    int month = 0;
    while (month < 12) {
        int dim = days_in_month[month];
        if (month == 1 && is_leap_year(year)) dim = 29;
        if (days < dim) break;
        days -= dim;
        month++;
    }
    result->tm_mon = month;
    result->tm_mday = days + 1;

    /* 计算星期（1970年1月1日是星期四） */
    result->tm_wday = (days + 4) % 7;  /* 0=周日 */

    /* 其他字段 */
    result->tm_isdst = 0;  /* 不使用夏令时 */
    result->tm_gmtoff = 0;
    result->tm_zone = "UTC";

    return result;
}

/* 将time_t转换为本地时间（简化：与UTC相同） */
struct tm *localtime(const time_t *timep)
{
    static struct tm result;
    return localtime_r(timep, &result);
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    /* 简化：本地时间 = UTC（无时区） */
    return gmtime_r(timep, result);
}

/* 将分解时间转换为time_t */
time_t mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;

    time_t days = 0;

    /* 计算从1970年到指定年份的天数 */
    int year = tm->tm_year + 1900;
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    /* 计算从年初到指定月份的天数 */
    for (int m = 0; m < tm->tm_mon; m++) {
        days += days_in_month[m];
        if (m == 1 && is_leap_year(year)) days += 1;
    }

    /* 加上日期 */
    days += tm->tm_mday - 1;

    /* 转换为秒 */
    time_t seconds = days * 86400;
    seconds += tm->tm_hour * 3600;
    seconds += tm->tm_min * 60;
    seconds += tm->tm_sec;

    return seconds;
}

/* 简化的时间格式化 */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
    if (!s || !format || !tm || max == 0) return 0;

    size_t pos = 0;
    const char *p = format;

    while (*p && pos < max - 1) {
        if (*p == '%' && *(p + 1)) {
            p++;
            char buf[32];
            int len = 0;

            switch (*p) {
                case 'Y':  /* 年（4位） */
                    len = sprintf(buf, "%04d", tm->tm_year + 1900);
                    break;
                case 'y':  /* 年（2位） */
                    len = sprintf(buf, "%02d", (tm->tm_year + 1900) % 100);
                    break;
                case 'm':  /* 月（2位） */
                    len = sprintf(buf, "%02d", tm->tm_mon + 1);
                    break;
                case 'd':  /* 日（2位） */
                    len = sprintf(buf, "%02d", tm->tm_mday);
                    break;
                case 'H':  /* 时（24小时制，2位） */
                    len = sprintf(buf, "%02d", tm->tm_hour);
                    break;
                case 'I':  /* 时（12小时制，2位） */
                    len = sprintf(buf, "%02d", tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12);
                    break;
                case 'M':  /* 分（2位） */
                    len = sprintf(buf, "%02d", tm->tm_min);
                    break;
                case 'S':  /* 秒（2位） */
                    len = sprintf(buf, "%02d", tm->tm_sec);
                    break;
                case 'A':  /* 星期全名 */
                case 'a':  /* 星期缩写 */
                    {
                        static const char *days[] = {"Sun", "Mon", "Tue", "Wed",
                                                       "Thu", "Fri", "Sat"};
                        len = strlen(days[tm->tm_wday]);
                        memcpy(buf, days[tm->tm_wday], len);
                    }
                    break;
                case 'B':  /* 月份全名 */
                case 'b':  /* 月份缩写 */
                    {
                        static const char *months[] = {"Jan", "Feb", "Mar", "Apr",
                                                         "May", "Jun", "Jul", "Aug",
                                                         "Sep", "Oct", "Nov", "Dec"};
                        len = strlen(months[tm->tm_mon]);
                        memcpy(buf, months[tm->tm_mon], len);
                    }
                    break;
                case 'p':  /* AM/PM */
                    len = sprintf(buf, "%s", tm->tm_hour < 12 ? "AM" : "PM");
                    break;
                case 'j':  /* 一年中的第几天（3位） */
                    len = sprintf(buf, "%03d", tm->tm_yday + 1);
                    break;
                case 'w':  /* 星期（0-6，0=周日） */
                    len = sprintf(buf, "%d", tm->tm_wday);
                    break;
                case '%':  /* 百分号 */
                    buf[0] = '%';
                    len = 1;
                    break;
                default:
                    buf[0] = '%';
                    buf[1] = *p;
                    len = 2;
                    break;
            }

            if (pos + len < max) {
                memcpy(s + pos, buf, len);
                pos += len;
            }
            p++;
        } else {
            s[pos++] = *p++;
        }
    }

    s[pos] = '\0';
    return pos;
}

/* 将分解时间转换为字符串 */
char *asctime(const struct tm *tm)
{
    static char buf[64];
    return asctime_r(tm, buf);
}

char *asctime_r(const struct tm *tm, char *buf)
{
    if (!tm || !buf) return NULL;
    strftime(buf, 64, "%a %b %d %H:%M:%S %Y\n", tm);
    return buf;
}

/* 将time_t转换为字符串 */
char *ctime(const time_t *timep)
{
    static char buf[64];
    return ctime_r(timep, buf);
}

char *ctime_r(const time_t *timep, char *buf)
{
    if (!timep || !buf) return NULL;
    struct tm *tm = localtime(timep);
    return asctime_r(tm, buf);
}

/* 睡眠（秒） */
unsigned int sleep(unsigned int seconds)
{
    /* 简化：使用系统调用 */
    extern unsigned int sys_sleep(unsigned int seconds);
    return sys_sleep(seconds);
}

/* 睡眠（微秒） */
int usleep(useconds_t usec)
{
    /* 简化：转换为秒并睡眠 */
    if (usec >= 1000000) {
        sleep(usec / 1000000);
    }
    return 0;
}

/* 睡眠（纳秒） */
int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!req) return -1;
    if (req->tv_sec > 0) {
        sleep(req->tv_sec);
    }
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}
