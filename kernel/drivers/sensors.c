/* ============================================================================
 * 赫普OS - 传感器框架
 * 加速度计/陀螺仪/磁力计/光线/距离/气压/温度/心率/计步
 * ========================================================================== */

#include <stdint.h>
#include "sensors.h"
#include "vga.h"
#include "string.h"
#include "pit.h"

static sensor_device_t sensors[SENSOR_MAX_DEVICES];
static int sensor_count = 0;
static int sensors_initialized = 0;
static uint32_t step_count = 0;

/* 模拟传感器读取函数 */
static int read_accel(sensor_data_t *data)
{
    /* 模拟: 重力加速度 */
    data->x = 0.0f;
    data->y = 0.0f;
    data->z = 9.81f;
    data->value = 9.81f;
    data->accuracy = 3;
    return 0;
}

static int read_gyro(sensor_data_t *data)
{
    data->x = 0.0f;
    data->y = 0.0f;
    data->z = 0.0f;
    data->value = 0.0f;
    data->accuracy = 2;
    return 0;
}

static int read_mag(sensor_data_t *data)
{
    data->x = 25.0f;
    data->y = 10.0f;
    data->z = 40.0f;
    data->value = 48.0f;
    data->accuracy = 2;
    return 0;
}

static int read_light(sensor_data_t *data)
{
    data->value = 350.0f;  /* lux */
    data->accuracy = 3;
    return 0;
}

static int read_proximity(sensor_data_t *data)
{
    data->value = 5.0f;  /* cm */
    data->accuracy = 2;
    return 0;
}

static int read_pressure(sensor_data_t *data)
{
    data->value = 1013.25f;  /* hPa */
    data->accuracy = 3;
    return 0;
}

static int read_temperature(sensor_data_t *data)
{
    data->value = 25.5f;  /* °C */
    data->accuracy = 2;
    return 0;
}

static int read_heart_rate(sensor_data_t *data)
{
    data->value = 72.0f;  /* BPM */
    data->accuracy = 2;
    return 0;
}

static int read_step_counter(sensor_data_t *data)
{
    data->value = (float)step_count;
    data->accuracy = 3;
    return 0;
}

static int dummy_enable(int enable) { (void)enable; return 0; }
static int dummy_set_rate(uint32_t rate) { (void)rate; return 0; }

void sensors_init(void)
{
    for (int i = 0; i < SENSOR_MAX_DEVICES; i++) {
        sensors[i].used = 0;
    }
    sensor_count = 0;
    sensors_initialized = 1;
    step_count = 0;

    /* 注册模拟传感器 */
    sensor_device_t s;

    memset(&s, 0, sizeof(s));
    s.name = "LSM6DS3 Accelerometer";
    s.type = SENSOR_ACCELEROMETER;
    s.resolution = 0.001f;
    s.max_range = 16.0f;
    s.power = 0.65f;
    s.read = read_accel;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 100;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "LSM6DS3 Gyroscope";
    s.type = SENSOR_GYROSCOPE;
    s.resolution = 0.001f;
    s.max_range = 2000.0f;
    s.power = 0.95f;
    s.read = read_gyro;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 100;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "LIS3MDL Magnetometer";
    s.type = SENSOR_MAGNETOMETER;
    s.resolution = 0.001f;
    s.max_range = 16.0f;
    s.power = 0.2f;
    s.read = read_mag;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 50;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "TSL2561 Light";
    s.type = SENSOR_LIGHT;
    s.resolution = 1.0f;
    s.max_range = 40000.0f;
    s.power = 0.5f;
    s.read = read_light;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 10;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "VCNL4010 Proximity";
    s.type = SENSOR_PROXIMITY;
    s.resolution = 0.1f;
    s.max_range = 20.0f;
    s.power = 0.3f;
    s.read = read_proximity;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 20;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "BMP280 Pressure";
    s.type = SENSOR_PRESSURE;
    s.resolution = 0.01f;
    s.max_range = 1100.0f;
    s.power = 0.03f;
    s.read = read_pressure;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 10;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "BMP280 Temperature";
    s.type = SENSOR_TEMPERATURE;
    s.resolution = 0.01f;
    s.max_range = 85.0f;
    s.power = 0.03f;
    s.read = read_temperature;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 1;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "MAX30102 Heart Rate";
    s.type = SENSOR_HEART_RATE;
    s.resolution = 1.0f;
    s.max_range = 250.0f;
    s.power = 1.5f;
    s.read = read_heart_rate;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 5;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    memset(&s, 0, sizeof(s));
    s.name = "Step Counter";
    s.type = SENSOR_STEP_COUNTER;
    s.resolution = 1.0f;
    s.max_range = 1000000.0f;
    s.power = 0.1f;
    s.read = read_step_counter;
    s.enable = dummy_enable;
    s.set_rate = dummy_set_rate;
    s.config.sampling_rate = 10;
    s.config.enabled = 1;
    s.initialized = 1;
    sensors_register(&s);

    vga_puts("[SENSORS] sensor framework init done \n");
    vga_printf(" registered %d sensors \n", sensor_count);
}

