/* ============================================================================
 * 赫普OS 64位 - GPS 定位服务（NMEA 0183 解析）
 * V0.13.20: P0 系统底层 — GPS 定位框架
 *
 * 数据源: 串口 NMEA 语句（真机 GPS 模块 / 虚拟串口 / 模拟注入）
 * 支持语句: $GPGGA（定位质量/卫星数/海拔）、$GPRMC（状态/坐标/速度）
 * 坐标格式: ddmm.mmmm（度分）, 转换后存为 1e5 度整数（-18000000..18000000）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);

#define GPS_BUF_MAX 128

typedef struct {
    int   initialized;
    int   fix;              /* 0=无定位 1=GPS定位 2=差分定位 */
    int   sats;             /* 可见卫星数 */
    int32_t lat_1e5;        /* 纬度 * 1e5 (-9000000..9000000) */
    int32_t lon_1e5;        /* 经度 * 1e5 (-18000000..18000000) */
    int   altitude_m;       /* 海拔（米） */
    int   speed_kmh;        /* 速度（km/h） */
    int   utc_h;            /* UTC 时间 */
    int   utc_m;
    int   utc_s;
    int   last_feed_ms;     /* 最近有效语句时间（节拍占位） */
} gps_state_t;

static gps_state_t g_gps;

