#ifndef DRIVERS_REAL_H
#define DRIVERS_REAL_H

#include <stdint.h>

#define DRV_R_MAX_KEYS 256
#define DRV_R_MAX_MOUSE_EVENTS 64
#define DRV_R_MAX_DISK_PARTITIONS 16
#define DRV_R_MAX_NET_PACKETS 32

/* VGA模式 */
typedef enum {
    VGA_MODE_TEXT_80x25 = 0,
    VGA_MODE_TEXT_80x50,
    VGA_MODE_GRAPHICS_320x200,
    VGA_MODE_GRAPHICS_640x480,
    VGA_MODE_GRAPHICS_800x600,
    VGA_MODE_GRAPHICS_1024x768,
    VGA_MODE_GRAPHICS_1280x1024,
} vga_mode_t;

/* VGA信息 */
typedef struct {
    vga_mode_t mode;
    int width;
    int height;
    int bpp;  /* 位深 */
    uint64_t framebuffer_phys;
    void *framebuffer_virt;
    int text_cols;
    int text_rows;
    int cursor_x;
    int cursor_y;
    int cursor_enabled;
    int blink_enabled;
    int font_height;
    int font_width;
    int memory_size_kb;
} vga_info_t;

/* 键盘状态 */
typedef struct {
    int caps_lock;
    int num_lock;
    int scroll_lock;
    int shift;
    int ctrl;
    int alt;
    int super;  /* Windows键 */
    int altgr;
    int keys[DRV_R_MAX_KEYS];  /* 按键状态 */
    int last_key;
    int last_scancode;
    uint64_t last_key_time;
    int repeat_rate;
    int repeat_delay;
    int layout;  /* 0=US,1=UK,2=DE,3=FR,4=JP,5=CN */
} drv_r_keyboard_state_t;

/* 键盘事件 */
typedef struct {
    int scancode;
    int keycode;
    int pressed;  /* 0=释放,1=按下 */
    char ascii;
    int modifiers;
    uint64_t timestamp;
} keyboard_event_t;

/* 鼠标状态 */
typedef struct {
    int x;
    int y;
    int buttons;  /* 位0=左键,1=右键,2=中键 */
    int wheel;
    int sensitivity;
    int acceleration;
    int left_pressed;
    int right_pressed;
    int middle_pressed;
    int last_x;
    int last_y;
} drv_r_mouse_state_t;

/* 鼠标事件 */
typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    int buttons;
    int wheel;
    uint64_t timestamp;
} mouse_event_t;

/* 硬盘类型 */
typedef enum {
    DISK_TYPE_IDE = 0,
    DISK_TYPE_SATA,
    DISK_TYPE_NVME,
    DISK_TYPE_USB,
    DISK_TYPE_VIRTIO,
    DISK_TYPE_RAMDISK,
} disk_type_t;

/* 硬盘分区 */
typedef struct {
    int used;
    int id;
    int number;
    uint64_t start_lba;
    uint64_t sector_count;
    uint64_t size;
    int type;  /* 分区类型 */
    int bootable;
    char filesystem[32];
    char label[64];
    char uuid[32];
    int mounted;
    char mount_point[256];
} disk_partition_t;

/* 硬盘信息 */
typedef struct {
    int used;
    int id;
    char name[32];  /* 如 /dev/sda */
    disk_type_t type;
    uint64_t total_sectors;
    uint64_t sector_size;
    uint64_t size;
    int cylinders;
    int heads;
    int sectors_per_track;
    int lba48_supported;
    int dma_supported;
    int ncq_supported;
    int trim_supported;
    char model[64];
    char serial[32];
    char firmware[32];
    int rpm;  /* 转速, SSD=0 */
    int temperature;
    int smart_supported;
    int smart_enabled;
    disk_partition_t partitions[DRV_R_MAX_DISK_PARTITIONS];
    int partition_count;
    uint64_t read_count;
    uint64_t write_count;
    uint64_t read_bytes;
    uint64_t write_bytes;
} disk_info_t;

/* 网卡类型 */
typedef enum {
    NET_TYPE_ETHERNET = 0,
    NET_TYPE_WIFI,
    NET_TYPE_CELLULAR,
    NET_TYPE_VIRTIO,
    NET_TYPE_USB,
} net_type_t;

