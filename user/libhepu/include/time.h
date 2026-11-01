/* ============================================================================
 * 赫普OS - time.h 头文件
 * 时间相关定义和函数声明
 * V8.20: 初始实现
 * ========================================================================== */

#ifndef _TIME_H
#define _TIME_H

#include <sys/types.h>

/* 时间类型 */
typedef long time_t;           /* 日历时间（秒） */
typedef long suseconds_t;      /* 微秒 */
typedef int clockid_t;         /* 时钟ID */
typedef unsigned long clock_t; /* CPU时间 */

/* 时钟ID */
#define CLOCK_REALTIME           0  /* 系统实时时钟 */
#define CLOCK_MONOTONIC          1  /* 单调时钟（不受系统时间调整影响） */
#define CLOCK_PROCESS_CPUTIME_ID 2  /* 进程CPU时间 */
#define CLOCK_THREAD_CPUTIME_ID  3  /* 线程CPU时间 */
#define CLOCK_MONOTONIC_RAW      4  /* 原始单调时钟（不受NTP调整影响） */
#define CLOCK_REALTIME_COARSE    5  /* 粗粒度实时时钟 */
#define CLOCK_MONOTONIC_COARSE   6  /* 粗粒度单调时钟 */
#define CLOCK_BOOTTIME           7  /* 启动时间（包含挂起时间） */
#define CLOCK_REALTIME_ALARM     8  /* 实时时钟闹钟 */
#define CLOCK_BOOTTIME_ALARM     9  /* 启动时间闹钟 */

/* 每秒时钟滴答数 */
#define CLOCKS_PER_SEC  1000000L  /* 每秒微秒数 */

/* 时间结构 */
struct timespec {
    time_t   tv_sec;   /* 秒 */
    long     tv_nsec;  /* 纳秒 */
};

struct timeval {
    time_t      tv_sec;  /* 秒 */
    suseconds_t tv_usec; /* 微秒 */
};

/* 分解时间结构 */
struct tm {
    int tm_sec;    /* 秒（0-60，60为闰秒） */
    int tm_min;    /* 分（0-59） */
    int tm_hour;   /* 时（0-23） */
    int tm_mday;   /* 日（1-31） */
    int tm_mon;    /* 月（0-11，0为1月） */
    int tm_year;   /* 年（从1900年起） */
    int tm_wday;   /* 星期（0-6，0为周日） */
    int tm_yday;   /* 一年中的第几天（0-365） */
    int tm_isdst;  /* 夏令时标志（>0启用，=0不启用，<0未知） */
    long tm_gmtoff; /* 与UTC的偏移（秒） */
    char *tm_zone;  /* 时区名称 */
};

/* 时区结构 */
struct timezone {
    int tz_minuteswest; /* 与UTC的分钟差（向西为正） */
    int tz_dsttime;     /* 夏令时类型 */
};

/* 函数声明 */

/* 时间获取 */
time_t time(time_t *t);
int gettimeofday(struct timeval *tv, struct timezone *tz);
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_getres(clockid_t clk_id, struct timespec *res);
clock_t clock(void);

/* 时间设置 */
int settimeofday(const struct timeval *tv, const struct timezone *tz);
int clock_settime(clockid_t clk_id, const struct timespec *tp);

/* 时间转换 */
struct tm *gmtime(const time_t *timep);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime(const time_t *timep);
struct tm *localtime_r(const time_t *timep, struct tm *result);
time_t mktime(struct tm *tm);
time_t timegm(struct tm *tm);

/* 时间格式化 */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
char *asctime(const struct tm *tm);
char *asctime_r(const struct tm *tm, char *buf);
char *ctime(const time_t *timep);
char *ctime_r(const time_t *timep, char *buf);

/* 时间解析 */
char *strptime(const char *s, const char *format, struct tm *tm);

/* 时间差 */
double difftime(time_t time1, time_t time0);

/* 睡眠 */
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
int nanosleep(const struct timespec *req, struct timespec *rem);

/* 定时器 */
int timer_create(clockid_t clockid, struct sigevent *sevp, timer_t *timerid);
int timer_delete(timer_t timerid);
int timer_settime(timer_t timerid, int flags, const struct itimerspec *new_value,
                   struct itimerspec *old_value);
int timer_gettime(timer_t timerid, struct itimerspec *curr_value);
int timer_getoverrun(timer_t timerid);

/* 定时器结构 */
struct itimerspec {
    struct timespec it_interval;  /* 定时器间隔 */
    struct timespec it_value;     /* 定时器初始值 */
};

/* 信号事件结构（前向声明） */
struct sigevent;

/* 定时器类型（前向声明） */
typedef int timer_t;

/* 时区设置 */
void tzset(void);
extern char *tzname[2];
extern long timezone;
extern int daylight;

/* 时间常量 */
#define TM_YEAR_BASE  1900  /* tm_year的基准年 */

/* 星期名称 */
extern const char *const day_name[7];
extern const char *const mon_name[12];

#endif /* _TIME_H */