/* ---------- 解析工具 ---------- */
static int gps_parse_int(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

/* 解析 ddmm.mmmm / dddmm.mmmm（度分）→ 1e5 度整数; 负值由调用方处理 */
static int32_t gps_parse_ddmm(const char *s, int deg_digits)
{
    /* 度 = 前 deg_digits 位 (纬度2位, 经度3位) */
    int deg = 0;
    for (int i = 0; i < deg_digits && i < 3; i++) {
        if (s[i] >= '0' && s[i] <= '9') deg = deg * 10 + (s[i] - '0');
    }

    /* 分整数部分 */
    int p = deg_digits;
    int whole_min = 0;
    while (s[p] >= '0' && s[p] <= '9') {
        whole_min = whole_min * 10 + (s[p] - '0');
        p++;
    }
    /* 分小数部分: 十分位起, 权重 10000..1 (×1e5) */
    int frac_min = 0;
    int mul = 10000;
    if (s[p] == '.') {
        p++;
        while (s[p] >= '0' && s[p] <= '9' && mul > 0) {
            frac_min += (s[p] - '0') * mul;
            mul /= 10;
            p++;
        }
    }
    /* 分(×1e5) = 整数分*1e5 + 小数分 */
    int64_t min_1e5 = (int64_t)whole_min * 100000 + frac_min;
    /* 1e5 度 = 度*1e5 + 分(×1e5)/60 */
    return (int32_t)((int64_t)deg * 100000 + min_1e5 / 60);
}

/* 解析 $GPGGA: 时间,纬度,NS,经度,EW,质量,卫星数,HDOP,海拔,... */
static void gps_parse_gga(const char *line)
{
    static char lbuf[GPS_BUF_MAX];
    int li = 0;
    while (line[li] && li < GPS_BUF_MAX - 1) { lbuf[li] = line[li]; li++; }
    lbuf[li] = 0;
    const char *f[10];
    int fc = 0;
    char *cur = lbuf;
    while (fc < 10 && *cur) {
        f[fc++] = cur;
        while (*cur && *cur != ',') cur++;
        if (*cur) { *cur++ = 0; } else break;
    }
    if (fc < 8) return;

    /* f[0]=$GPGGA 1=时间 2=纬度 3=NS 4=经度 5=EW 6=质量 7=卫星 8=HDOP 9=海拔 */
    int tm = gps_parse_int(f[1]);
    g_gps.utc_h = tm / 10000;
    g_gps.utc_m = (tm / 100) % 100;
    g_gps.utc_s = tm % 100;

    int32_t lat = gps_parse_ddmm(f[2], 2);
    int32_t lon = gps_parse_ddmm(f[4], 3);
    if (f[3][0] == 'S') lat = -lat;
    if (f[5][0] == 'W') lon = -lon;

    g_gps.fix = gps_parse_int(f[6]);
    g_gps.sats = gps_parse_int(f[7]);

    /* 海拔: f[9] */
    if (fc >= 10) g_gps.altitude_m = gps_parse_int(f[9]);

    /* 定位质量>=1 才采用坐标 */
    if (g_gps.fix >= 1) {
        g_gps.lat_1e5 = lat;
        g_gps.lon_1e5 = lon;
        g_gps.last_feed_ms++;
    }
}

/* 解析 $GPRMC: 时间,状态,纬度,NS,经度,EW,速度,航向,日期,... */
static void gps_parse_rmc(const char *line)
{
    static char lbuf[GPS_BUF_MAX];
    int li = 0;
    while (line[li] && li < GPS_BUF_MAX - 1) { lbuf[li] = line[li]; li++; }
    lbuf[li] = 0;
    const char *f[9];
    int fc = 0;
    char *cur = lbuf;
    while (fc < 9 && *cur) {
        f[fc++] = cur;
        while (*cur && *cur != ',') cur++;
        if (*cur) { *cur++ = 0; } else break;
    }
    if (fc < 8) return;

    /* f[0]=$GPRMC 1=时间 2=状态 3=纬度 4=NS 5=经度 6=EW 7=速度 */
    int tm = gps_parse_int(f[1]);
    g_gps.utc_h = tm / 10000;
    g_gps.utc_m = (tm / 100) % 100;
    g_gps.utc_s = tm % 100;

    if (f[2][0] == 'A') {   /* A=有效定位 */
        int32_t lat = gps_parse_ddmm(f[3], 2);
        int32_t lon = gps_parse_ddmm(f[5], 3);
        if (f[4][0] == 'S') lat = -lat;
        if (f[6][0] == 'W') lon = -lon;
        g_gps.lat_1e5 = lat;
        g_gps.lon_1e5 = lon;
        g_gps.speed_kmh = gps_parse_int(f[7]) * 1852 / 1000;  /* 节 → km/h */
        if (g_gps.fix == 0) g_gps.fix = 1;
        g_gps.last_feed_ms++;
    }
}

/* ---------- 对外 API ---------- */

/* 初始化 GPS 服务 */
void gps64_init(void)
{
    g_gps.initialized = 1;
    g_gps.fix = 0;
    g_gps.sats = 0;
    g_gps.lat_1e5 = 0;
    g_gps.lon_1e5 = 0;
    g_gps.altitude_m = 0;
    g_gps.speed_kmh = 0;
    g_gps.utc_h = g_gps.utc_m = g_gps.utc_s = 0;
    g_gps.last_feed_ms = 0;
    vga_puts("[GPS64] GPS service initialized\n");
}

/* 喂入一行 NMEA 语句（以 $ 开头） */
int gps64_feed(const char *line)
{
    if (!g_gps.initialized) return -1;
    if (!line || line[0] != '$') return -1;

    if (line[1] == 'G' && line[2] == 'P' && line[3] == 'G' &&
        line[4] == 'G' && line[5] == 'A') {
        gps_parse_gga(line);
        return 0;
    }
    if (line[1] == 'G' && line[2] == 'P' && line[3] == 'R' &&
        line[4] == 'M' && line[5] == 'C') {
        gps_parse_rmc(line);
        return 0;
    }
    return -1;
}

/* 获取定位状态 */
int gps64_get_status(int *fix, int *sats, int32_t *lat_1e5, int32_t *lon_1e5,
                     int *altitude_m, int *speed_kmh)
{
    if (!g_gps.initialized) return -1;
    if (fix) *fix = g_gps.fix;
    if (sats) *sats = g_gps.sats;
    if (lat_1e5) *lat_1e5 = g_gps.lat_1e5;
    if (lon_1e5) *lon_1e5 = g_gps.lon_1e5;
    if (altitude_m) *altitude_m = g_gps.altitude_m;
    if (speed_kmh) *speed_kmh = g_gps.speed_kmh;
    return 0;
}

/* 是否已定位 */
int gps64_is_fixed(void)
{
    return (g_gps.initialized && g_gps.fix >= 1) ? 1 : 0;
}

/* 模拟定位源（无 GPS 硬件时的测试/天气定位回退）:
 * 注入北京坐标 39.9042N 116.4074E */
void gps64_simulate_fix(void)
{
    g_gps.fix = 1;
    g_gps.sats = 8;
    g_gps.lat_1e5 = 3990420;    /* 39.90420 N */
    g_gps.lon_1e5 = 11640740;   /* 116.40740 E */
    g_gps.altitude_m = 43;
    g_gps.speed_kmh = 0;
    g_gps.utc_h = 2; g_gps.utc_m = 0; g_gps.utc_s = 0;
    g_gps.last_feed_ms++;
    vga_puts("[GPS64] Simulated fix injected (39.9042N 116.4074E)\n");
}

/* 打印定位状态 */
void gps64_print_status(void)
{
    if (!g_gps.initialized) { vga_puts("(gps not initialized)\n"); return; }
    if (g_gps.fix < 1) {
        vga_puts("GPS: no fix (sats=");
        /* 十进制 */
        char t[8]; int tp = 0, v = g_gps.sats;
        if (v == 0) t[tp++] = '0';
        while (v) { t[tp++] = '0' + v % 10; v /= 10; }
        for (int k = 0; k < tp / 2; k++) { char c = t[k]; t[k] = t[tp - 1 - k]; t[tp - 1 - k] = c; }
        t[tp] = 0;
        vga_puts(t);
        vga_puts(")\n");
        return;
    }
    vga_puts("GPS: fix=");
    /* fix 数值 */
    char fb[4];
    int fp2 = 0, fv = g_gps.fix;
    if (fv == 0) fb[fp2++] = '0';
    while (fv) { fb[fp2++] = '0' + fv % 10; fv /= 10; }
    for (int k = 0; k < fp2 / 2; k++) { char c = fb[k]; fb[k] = fb[fp2 - 1 - k]; fb[fp2 - 1 - k] = c; }
    fb[fp2] = 0;
    vga_puts(fb);
    vga_puts(" lat=");
    /* 纬度 */
    char buf[32];
    int p = 0;
    int32_t lat = g_gps.lat_1e5;
    int32_t lon = g_gps.lon_1e5;
    if (lat < 0) { buf[p++] = '-'; lat = -lat; }
    buf[p++] = '0' + lat / 1000000 % 10;
    buf[p++] = '0' + lat / 100000 % 10;
    buf[p++] = '.';
    buf[p++] = '0' + lat / 10000 % 10;
    buf[p++] = '0' + lat / 1000 % 10;
    buf[p++] = '0' + lat / 100 % 10;
    buf[p++] = '0' + lat / 10 % 10;
    buf[p++] = '0' + lat % 10;
    buf[p++] = (g_gps.lat_1e5 >= 0) ? 'N' : 'S';
    buf[p] = 0;
    vga_puts(buf);
    vga_puts(" lon=");
    p = 0;
    if (lon < 0) { buf[p++] = '-'; lon = -lon; }
    buf[p++] = '0' + lon / 10000000 % 10;
    buf[p++] = '0' + lon / 1000000 % 10;
    buf[p++] = '0' + lon / 100000 % 10;
    buf[p++] = '.';
    buf[p++] = '0' + lon / 10000 % 10;
    buf[p++] = '0' + lon / 1000 % 10;
    buf[p++] = '0' + lon / 100 % 10;
    buf[p++] = '0' + lon / 10 % 10;
    buf[p++] = '0' + lon % 10;
    buf[p++] = (g_gps.lon_1e5 >= 0) ? 'E' : 'W';
    buf[p] = 0;
    vga_puts(buf);
    vga_puts(" sats=");
    char t[8]; int tp2 = 0, v = g_gps.sats;
    if (v == 0) t[tp2++] = '0';
    while (v) { t[tp2++] = '0' + v % 10; v /= 10; }
    for (int k = 0; k < tp2 / 2; k++) { char c = t[k]; t[k] = t[tp2 - 1 - k]; t[tp2 - 1 - k] = c; }
    t[tp2] = 0;
    vga_puts(t);
    vga_puts("\n");
}
