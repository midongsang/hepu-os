/* ============================================================================
 * 赫普OS - GPS/GNSS 位置服务
 * NMEA解析 + 定位 + 距离/方位计算
 * ========================================================================== */

#include <stdint.h>
#include "gps.h"
#include "vga.h"
#include "string.h"
#include "pit.h"

/* 简单数学函数近似 (裸机无libm) */
static double math_sin(double x) {
    /* 泰勒级数近似, x以弧度为单位 */
    while (x > 3.14159) x -= 6.28318;
    while (x < -3.14159) x += 6.28318;
    double x2 = x * x;
    return x * (1 - x2/6 + x2*x2/120 - x2*x2*x2/5040);
}
static double math_cos(double x) {
    while (x > 3.14159) x -= 6.28318;
    while (x < -3.14159) x += 6.28318;
    double x2 = x * x;
    return 1 - x2/2 + x2*x2/24 - x2*x2*x2/720;
}
static double math_sqrt(double x) {
    if (x <= 0) return 0;
    double r = x;
    for (int i = 0; i < 20; i++) r = (r + x/r) / 2;
    return r;
}
static double math_atan2(double y, double x) {
    if (x > 0) {
        double r = y / x;
        return r - r*r*r/3 + r*r*r*r*r/5;
    }
    if (x < 0 && y >= 0) return 3.14159 + math_atan2(y, -x);
    if (x < 0 && y < 0) return -3.14159 + math_atan2(y, -x);
    if (x == 0 && y > 0) return 1.5708;
    if (x == 0 && y < 0) return -1.5708;
    return 0;
}
static double math_fmod(double x, double y) {
    if (y == 0) return 0;
    double q = x / y;
    long long iq = (long long)q;
    return x - iq * y;
}


static gps_device_t gps_dev;
static int gps_initialized = 0;

/* Haversine公式计算两点距离 */
static double to_radians(double deg) { return deg * 3.141592653589793 / 180.0; }
static double to_degrees(double rad) { return rad * 180.0 / 3.141592653589793; }

void gps_init(void)
{
    memset(&gps_dev, 0, sizeof(gps_device_t));
    gps_dev.initialized = 1;
    gps_dev.enabled = 0;
    gps_dev.baud_rate = 9600;
    gps_initialized = 1;

    /* 模拟一些卫星 */
    gps_dev.satellite_count = 8;
    int prns[] = {3, 7, 12, 15, 19, 23, 28, 32};
    for (int i = 0; i < 8; i++) {
        gps_dev.satellites[i].prn = prns[i];
        gps_dev.satellites[i].elevation = 20 + i * 8;
        gps_dev.satellites[i].azimuth = (i * 45) % 360;
        gps_dev.satellites[i].snr = 30 + i * 3;
        gps_dev.satellites[i].used = (i < 5) ? 1 : 0;
    }

    vga_puts("[GPS] GPS/GNSS location init done \n");
    vga_printf(" baud: %d, satellites visible: %d\n", gps_dev.baud_rate, gps_dev.satellite_count);
}

int gps_enable(int enable)
{
    if (!gps_initialized) return -1;
    gps_dev.enabled = enable ? 1 : 0;
    if (enable) {
        gps_dev.last_fix.fix = GPS_SEARCHING;
        vga_puts("[GPS] GPS enabled, searching satellites ...\n");
    } else {
        gps_dev.last_fix.fix = GPS_OFF;
        vga_puts("[GPS] GPS disabled \n");
    }
    return 0;
}

int gps_get_position(gps_position_t *pos)
{
    if (!gps_initialized || !pos) return -1;
    if (!gps_dev.enabled) return -1;

    /* 原型阶段: 模拟定位 (北京天安门附近) */
    if (gps_dev.last_fix.fix < GPS_2D_FIX) {
        /* 模拟首次定位 */
        gps_dev.last_fix.fix = GPS_3D_FIX;
        gps_dev.last_fix.latitude = 39.9042;
        gps_dev.last_fix.longitude = 116.4074;
        gps_dev.last_fix.altitude = 43.5;
        gps_dev.last_fix.speed = 0.0f;
        gps_dev.last_fix.course = 0.0f;
        gps_dev.last_fix.hdop = 1.2f;
        gps_dev.last_fix.vdop = 1.8f;
        gps_dev.last_fix.pdop = 2.1f;
        gps_dev.last_fix.satellites_used = 5;
        gps_dev.last_fix.satellites_view = 8;
        gps_dev.last_fix.valid = 1;
        gps_dev.last_fix.year = 2026;
        gps_dev.last_fix.month = 8;
        gps_dev.last_fix.day = 22;
        gps_dev.last_fix.hour = 9;
        gps_dev.last_fix.minute = 41;
        gps_dev.last_fix.second = 0;
        gps_dev.total_fixes++;
    }

    memcpy(pos, &gps_dev.last_fix, sizeof(gps_position_t));
    return 0;
}

