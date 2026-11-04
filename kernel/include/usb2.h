#ifndef USB2_H
#define USB2_H

#include <stdint.h>

#define USB2_MAX_DEVICES 8
#define USB2_MAX_INTERFACES 4
#define USB2_MAX_ENDPOINTS 8
#define USB2_MAX_CONFIGS 2
#define USB2_MAX_ALT_SETTINGS 8

/* USB角色 */
typedef enum {
    USB_ROLE_NONE = 0,
    USB_ROLE_HOST,
    USB_ROLE_DEVICE,
    USB_ROLE_OTG,
} usb_role_t;

/* USB速度 */
typedef enum {
    USB_SPEED_LOW = 0,     /* 1.5 Mbps */
    USB_SPEED_FULL,        /* 12 Mbps */
    USB_SPEED_HIGH,        /* 480 Mbps */
    USB_SPEED_SUPER,       /* 5 Gbps */
    USB_SPEED_SUPER_PLUS,  /* 10 Gbps */
} usb_speed_t;

/* USB设备类 */
typedef enum {
    USB2_CLASS_PER_INTERFACE = 0,
    USB2_CLASS_AUDIO = 1,
    USB2_CLASS_COMM = 2,
    USB2_CLASS_HID = 3,
    USB2_CLASS_PHYSICAL = 5,
    USB2_CLASS_STILL_IMAGE = 6,
    USB2_CLASS_PRINTER = 7,
    USB_CLASS_MASS_STORAGE = 8,
    USB2_CLASS_HUB = 9,
    USB2_CLASS_CDC_DATA = 10,
    USB2_CLASS_CSCID = 11,
    USB2_CLASS_CONTENT_SEC = 13,
    USB2_CLASS_VIDEO = 14,
    USB2_CLASS_HEALTHCARE = 15,
    USB2_CLASS_AV = 16,
    USB2_CLASS_BILLBOARD = 17,
    USB2_CLASS_TYPEC_BRIDGE = 18,
    USB2_CLASS_MISC = 0xEF,
    USB2_CLASS_APP_SPEC = 0xFE,
    USB2_CLASS_VENDOR_SPEC = 0xFF,
} usb_class_t;

/* USB端点类型 */
typedef enum {
    USB2_EP_CONTROL = 0,
    USB2_EP_ISOCHRONOUS,
    USB2_EP_BULK,
    USB2_EP_INTERRUPT,
} usb_ep_type_t;

/* USB端点方向 */
typedef enum {
    USB2_EP_OUT = 0,
    USB2_EP_IN = 1,
} usb_ep_dir_t;

/* USB端点 */
typedef struct {
    int used;
    uint8_t address;
    usb_ep_type_t type;
    usb_ep_dir_t direction;
    uint16_t max_packet_size;
    uint8_t interval;
    int halted;
    uint32_t total_transfers;
    uint64_t bytes_transferred;
} usb2_endpoint_t;

/* USB接口 */
typedef struct {
    int used;
    uint8_t interface_number;
    uint8_t alt_setting;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    char description[128];
    usb2_endpoint_t endpoints[USB2_MAX_ENDPOINTS];
    int endpoint_count;
} usb2_interface_t;

/* USB配置 */
typedef struct {
    int used;
    uint8_t config_value;
    char description[128];
    int self_powered;
    int remote_wakeup;
    uint16_t max_power;  /* mA */
    usb2_interface_t interfaces[USB2_MAX_INTERFACES];
    int interface_count;
} usb2_config_t;

/* USB设备 */
typedef struct {
    int used;
    int id;
    uint8_t address;
    usb_speed_t speed;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t manufacturer[128];
    uint8_t product[128];
    uint8_t serial_number[128];
    usb2_config_t configs[USB2_MAX_CONFIGS];
    int config_count;
    int active_config;
    int configured;
    int suspended;
    uint64_t connect_time;
    int port;
    int hub_id;
} usb2_device_t;

/* USB传输类型 */
typedef enum {
    USB_TRANSFER_CONTROL = 0,
    USB_TRANSFER_BULK,
    USB_TRANSFER_INTERRUPT,
    USB_TRANSFER_ISOCHRONOUS,
} usb2_transfer_type_t;

