#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

#define SENSOR_MAX_DEVICES 16
#define SENSOR_NAME_LEN 32

/* 传感器类型 */
typedef enum {
    SENSOR_ACCELEROMETER = 0,  /* 加速度计 */
    SENSOR_GYROSCOPE,          /* 陀螺仪 */
    SENSOR_MAGNETOMETER,       /* 磁力计 */
    SENSOR_LIGHT,              /* 光线传感器 */
    SENSOR_PROXIMITY,          /* 距离传感器 */
    SENSOR_PRESSURE,           /* 气压计 */
    SENSOR_TEMPERATURE,        /* 温度传感器 */
    SENSOR_HUMIDITY,           /* 湿度传感器 */
    SENSOR_HEART_RATE,         /* 心率传感器 */
    SENSOR_STEP_COUNTER,       /* 计步器 */
    SENSOR_GPS,                /* GPS */
    SENSOR_TYPE_MAX,
} sensor_type_t;

/* 传感器数据 */
typedef struct {
    float x, y, z;       /* 3轴传感器 */
    float value;         /* 单值传感器 */
    uint64_t timestamp;  /* 时间戳 (ms) */
    int accuracy;        /* 精度 0-3 */
} sensor_data_t;

/* 传感器配置 */
typedef struct {
    uint32_t sampling_rate;  /* 采样率 (Hz) */
    int enabled;
    int batching;            /* 批量模式 */
    uint32_t batch_count;
} sensor_config_t;

/* 传感器设备 */
typedef struct {
    int used;
    const char *name;
    sensor_type_t type;
    int vendor;
    int version;
    float resolution;
    float max_range;
    float power;  /* 功耗 mA */
    sensor_config_t config;
    sensor_data_t last_data;
    int (*read)(sensor_data_t *data);
    int (*enable)(int enable);
    int (*set_rate)(uint32_t rate);
    int initialized;
} sensor_device_t;

/* 初始化传感器子系统 */
void sensors_init(void);

/* 注册传感器 */
int sensors_register(sensor_device_t *sensor);

/* 读取传感器数据 */
int sensors_read(int sensor_id, sensor_data_t *data);
int sensors_sim_write(int sensor_id, float x, float y, float z, float value, int accuracy);
int sensors_sim_demo(void);

/* 启用/禁用传感器 */
int sensors_enable(int sensor_id, int enable);

/* 设置采样率 */
int sensors_set_rate(int sensor_id, uint32_t rate);

/* 获取传感器信息 */
int sensors_get_info(int sensor_id, sensor_device_t *info);

/* 按类型查找传感器 */
int sensors_find_by_type(sensor_type_t type);

/* 获取传感器数量 */
int sensors_count(void);

/* 获取活跃传感器数量 */
int sensors_active_count(void);

/* 传感器轮询 (由定时器调用) */
void sensors_poll(void);

/* 加速度计: 获取重力向量 */
int sensors_get_gravity(float *x, float *y, float *z);

/* 陀螺仪: 获取角速度 */
int sensors_get_rotation(float *x, float *y, float *z);

/* 磁力计: 获取磁场 */
int sensors_get_magnetic(float *x, float *y, float *z);

/* 光线: 获取照度 */
int sensors_get_light(float *lux);

/* 距离: 获取距离 */
int sensors_get_proximity(float *cm);

/* 气压计: 获取气压 */
int sensors_get_pressure(float *hpa);

/* 温度: 获取温度 */
int sensors_get_temperature(float *celsius);

/* 计算方向 (旋转矩阵 -> 方位角/俯仰角/横滚角) */
int sensors_get_orientation(float *azimuth, float *pitch, float *roll);

/* 步数计数 */
int sensors_get_step_count(void);

/* 传感器中断处理 */
void sensors_handle_irq(void);

#endif /* SENSORS_H */