int gps_get_satellites(gps_satellite_t *sats, int max)
{
    if (!sats || max <= 0) return 0;
    int count = (gps_dev.satellite_count < max) ? gps_dev.satellite_count : max;
    memcpy(sats, gps_dev.satellites, count * sizeof(gps_satellite_t));
    return count;
}

/* 解析NMEA GGA语句 */
static int parse_gga(const char *sentence)
{
    /* $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh */
    (void)sentence;
    return 0;
}

/* 解析NMEA RMC语句 */
static int parse_rmc(const char *sentence)
{
    /* $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*hh */
    (void)sentence;
    return 0;
}

/* 解析NMEA GSV语句 (卫星信息) */
static int parse_gsv(const char *sentence)
{
    (void)sentence;
    return 0;
}

int gps_parse_nmea(const char *sentence)
{
    if (!sentence || sentence[0] != '$') return -1;

    /* 检查校验和 */
    const char *star = strchr(sentence, '*');
    if (!star) return -1;

    /* 识别语句类型 */
    if (strncmp(sentence + 3, "GGA", 3) == 0) return parse_gga(sentence);
    if (strncmp(sentence + 3, "RMC", 3) == 0) return parse_rmc(sentence);
    if (strncmp(sentence + 3, "GSV", 3) == 0) return parse_gsv(sentence);
    if (strncmp(sentence + 3, "GSA", 3) == 0) return 0;
    if (strncmp(sentence + 3, "VTG", 3) == 0) return 0;

    return -1;
}

double gps_distance(double lat1, double lon1, double lat2, double lon2)
{
    double R = 6371000.0;  /* 地球半径 (米) */
    double dlat = to_radians(lat2 - lat1);
    double dlon = to_radians(lon2 - lon1);
    double a = math_sin(dlat/2) * math_sin(dlat/2) +
               math_cos(to_radians(lat1)) * math_cos(to_radians(lat2)) *
               math_sin(dlon/2) * math_sin(dlon/2);
    double c = 2 * math_atan2(math_sqrt(a), math_sqrt(1-a));
    return R * c;
}

double gps_bearing(double lat1, double lon1, double lat2, double lon2)
{
    double phi1 = to_radians(lat1);
    double phi2 = to_radians(lat2);
    double dlon = to_radians(lon2 - lon1);
    double y = math_sin(dlon) * math_cos(phi2);
    double x = math_cos(phi1) * math_sin(phi2) - math_sin(phi1) * math_cos(phi2) * math_cos(dlon);
    double theta = math_atan2(y, x);
    return math_fmod(to_degrees(theta) + 360.0, 360.0);
}

float gps_speed_kmh(float speed_ms)
{
    return speed_ms * 3.6f;
}

float gps_speed_knots(float speed_ms)
{
    return speed_ms * 1.94384f;
}

void gps_format_latlon(double lat, double lon, char *buf, int len)
{
    if (!buf || len <= 0) return;
    char lat_dir = (lat >= 0) ? 'N' : 'S';
    char lon_dir = (lon >= 0) ? 'E' : 'W';
    if (lat < 0) lat = -lat;
    if (lon < 0) lon = -lon;
    /* 简单格式化: 整数部分 */
    int lat_int = (int)lat;
    int lon_int = (int)lon;
    int pos = 0;
    char tmp[16];
    int tlen = 0;
    if (lat_int == 0) tmp[tlen++] = '0';
    while (lat_int > 0) { tmp[tlen++] = '0' + lat_int % 10; lat_int /= 10; }
    for (int i = tlen - 1; i >= 0 && pos < len - 10; i--) buf[pos++] = tmp[i];
    buf[pos++] = lat_dir;
    buf[pos++] = ',';
    buf[pos++] = ' ';
    tlen = 0;
    if (lon_int == 0) tmp[tlen++] = '0';
    while (lon_int > 0) { tmp[tlen++] = '0' + lon_int % 10; lon_int /= 10; }
    for (int i = tlen - 1; i >= 0 && pos < len - 4; i--) buf[pos++] = tmp[i];
    buf[pos++] = lon_dir;
    buf[pos] = '\0';
}

int gps_get_status(gps_device_t *status)
{
    if (!status) return -1;
    memcpy(status, &gps_dev, sizeof(gps_device_t));
    return 0;
}

void gps_handle_irq(void)
{
    /* 原型阶段: 串口数据到达中断 */
}

void gps_poll(void)
{
    if (!gps_dev.enabled) return;
    gps_dev.update_count++;
    /* 原型阶段: 定期更新位置 */
    if (gps_dev.last_fix.fix >= GPS_2D_FIX) {
        gps_dev.last_fix.timestamp++;
    }
}
