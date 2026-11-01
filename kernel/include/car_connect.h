#ifndef CAR_CONNECT_H
#define CAR_CONNECT_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 车机互联
 * 赫普Car车机投射 / 无线有线连接 / 车机应用生态 / 方向盘语音控制
 * ========================================================================== */

#define CAR_MAX_VEHICLES 16
#define CAR_MAX_APPS 64
#define CAR_MAX_CONTROLS 32
#define CAR_NAME_LEN 128

/* 连接类型 */
typedef enum {
    CAR_CONN_USB = 0,
    CAR_CONN_WIFI,
    CAR_CONN_BLUETOOTH,
    CAR_CONN_5G,
    CAR_CONN_NFC,
} car_conn_type_t;

/* 连接状态 */
typedef enum {
    CAR_STATE_DISCONNECTED = 0,
    CAR_STATE_PAIRING,
    CAR_STATE_CONNECTING,
    CAR_STATE_CONNECTED,
    CAR_STATE_PROJECTING,
    CAR_STATE_ERROR,
} car_state_t;

/* 投射模式 */
typedef enum {
    CAR_PROJ_FULL = 0,      /* 全屏投射 */
    CAR_PROJ_SPLIT,         /* 分屏 */
    CAR_PROJ_PICTURE,       /* 画中画 */
    CAR_PROJ_CLUSTER,       /* 仪表盘投射 */
    CAR_PROJ_HUD,           /* HUD投射 */
} car_proj_mode_t;

/* 车辆类型 */
typedef enum {
    CAR_VEH_ICE = 0,        /* 燃油车 */
    CAR_VEH_HEV,            /* 混动 */
    CAR_VEH_PHEV,           /* 插混 */
    CAR_VEH_BEV,            /* 纯电 */
    CAR_VEH_FCEV,           /* 燃料电池 */
} car_vehicle_type_t;

/* 车辆数据 */
typedef struct {
    int used;
    int id;
    char name[CAR_NAME_LEN];
    char vin[20];
    char brand[64];
    char model[64];
    int year;
    car_vehicle_type_t type;
    /* 连接 */
    car_conn_type_t conn_type;
    car_state_t state;
    car_proj_mode_t proj_mode;
    /* 车辆状态 */
    float speed_kmh;
    float rpm;
    float fuel_level;    /* 0-100% */
    float battery_level; /* 0-100% */
    float range_km;
    float odometer_km;
    float tire_pressure[4];
    float coolant_temp;
    int gear;  /* 0=P,1=R,2=N,3=D,4=S */
    int doors_open;
    int lights_on;
    /* 位置 */
    float latitude;
    float longitude;
    float heading;
    /* 媒体 */
    char now_playing[256];
    int volume;
    int is_playing;
} car_vehicle_t;

/* 车机应用 */
typedef struct {
    int used;
    int id;
    char name[CAR_NAME_LEN];
    char package[128];
    int category;  /* 0=导航,1=音乐,2=电话,3=消息,4=设置,5=其他 */
    int car_optimized;  /* 车机适配 */
    int driver_safe;    /* 驾驶安全（行驶中可用） */
    int min_speed_limit; /* 最低速度限制 */
    int installed;
    int running;
} car_app_t;

/* 方向盘控制 */
typedef enum {
    CAR_CTRL_VOLUME_UP = 0,
    CAR_CTRL_VOLUME_DOWN,
    CAR_CTRL_VOICE,
    CAR_CTRL_ANSWER,
    CAR_CTRL_HANGUP,
    CAR_CTRL_NEXT,
    CAR_CTRL_PREV,
    CAR_CTRL_PLAY_PAUSE,
    CAR_CTRL_MUTE,
    CAR_CTRL_BACK,
    CAR_CTRL_HOME,
    CAR_CTRL_OK,
} car_control_t;

/* 控制事件 */
typedef struct {
    car_control_t control;
    int pressed;
    int long_press;
    uint64_t timestamp;
} car_control_event_t;

/* 语音命令 */
typedef struct {
    char command[256];
    char intent[64];
    char entity[128];
    float confidence;
    int handled;
} car_voice_cmd_t;

/* 车机配置 */
typedef struct {
    int auto_connect;
    int auto_project;
    car_proj_mode_t default_proj;
    int driver_mode;     /* 驾驶模式限制 */
    int night_mode;
    int large_text;
    int high_contrast;
    int voice_control;
    int steering_wheel_controls;
    int notification_filter;
} car_config_t;

/* 车机互联状态 */
typedef struct {
    int initialized;
    int enabled;
    car_config_t config;
    /* 车辆 */
    car_vehicle_t vehicles[CAR_MAX_VEHICLES];
    int vehicle_count;
    int active_vehicle;
    /* 应用 */
    car_app_t apps[CAR_MAX_APPS];
    int app_count;
    /* 控制事件 */
    car_control_event_t events[CAR_MAX_CONTROLS];
    int event_count;
    int event_pos;
    /* 统计 */
    uint64_t total_connect_time;
    uint32_t connect_count;
    uint64_t projected_time;
    uint32_t voice_commands;
    uint32_t control_events;
} car_connect_t;

/* 初始化 */
void car_connect_init(void);

/* 连接管理 */
int car_pair(car_conn_type_t type, const char *device_id);
int car_connect(int vehicle_id);
int car_disconnect(int vehicle_id);
int car_get_state(car_state_t *state);
int car_get_vehicle(int id, car_vehicle_t *veh);
int car_list_vehicles(car_vehicle_t *vehs, int max);
int car_get_active(car_vehicle_t *veh);

/* 投射 */
int car_project_start(car_proj_mode_t mode);
int car_project_stop(void);
int car_project_set_mode(car_proj_mode_t mode);
int car_project_get_mode(car_proj_mode_t *mode);

/* 车辆数据 */
int car_update_vehicle_data(int vehicle_id, const car_vehicle_t *data);
int car_get_vehicle_data(int vehicle_id, car_vehicle_t *data);
int car_get_speed(float *speed);
int car_get_fuel(float *fuel);
int car_get_battery(float *battery);
int car_get_range(float *range);
int car_get_location(float *lat, float *lon, float *heading);

/* 车机应用 */
int car_app_register(const char *name, const char *package, int category,
                     int car_optimized, int driver_safe);
int car_app_unregister(int app_id);
int car_app_launch(int app_id);
int car_app_close(int app_id);
int car_app_list(car_app_t *apps, int max);
int car_app_get(int app_id, car_app_t *app);

/* 方向盘控制 */
int car_control_send(car_control_t control, int pressed, int long_press);
int car_control_poll(car_control_event_t *event);
int car_control_register_handler(car_control_t control, void (*handler)(int));

/* 语音控制 */
int car_voice_start(void);
int car_voice_stop(void);
int car_voice_process(const char *text, car_voice_cmd_t *cmd);
int car_voice_tts(const char *text);

/* 通知 */
int car_notification_show(const char *title, const char *text, int type);
int car_notification_dismiss(int id);

/* 配置 */
int car_set_config(const car_config_t *config);
int car_get_config(car_config_t *config);
int car_set_driver_mode(int enable);
int car_set_night_mode(int enable);
int car_set_voice_control(int enable);

/* 统计 */
int car_get_stats(car_connect_t *stats);

#endif /* CAR_CONNECT_H */