int sensors_register(sensor_device_t *sensor)
{
    if (!sensor || !sensors_initialized) return -1;
    for (int i = 0; i < SENSOR_MAX_DEVICES; i++) {
        if (!sensors[i].used) {
            memcpy(&sensors[i], sensor, sizeof(sensor_device_t));
            sensors[i].used = 1;
            sensor_count++;
            return i;
        }
    }
    return -1;
}

/* V4.34-E79: 传感器模拟注入——无硬件时演示手机传感器数据流 */
int sensors_sim_write(int sensor_id, float x, float y, float z, float value, int accuracy)
{
    if (sensor_id < 0 || sensor_id >= SENSOR_MAX_DEVICES || !sensors[sensor_id].used) return -1;
    sensor_data_t d;
    d.x = x; d.y = y; d.z = z; d.value = value;
    d.accuracy = accuracy;
    d.timestamp = 0;
    memcpy(&sensors[sensor_id].last_data, &d, sizeof(sensor_data_t));
    return 0;
}

int sensors_sim_demo(void)
{
    if (!sensors_initialized) return -1;
    /* 手机平放→倾斜演示: 加速度计 x/y 随"模拟倾斜"变化 */
    static int t = 0;
    t++;
    float ang = (float)(t % 36) * 10.0f * 0.017453f;   /* 0..360 deg -> rad */
    sensors_sim_write(0, 9.8f, 0.0f, 0.0f, 0.0f, 3);   /* accel */
    sensors_sim_write(1, 0.0f, 0.0f, (float)(t % 10), 0.0f, 2); /* gyro z */
    sensors_sim_write(2, (float)(t % 5), 0.0f, 0.0f, 0.0f, 1);   /* mag */
    sensors_sim_write(3, 0.0f, 0.0f, 0.0f, (float)(300 + (t % 50)), 3); /* light */
    sensors_sim_write(4, 0.0f, 0.0f, 0.0f, 1.0f, 0);   /* prox */
    sensors_sim_write(6, 0.0f, 0.0f, 0.0f, 26.0f + (float)(t % 3), 2); /* temp */
    sensors_sim_write(7, 0.0f, 0.0f, 0.0f, 72.0f + (float)(t % 4), 3); /* heart */
    sensors_sim_write(8, 0.0f, 0.0f, 0.0f, (float)(4000 + t * 10), 2); /* steps */
    return 0;
}

int sensors_read(int sensor_id, sensor_data_t *data)
{
    if (sensor_id < 0 || sensor_id >= SENSOR_MAX_DEVICES || !sensors[sensor_id].used) return -1;
    if (!sensors[sensor_id].config.enabled) return -1;
    if (!data) return -1;
    if (sensors[sensor_id].read) {
        int ret = sensors[sensor_id].read(data);
        if (ret == 0) {
            memcpy(&sensors[sensor_id].last_data, data, sizeof(sensor_data_t));
        }
        return ret;
    }
    return -1;
}

int sensors_enable(int sensor_id, int enable)
{
    if (sensor_id < 0 || sensor_id >= SENSOR_MAX_DEVICES || !sensors[sensor_id].used) return -1;
    sensors[sensor_id].config.enabled = enable ? 1 : 0;
    if (sensors[sensor_id].enable) {
        return sensors[sensor_id].enable(enable);
    }
    return 0;
}

int sensors_set_rate(int sensor_id, uint32_t rate)
{
    if (sensor_id < 0 || sensor_id >= SENSOR_MAX_DEVICES || !sensors[sensor_id].used) return -1;
    sensors[sensor_id].config.sampling_rate = rate;
    if (sensors[sensor_id].set_rate) {
        return sensors[sensor_id].set_rate(rate);
    }
    return 0;
}