/* 网卡信息 */
typedef struct {
    int used;
    int id;
    char name[32];  /* 如 eth0 */
    net_type_t type;
    uint8_t mac[6];
    int mtu;
    int speed_mbps;
    int duplex;  /* 0=half,1=full */
    int auto_neg;
    int link_up;
    char driver_name[64];
    char vendor[64];
    char model[64];
    int rx_ring_size;
    int tx_ring_size;
    int rx_queue_count;
    int tx_queue_count;
    int checksum_offload;
    int tso_supported;
    int gso_supported;
    int vlan_offload;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
} netcard_info_t;

/* 网络包 */
typedef struct {
    uint8_t data[1514];
    int length;
    uint64_t timestamp;
    int protocol;
} net_packet_t;

/* PS/2控制器 */
typedef struct {
    int initialized;
    int port1_enabled;  /* 键盘 */
    int port2_enabled;  /* 鼠标 */
    int translation;  /* 扫描码转换 */
} ps2_controller_t;

/* 驱动真实化状态 */
typedef struct {
    int initialized;
    /* VGA */
    vga_info_t vga;
    /* 键盘 */
    drv_r_keyboard_state_t keyboard;
    keyboard_event_t key_buffer[256];
    int key_buffer_head;
    int key_buffer_tail;
    int key_buffer_count;
    /* 鼠标 */
    drv_r_mouse_state_t mouse;
    mouse_event_t mouse_buffer[DRV_R_MAX_MOUSE_EVENTS];
    int mouse_buffer_head;
    int mouse_buffer_tail;
    int mouse_buffer_count;
    /* PS/2 */
    ps2_controller_t ps2;
    /* 硬盘 */
    disk_info_t disks[4];
    int disk_count;
    /* 网卡 */
    netcard_info_t netcards[4];
    int netcard_count;
    /* 统计 */
    uint64_t interrupts;
    uint64_t io_ports;
    uint64_t mmio_regions;
} drivers_real_t;

/* 初始化驱动真实化 */
void drivers_real_init(void);

/* VGA */
int drv_r_vga_set_mode(vga_mode_t mode);
int drv_r_vga_get_mode(vga_info_t *info);
int drv_r_vga_putc(int x, int y, char c, int color);
int drv_r_vga_puts(int x, int y, const char *s, int color);
int drv_r_vga_clear(int color);
int drv_r_vga_set_cursor(int x, int y);
int drv_r_vga_get_cursor(int *x, int *y);
int drv_r_vga_scroll(int lines);
int drv_r_vga_set_palette(int index, int r, int g, int b);

/* 键盘 */
int drv_r_keyboard_init(void);
int drv_r_keyboard_get_key(keyboard_event_t *event);
int drv_r_keyboard_get_state(drv_r_keyboard_state_t *state);
int drv_r_keyboard_set_layout(int layout);
int drv_r_keyboard_get_char(void);
int drv_r_keyboard_key_pressed(int keycode);
int drv_r_keyboard_set_leds(int caps, int num, int scroll);

/* 鼠标 */
int drv_r_mouse_init(void);
int drv_r_mouse_get_event(mouse_event_t *event);
int drv_r_mouse_get_state(drv_r_mouse_state_t *state);
int drv_r_mouse_set_sensitivity(int sensitivity);
int drv_r_mouse_set_position(int x, int y);

/* PS/2 */
int drv_r_ps2_init(void);
int drv_r_ps2_get_status(ps2_controller_t *ps2);
int drv_r_ps2_write(int port, uint8_t data);
int drv_r_ps2_read(int port, uint8_t *data);

/* 硬盘 */
int drv_r_disk_init(void);
int drv_r_disk_list(disk_info_t *disks, int max);
int drv_r_disk_get(int disk_id, disk_info_t *disk);
int drv_r_disk_read(int disk_id, uint64_t lba, int count, void *buffer);
int drv_r_disk_write(int disk_id, uint64_t lba, int count, const void *buffer);
int drv_r_disk_flush(int disk_id);
int drv_r_disk_trim(int disk_id, uint64_t lba, int count);
int drv_r_disk_get_partitions(int disk_id, disk_partition_t *parts, int max);

/* 网卡 */
int drv_r_net_init(void);
int drv_r_net_list(netcard_info_t *cards, int max);
int drv_r_net_get(int card_id, netcard_info_t *card);
int drv_r_net_send(int card_id, const void *data, int length);
int drv_r_net_receive(int card_id, void *buffer, int max_length);
int drv_r_net_set_promiscuous(int card_id, int enable);
int drv_r_net_set_mtu(int card_id, int mtu);

/* 统计 */
int drv_r_get_stats(drivers_real_t *stats);

#endif /* DRIVERS_REAL_H */
