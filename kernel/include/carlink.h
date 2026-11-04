#ifndef CARLINK_H
#define CARLINK_H

#include <stdint.h>

#define CARLINK_MAX_VEHICLES 8
#define CARLINK_MAX_APPS 32
#define CARLINK_MAX_MESSAGES 16
#define CARLINK_MAX_CONTACTS 64

/* 连接类型 */
typedef enum {
    CARLINK_USB = 0,
    CARLINK_BLUETOOTH,
    CARLINK_WIFI,
    CARLINK_CLOUD,
} carlink_type_t;

/* 车机协议 */
typedef enum {
    CARLINK_CARPLAY = 0,
    CARLINK_ANDROID_AUTO,
    CARLINK_HICAR,  /* 华为HiCar */
    CARLINK_CARLIFE,  /* 百度CarLife */
    CARLINK_HEPU_LINK,  /* 赫普原生互联 */
    CARLINK_MIRROR,  /* 镜像投屏 */
} carlink_protocol_t;

/* 车辆状态 */
typedef enum {
    VEHICLE_STATE_OFF = 0,
    VEHICLE_STATE_ACCESSORY,
    VEHICLE_STATE_IGNITION_ON,
    VEHICLE_STATE_RUNNING,
    VEHICLE_STATE_CHARGING,
    VEHICLE_STATE_REMOTE_START,
} vehicle_state_t;

/* 车辆信息 */
typedef struct {
    int used;
    int id;
    char name[64];
    char vin[32];  /* 车辆识别码 */
    char make[32];  /* 品牌 */
    char model[32];  /* 型号 */
    int year;
    char license_plate[16];
    /* 连接 */
    carlink_type_t link_type;
    carlink_protocol_t protocol;
    int connected;
    int paired;
    /* 状态 */
    vehicle_state_t state;
    int fuel_level;  /* 百分比 */
    int battery_level;  /* 电动车百分比 */
    int range_km;  /* 续航里程 */
    float odometer_km;  /* 总里程 */
    float tire_pressure[4];  /* 胎压 bar */
    int door_locked;
    int engine_on;
    int lights_on;
    int parking_brake;
    /* 位置 */
    double latitude;
    double longitude;
    float altitude;
    float heading;
    float speed_kmh;
    /* 温度 */
    float outside_temp;
    float inside_temp;
    float target_temp;
    int ac_on;
    int heater_on;
    int seat_heat[2];  /* 座椅加热 0-3 */
    /* 媒体 */
    char media_title[128];
    char media_artist[64];
    int media_playing;
    int media_position;
    int media_duration;
    int volume;
} vehicle_info_t;

/* 蓝牙钥匙 */
typedef struct {
    int enabled;
    int paired;
    char key_id[32];
    char vehicle_vin[32];
    int key_type;  /* 0=标准,1=数字钥匙,2=NFC,3=UWB */
    int authenticated;
    int in_range;
    float distance_m;
    int rssi;
    int passive_entry;  /* 无钥匙进入 */
    int remote_start;
    int trunk_release;
    int panic_alarm;
    int validity_days;
    uint64_t last_used;
} ble_key_t;

/* 车机应用 */
typedef struct {
    int used;
    int id;
    char name[64];
    char bundle_id[128];
    int category;  /* 0=导航,1=音乐,2=通话,3=消息,4=播客,5=有声书,6=停车,7=充电,8=其他 */
    int installed;
    int running;
    int icon_id;
} carlink_app_t;

/* 车机消息 */
typedef struct {
    int used;
    int id;
    char sender[64];
    char message[512];
    uint64_t timestamp;
    int read;
    int type;  /* 0=SMS,1=微信,2=WhatsApp,3=其他 */
    int can_reply;
} carlink_message_t;

/* 联系人 */
typedef struct {
    int used;
    int id;
    char name[64];
    char phone[32];
    int favorite;
    int recent_calls;
} carlink_contact_t;

/* 导航信息 */
typedef struct {
    int active;
    char destination[128];
    char current_road[128];
    char next_turn[128];
    int distance_to_turn_m;
    int total_distance_m;
    int remaining_distance_m;
    int eta_seconds;
    int current_speed_limit;
    int traffic_level;  /* 0=畅通,1=缓行,2=拥堵 */
    int lane_count;
    int recommended_lane;
} carlink_nav_t;

