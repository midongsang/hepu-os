#ifndef GPS_H
#define GPS_H

#include <stdint.h>

#define GPS_MAX_SATELLITES 32
#define GPS_NMEA_BUF_SIZE 256

/* GPS状态 */
typedef enum {
    GPS_OFF = 0,
    GPS_SEARCHING,
    GPS_2D_FIX,
    GPS_3D_FIX,
    GPS_DGPS_FIX,
    GPS_RTK_FIX,
    GPS_RTK_FLOAT,
} gps_fix_t;

/* 卫星信息 */
typedef struct {
    int prn;           /* 卫星编号 */
    int elevation;     /* 仰角 (0-90) */
    int azimuth;       /* 方位角 (0-359) */
    int snr;           /* 信噪比 (dB-Hz) */
    int used;          /* 是否用于定位 */
} gps_satellite_t;

/* GPS位置 */
typedef struct {
    double latitude;      /* 纬度 (度) */
    double longitude;     /* 经度 (度) */
    double altitude;      /* 海拔 (米) */
    float speed;          /* 速度 (m/s) */
    float course;         /* 航向 (度) */
    float hdop;           /* 水平精度因子 */
    float vdop;           /* 垂直精度因子 */
    float pdop;           /* 位置精度因子 */
    gps_fix_t fix;        /* 定位状态 */
    int satellites_used;  /* 使用卫星数 */
    int satellites_view;  /* 可见卫星数 */
    uint64_t timestamp;   /* 时间戳 (ms) */
    int year, month, day;
    int hour, minute, second;
    int valid;            /* 数据有效 */
} gps_position_t;

/* GPS设备 */
typedef struct {
    int initialized;
    int enabled;
    uint32_t baud_rate;
    gps_position_t last_fix;
    gps_satellite_t satellites[GPS_MAX_SATELLITES];
    int satellite_count;
    char nmea_buf[GPS_NMEA_BUF_SIZE];
    int nmea_pos;
    uint64_t update_count;
    /* 统计 */
    uint64_t total_fixes;
    uint64_t time_to_first_fix;
} gps_device_t;

/* 初始化GPS */
void gps_init(void);

/* 启用/禁用GPS */
int gps_enable(int enable);

/* 获取当前位置 */
int gps_get_position(gps_position_t *pos);

/* 获取卫星信息 */
int gps_get_satellites(gps_satellite_t *sats, int max);

/* 解析NMEA语句 */
int gps_parse_nmea(const char *sentence);

/* 计算两点距离 (米) */
double gps_distance(double lat1, double lon1, double lat2, double lon2);

/* 计算方位角 (度) */
double gps_bearing(double lat1, double lon1, double lat2, double lon2);

/* 速度转换 m/s -> km/h */
float gps_speed_kmh(float speed_ms);

/* 速度转换 m/s -> 节 */
float gps_speed_knots(float speed_ms);

/* 格式化坐标 */
void gps_format_latlon(double lat, double lon, char *buf, int len);

/* 获取GPS状态 */
int gps_get_status(gps_device_t *status);

/* GPS中断处理 (串口数据到达) */
void gps_handle_irq(void);

/* GPS轮询 */
void gps_poll(void);

#endif /* GPS_H */