int sensors_get_info(int sensor_id, sensor_device_t *info)
{
    if (sensor_id < 0 || sensor_id >= SENSOR_MAX_DEVICES || !sensors[sensor_id].used) return -1;
    if (info) memcpy(info, &sensors[sensor_id], sizeof(sensor_device_t));
    return 0;
}

int sensors_find_by_type(sensor_type_t type)
{
    for (int i = 0; i < SENSOR_MAX_DEVICES; i++) {
        if (sensors[i].used && sensors[i].type == type) return i;
    }
    return -1;
}

int sensors_count(void) { return sensor_count; }

int sensors_active_count(void)
{
    int count = 0;
    for (int i = 0; i < SENSOR_MAX_DEVICES; i++) {
        if (sensors[i].used && sensors[i].config.enabled) count++;
    }
    return count;
}

void sensors_poll(void)
{
    /* 原型阶段: 定期读取所有活跃传感器 */
    sensor_data_t data;
    for (int i = 0; i < SENSOR_MAX_DEVICES; i++) {
        if (sensors[i].used && sensors[i].config.enabled) {
            sensors_read(i, &data);
        }
    }
    /* 模拟计步 */
    static int poll_count = 0;
    if (++poll_count % 100 == 0) {
        step_count++;
    }
}

int sensors_get_gravity(float *x, float *y, float *z)
{
    int id = sensors_find_by_type(SENSOR_ACCELEROMETER);
    if (id < 0) return -1;
    sensor_data_t data;
    if (sensors_read(id, &data) != 0) return -1;
    if (x) *x = data.x;
    if (y) *y = data.y;
    if (z) *z = data.z;
    return 0;
}

int sensors_get_rotation(float *x, float *y, float *z)
{
    int id = sensors_find_by_type(SENSOR_GYROSCOPE);
    if (id < 0) return -1;
    sensor_data_t data;
    if (sensors_read(id, &data) != 0) return -1;
    if (x) *x = data.x;
    if (y) *y = data.y;
    if (z) *z = data.z;
    return 0;
}

int sensors_get_magnetic(float *x, float *y, float *z)
{
    int id = sensors_find_by_type(SENSOR_MAGNETOMETER);
    if (id < 0) return -1;
    sensor_data_t data;
    if (sensors_read(id, &data) != 0) return -1;
    if (x) *x = data.x;
    if (y) *y = data.y;
    if (z) *z = data.z;
    return 0;
}

int sensors_get_light(float *lux)
{
    int id = sensors_find_by_type(SENSOR_LIGHT);
    if (id < 0) return -1;
    sensor_data_t data;
    if (sensors_read(id, &data) != 0) return -1;
    if (lux) *lux = data.value;
    return 0;
}

int sensors_get_proximity(float *cm)
{
    int id = sensors_find_by_type(SENSOR_PROXIMITY);
    if (id < 0) return -1;
    sensor_data_t data;
    if (sensors_read(id, &data) != 0) return -1;
    if (cm) *cm = data.value;
    return 0;
}

int sensors_get_pressure(float *hpa)
{
    int id = sensors_find_by_type(SENSOR_PRESSURE);
    if (id < 0) return -1;
    sensor_data_t data;
    if (sensors_read(id, &data) != 0) return -1;
    if (hpa) *hpa = data.value;
    return 0;
}

int sensors_get_temperature(float *celsius)
{
    int id = sensors_find_by_type(SENSOR_TEMPERATURE);
    if (id < 0) return -1;
    sensor_data_t data;
    if (sensors_read(id, &data) != 0) return -1;
    if (celsius) *celsius = data.value;
    return 0;
}

int sensors_get_orientation(float *azimuth, float *pitch, float *roll)
{
    /* 简化: 使用加速度计和磁力计计算方向 */
    float ax, ay, az, mx, my, mz;
    if (sensors_get_gravity(&ax, &ay, &az) != 0) return -1;
    if (sensors_get_magnetic(&mx, &my, &mz) != 0) return -1;

    /* 简化计算 */
    if (azimuth) *azimuth = 0.0f;
    if (pitch) *pitch = 0.0f;
    if (roll) *roll = 0.0f;
    return 0;
}

int sensors_get_step_count(void)
{
    return (int)step_count;
}

void sensors_handle_irq(void)
{
    /* 原型阶段: 传感器数据就绪中断 */
}