/* 汽车互联状态 */
typedef struct {
    int initialized;
    /* 车辆 */
    vehicle_info_t vehicles[CARLINK_MAX_VEHICLES];
    int vehicle_count;
    int active_vehicle;
    /* 蓝牙钥匙 */
    ble_key_t ble_key;
    /* 应用 */
    carlink_app_t apps[CARLINK_MAX_APPS];
    int app_count;
    /* 消息 */
    carlink_message_t messages[CARLINK_MAX_MESSAGES];
    int message_count;
    /* 联系人 */
    carlink_contact_t contacts[CARLINK_MAX_CONTACTS];
    int contact_count;
    /* 导航 */
    carlink_nav_t navigation;
    /* 连接状态 */
    int connected;
    carlink_type_t link_type;
    carlink_protocol_t protocol;
    int wifi_direct;
    /* 投屏 */
    int mirroring;
    int screen_width;
    int screen_height;
    int screen_dpi;
    int dark_mode;
    /* 统计 */
    uint64_t connect_time;
    int call_count;
    int message_count_total;
    int navigation_sessions;
} carlink_t;

/* 初始化汽车互联 */
void carlink_init(void);

/* 连接管理 */
int carlink_connect(carlink_type_t type, carlink_protocol_t protocol);
int carlink_disconnect(void);
int carlink_is_connected(void);
int carlink_get_protocol(void);

/* 车辆管理 */
int carlink_add_vehicle(vehicle_info_t *vehicle);
int carlink_remove_vehicle(int vehicle_id);
int carlink_set_active_vehicle(int vehicle_id);
int carlink_get_vehicle(int vehicle_id, vehicle_info_t *vehicle);
int carlink_list_vehicles(vehicle_info_t *vehicles, int max);
int carlink_update_vehicle_status(int vehicle_id, vehicle_info_t *status);

/* 蓝牙钥匙 */
int carlink_ble_key_enable(int enable);
int carlink_ble_key_pair(const char *vin);
int carlink_ble_key_unpair(void);
int carlink_ble_key_authenticate(void);
int carlink_ble_key_get_status(ble_key_t *key);
int carlink_ble_key_lock(int lock);
int carlink_ble_key_start_engine(int start);
int carlink_ble_key_open_trunk(void);
int carlink_ble_key_honk_flash(void);
int carlink_ble_key_set_climate(int ac, float temp);

/* 车机应用 */
int carlink_app_launch(int app_id);
int carlink_app_close(int app_id);
int carlink_app_list(carlink_app_t *apps, int max);
int carlink_app_get(int app_id, carlink_app_t *app);

/* 消息 */
int carlink_message_receive(carlink_message_t *msg);
int carlink_message_list(carlink_message_t *messages, int max);
int carlink_message_reply(int message_id, const char *text);
int carlink_message_read(int message_id);

/* 联系人 */
int carlink_contact_add(carlink_contact_t *contact);
int carlink_contact_remove(int contact_id);
int carlink_contact_list(carlink_contact_t *contacts, int max);
int carlink_contact_call(int contact_id);

/* 导航 */
int carlink_nav_start(const char *destination, double lat, double lng);
int carlink_nav_stop(void);
int carlink_nav_get_status(carlink_nav_t *nav);
int carlink_nav_is_active(void);

/* 媒体控制 */
int carlink_media_play(void);
int carlink_media_pause(void);
int carlink_media_next(void);
int carlink_media_prev(void);
int carlink_media_seek(int position_ms);
int carlink_media_set_volume(int volume);
int carlink_media_get_info(char *title, int title_size, char *artist, int artist_size, int *position, int *duration);

/* 通话 */
int carlink_call_start(const char *number);
int carlink_call_answer(void);
int carlink_call_end(void);
int carlink_call_mute(int mute);
int carlink_call_speaker(int speaker);

/* 投屏 */
int carlink_mirror_start(int width, int height, int dpi);
int carlink_mirror_stop(void);
int carlink_mirror_is_active(void);
int carlink_set_dark_mode(int dark);

/* 统计 */
int carlink_get_stats(uint64_t *connect_time, int *calls, int *messages, int *nav_sessions);

#endif /* CARLINK_H */