/* USB传输 */
typedef struct {
    int id;
    int device_id;
    uint8_t endpoint;
    usb2_transfer_type_t type;
    usb_ep_dir_t direction;
    uint8_t *buffer;
    int length;
    int actual_length;
    int status;  /* 0=成功,1=失败,2=stall,3=超时 */
    uint32_t timeout_ms;
    void (*callback)(int transfer_id, int status, int actual_length, void *arg);
    void *callback_arg;
} usb2_transfer_t;

/* ADB状态 */
typedef struct {
    int enabled;
    int connected;
    int authorized;
    char device_serial[64];
    uint64_t last_command;
    int transport;  /* 0=USB,1=WiFi */
} adb_state_t;

/* MTP状态 */
typedef struct {
    int enabled;
    int connected;
    int storage_count;
    char storage_names[8][64];
    uint64_t total_storage[8];
    uint64_t free_storage[8];
    int file_transfers;
    uint64_t bytes_transferred;
} mtp_state_t;

/* USB网卡状态 */
typedef struct {
    int enabled;
    int connected;
    uint8_t mac_address[6];
    int link_speed;  /* Mbps */
    int duplex;  /* 0=半双工,1=全双工 */
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    int errors;
} usb_ethernet_t;

/* USB深化状态 */
typedef struct {
    int initialized;
    usb_role_t role;
    int host_controller_count;
    int otg_supported;
    /* 设备 */
    usb2_device_t devices[USB2_MAX_DEVICES];
    int device_count;
    /* 功能 */
    adb_state_t adb;
    mtp_state_t mtp;
    usb_ethernet_t ethernet;
    /* 统计 */
    uint64_t total_transfers;
    uint64_t total_bytes;
    uint64_t errors;
    uint64_t resets;
} usb2_t;

/* 初始化USB深化 */
void usb2_init(void);

/* 角色管理 */
int usb2_set_role(usb_role_t role);
usb_role_t usb2_get_role(void);
int usb2_otg_supported(void);

/* 设备管理 */
int usb2_get_device(int device_id, usb2_device_t *device);
int usb2_list_devices(usb2_device_t *devices, int max);
int usb2_reset_device(int device_id);
int usb2_suspend_device(int device_id);
int usb2_resume_device(int device_id);
int usb2_get_device_count(void);

/* 传输 */
int usb2_control_transfer(int device_id, uint8_t request_type, uint8_t request,
                          uint16_t value, uint16_t index, uint8_t *data, uint16_t length, uint32_t timeout);
int usb2_bulk_transfer(int device_id, uint8_t endpoint, uint8_t *data, int length, int *actual, uint32_t timeout);
int usb2_interrupt_transfer(int device_id, uint8_t endpoint, uint8_t *data, int length, int *actual, uint32_t timeout);
int usb2_async_transfer(usb2_transfer_t *transfer);
int usb2_cancel_transfer(int transfer_id);

/* 描述符 */
int usb2_get_device_descriptor(int device_id, uint8_t *desc, int len);
int usb2_get_config_descriptor(int device_id, int config, uint8_t *desc, int len);
int usb2_get_string_descriptor(int device_id, int index, uint8_t *desc, int len);

/* ADB */
int adb_enable(int enable);
int adb_is_enabled(void);
int adb_authorize(int authorize);
int adb_is_connected(void);
int adb_send_command(const char *command, char *response, int response_len);
int adb_push_file(const char *local, const char *remote);
int adb_pull_file(const char *remote, const char *local);
int adb_shell(const char *command, char *output, int output_len);

/* MTP */
int mtp_enable(int enable);
int mtp_is_enabled(void);
int mtp_is_connected(void);
int mtp_get_storage_info(int storage_id, uint64_t *total, uint64_t *free);
int mtp_list_files(int storage_id, const char *path, char *files, int max);
int mtp_send_file(int storage_id, const char *local, const char *remote);
int mtp_receive_file(int storage_id, const char *remote, const char *local);
int mtp_delete_file(int storage_id, const char *path);

/* USB网卡 */
int usb_ethernet_enable(int enable);
int usb_ethernet_is_enabled(void);
int usb_ethernet_is_connected(void);
int usb_ethernet_get_mac(uint8_t *mac);
int usb_ethernet_get_stats(uint64_t *sent, uint64_t *received, uint64_t *bytes_sent, uint64_t *bytes_received);

/* USB功能切换 */
int usb2_set_function(int function, int enable);
int usb2_get_function(int function);

/* 统计 */
int usb2_get_stats(uint64_t *transfers, uint64_t *bytes, uint64_t *errors);

#endif /* USB2_H */
