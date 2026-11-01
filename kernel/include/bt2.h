#ifndef BT2_H
#define BT2_H

#include <stdint.h>

#define BT2_MAX_DEVICES 32
#define BT2_MAX_PROFILES 16
#define BT2_MAX_GATT_SERVICES 64
#define BT2_MAX_GATT_CHARS 128
#define BT2_MAX_MESH_NODES 128
#define BT2_MAX_CODECS 8

/* 蓝牙设备类型 */
typedef enum {
    BT_DEVICE_UNKNOWN = 0,
    BT_DEVICE_PHONE,
    BT_DEVICE_COMPUTER,
    BT_DEVICE_HEADPHONE,
    BT_DEVICE_HEADSET,
    BT_DEVICE_SPEAKER,
    BT_DEVICE_WATCH,
    BT_DEVICE_KEYBOARD,
    BT_DEVICE_MOUSE,
    BT_DEVICE_GAMEPAD,
    BT_DEVICE_CAR,
    BT_DEVICE_IOT,
    BT_DEVICE_BEACON,
} bt2_device_type_t;

/* 蓝牙配置文件 */
typedef enum {
    BT_PROFILE_A2DP = 0,    /* 高级音频分发 */
    BT_PROFILE_AVRCP,       /* 音视频远程控制 */
    BT_PROFILE_HFP,         /* 免提 */
    BT_PROFILE_HSP,         /* 耳机 */
    BT_PROFILE_GATT,        /* 通用属性 */
    BT_PROFILE_SPP,         /* 串口 */
    BT_PROFILE_OBEX,        /* 对象交换 */
    BT_PROFILE_PBAP,        /* 电话本访问 */
    BT_PROFILE_MAP,         /* 消息访问 */
    BT_PROFILE_HID,         /* 人机接口 */
    BT_PROFILE_PAN,         /* 个人局域网 */
    BT_PROFILE_BLE_MESH,    /* BLE Mesh */
} bt_profile_t;

/* 音频编解码器 */
typedef enum {
    BT_CODEC_SBC = 0,
    BT_CODEC_AAC,
    BT_CODEC_APTX,
    BT_CODEC_APTX_HD,
    BT_CODEC_APTX_ADAPTIVE,
    BT_CODEC_LDAC,
    BT_CODEC_LC3,
    BT_CODEC_OPUS,
} bt_codec_t;

/* 蓝牙设备 */
typedef struct {
    int used;
    int id;
    uint8_t address[6];
    char name[128];
    bt2_device_type_t type;
    int paired;
    int connected;
    int trusted;
    int blocked;
    int rssi;  /* 信号强度 dBm */
    int tx_power;
    uint16_t class_of_device;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version;
    /* 支持的配置文件 */
    int profiles[BT2_MAX_PROFILES];
    int profile_count;
    /* 支持的编解码器 */
    bt_codec_t codecs[BT2_MAX_CODECS];
    int codec_count;
    bt_codec_t current_codec;
    /* 连接时间 */
    uint64_t last_seen;
    uint64_t connect_time;
    /* 电池 */
    int battery_level;
    int has_battery;
} bt2_device_t;

/* A2DP状态 */
typedef struct {
    int streaming;
    int suspended;
    bt_codec_t codec;
    int sample_rate;
    int bits_per_sample;
    int channels;
    int bitrate;
    int latency_ms;
    uint64_t bytes_sent;
    uint64_t packets_sent;
    int retransmissions;
} a2dp_state_t;

/* AVRCP状态 */
typedef struct {
    int connected;
    int volume;
    int max_volume;
    int play_state;  /* 0=停止,1=播放,2=暂停,3=快进,4=快退 */
    uint64_t track_position;
    uint64_t track_duration;
    char track_title[128];
    char track_artist[128];
    char track_album[128];
    int track_number;
    int total_tracks;
    int repeat_mode;  /* 0=关,1=全部,2=单曲 */
    int shuffle_mode;
} avrcp_state_t;

/* HFP状态 */
typedef struct {
    int connected;
    int call_active;
    int call_setup;  /* 0=无,1=来电,2=呼出,3=呼出告警 */
    int call_held;
    int signal_strength;
    int roaming;
    int battery_level;
    char operator_name[64];
    char phone_number[32];
    int speaker_volume;
    int microphone_volume;
    int voice_recognition;
    int echo_canceling;
    int noise_reduction;
} hfp_state_t;

/* GATT服务 */
typedef struct {
    int used;
    int id;
    uint16_t handle;
    uint8_t uuid[16];
    int is_primary;
    int characteristic_count;
} gatt_service_t;

/* GATT特征值 */
typedef struct {
    int used;
    int id;
    uint16_t handle;
    uint16_t value_handle;
    uint8_t uuid[16];
    uint8_t properties;  /* 位掩码: 1=广播,2=读,4=写无响应,8=写,16=通知,32=指示 */
    uint8_t permissions;
    uint8_t *value;
    int value_len;
    int service_id;
    int notifying;
} gatt_characteristic_t;

/* BLE Mesh节点 */
typedef struct {
    int used;
    int id;
    uint16_t address;
    uint8_t uuid[16];
    char name[64];
    int element_count;
    int model_count;
    int online;
    int ttl;
    uint8_t dev_key[16];
    uint8_t app_key[16];
    uint8_t net_key[16];
    int seq;
    uint64_t last_seen;
} mesh_node_t;

/* BLE Mesh消息 */
typedef struct {
    uint16_t src;
    uint16_t dst;
    uint32_t opcode;
    uint8_t data[256];
    int data_len;
    int ttl;
    uint32_t seq;
    uint8_t iv_index[4];
} mesh_message_t;

/* 蓝牙深化状态 */
typedef struct {
    int initialized;
    int enabled;
    int discoverable;
    int discoverable_timeout;
    int scanning;
    int connecting;
    uint8_t local_address[6];
    char local_name[64];
    /* 设备 */
    bt2_device_t devices[BT2_MAX_DEVICES];
    int device_count;
    /* 配置文件状态 */
    a2dp_state_t a2dp;
    avrcp_state_t avrcp;
    hfp_state_t hfp;
    /* GATT */
    gatt_service_t gatt_services[BT2_MAX_GATT_SERVICES];
    int gatt_service_count;
    gatt_characteristic_t gatt_chars[BT2_MAX_GATT_CHARS];
    int gatt_char_count;
    /* Mesh */
    mesh_node_t mesh_nodes[BT2_MAX_MESH_NODES];
    int mesh_node_count;
    int mesh_provisioned;
    /* 统计 */
    uint64_t connections_total;
    uint64_t disconnections_total;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t pairing_attempts;
    uint64_t pairing_success;
} bt2_t;

/* 初始化蓝牙深化 */
void bt2_init(void);

/* 设备管理 */
int bt2_enable(int enable);
int bt2_is_enabled(void);
int bt2_set_discoverable(int enable, int timeout);
int bt2_start_scan(void);
int bt2_stop_scan(void);
int bt2_is_scanning(void);
int bt2_get_device(uint8_t *address, bt2_device_t *device);
int bt2_list_devices(bt2_device_t *devices, int max);
int bt2_pair(uint8_t *address);
int bt2_unpair(uint8_t *address);
int bt2_connect(uint8_t *address);
int bt2_disconnect(uint8_t *address);
int bt2_set_trusted(uint8_t *address, int trusted);
int bt2_set_blocked(uint8_t *address, int blocked);
int bt2_get_rssi(uint8_t *address);

/* A2DP */
int a2dp_connect(uint8_t *address);
int a2dp_disconnect(uint8_t *address);
int a2dp_start_streaming(uint8_t *address);
int a2dp_suspend_streaming(uint8_t *address);
int a2dp_set_codec(uint8_t *address, bt_codec_t codec);
int a2dp_get_state(uint8_t *address, a2dp_state_t *state);
int a2dp_get_supported_codecs(uint8_t *address, bt_codec_t *codecs, int max);

/* AVRCP */
int avrcp_connect(uint8_t *address);
int avrcp_disconnect(uint8_t *address);
int avrcp_play(uint8_t *address);
int avrcp_pause(uint8_t *address);
int avrcp_stop(uint8_t *address);
int avrcp_next(uint8_t *address);
int avrcp_previous(uint8_t *address);
int avrcp_set_volume(uint8_t *address, int volume);
int avrcp_get_volume(uint8_t *address);
int avrcp_get_state(uint8_t *address, avrcp_state_t *state);
int avrcp_set_track_info(uint8_t *address, const char *title, const char *artist, const char *album, uint64_t duration);

/* HFP */
int hfp_connect(uint8_t *address);
int hfp_disconnect(uint8_t *address);
int hfp_answer_call(uint8_t *address);
int hfp_reject_call(uint8_t *address);
int hfp_hangup_call(uint8_t *address);
int hfp_dial_number(uint8_t *address, const char *number);
int hfp_voice_dial(uint8_t *address);
int hfp_set_speaker_volume(uint8_t *address, int volume);
int hfp_set_microphone_volume(uint8_t *address, int volume);
int hfp_get_state(uint8_t *address, hfp_state_t *state);

/* GATT */
int gatt_add_service(uint8_t *uuid, int is_primary);
int gatt_add_characteristic(int service_id, uint8_t *uuid, uint8_t properties, uint8_t permissions);
int gatt_read_value(int char_id, uint8_t *value, int max_len);
int gatt_write_value(int char_id, const uint8_t *value, int len);
int gatt_notify(int char_id, const uint8_t *value, int len);
int gatt_set_notify(int char_id, int enable);
int gatt_discover_services(uint8_t *address);
int gatt_read_characteristic(uint8_t *address, uint16_t handle);
int gatt_write_characteristic(uint8_t *address, uint16_t handle, const uint8_t *value, int len);

/* BLE Mesh */
int mesh_provision(uint8_t *uuid, uint16_t address);
int mesh_unprovision(uint16_t address);
int mesh_send_message(mesh_message_t *msg);
int mesh_get_node(uint16_t address, mesh_node_t *node);
int mesh_list_nodes(mesh_node_t *nodes, int max);
int mesh_set_node_state(uint16_t address, int online);

/* 统计 */
int bt2_get_stats(uint64_t *connections, uint64_t *bytes_sent, uint64_t *bytes_recv);
int bt2_get_connected_count(void);

#endif /* BT2_H */
