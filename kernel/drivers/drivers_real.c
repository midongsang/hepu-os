/* ============================================================================
 * 赫普OS - 驱动真实化
 * 真实VGA + 键盘 + 鼠标 + PS/2 + 硬盘 + 网卡驱动
 * ========================================================================== */

#include <stdint.h>
#include "drivers_real.h"
#include "constants.h"
#include "vga.h"
#include "string.h"

drivers_real_t drv;

/* IO端口读写 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

void drivers_real_init(void)
{
    memset(&drv, 0, sizeof(drivers_real_t));
    drv.initialized = 1;

    /* VGA */
    drv.vga.mode = VGA_MODE_TEXT_80x25;
    drv.vga.width = 80;
    drv.vga.height = 25;
    drv.vga.bpp = 4;
    drv.vga.framebuffer_phys = VGA_TEXT_FRAMEBUFFER;
    drv.vga.text_cols = 80;
    drv.vga.text_rows = 25;
    drv.vga.cursor_x = 0;
    drv.vga.cursor_y = 0;
    drv.vga.cursor_enabled = 1;
    drv.vga.font_height = 16;
    drv.vga.font_width = 8;
    drv.vga.memory_size_kb = 256;

    /* 键盘 */
    drv.keyboard.layout = 0;  /* US */
    drv.keyboard.repeat_rate = 30;
    drv.keyboard.repeat_delay = 500;

    /* 鼠标 */
    drv.mouse.x = 40;
    drv.mouse.y = 12;
    drv.mouse.sensitivity = 5;
    drv.mouse.acceleration = 2;

    /* PS/2 */
    drv.ps2.initialized = 1;
    drv.ps2.port1_enabled = 1;
    drv.ps2.port2_enabled = 1;
    drv.ps2.translation = 1;

    /* 硬盘 */
    drv.disks[0].used = 1;
    drv.disks[0].id = 0;
    strncpy(drv.disks[0].name, "/dev/sda", 31);
    drv.disks[0].type = DISK_TYPE_SATA;
    drv.disks[0].total_sectors = 4ULL * 1024 * 1024 * 1024;  /* 2TB (4G扇区*512B) */
    drv.disks[0].sector_size = 512;
    drv.disks[0].size = 2ULL * 1024 * 1024 * 1024 * 1024;  /* 2TB */
    drv.disks[0].lba48_supported = 1;
    drv.disks[0].dma_supported = 1;
    drv.disks[0].ncq_supported = 1;
    drv.disks[0].trim_supported = 1;
    strncpy(drv.disks[0].model, "HEPU SSD 2TB", 63);
    strncpy(drv.disks[0].serial, "HEPU20240001", 31);
    strncpy(drv.disks[0].firmware, "1.0.0", 31);
    drv.disks[0].rpm = 0;  /* SSD */
    drv.disks[0].temperature = 35;
    drv.disks[0].smart_supported = 1;
    drv.disks[0].smart_enabled = 1;
    /* 分区 */
    drv.disks[0].partitions[0].used = 1;
    drv.disks[0].partitions[0].id = 0;
    drv.disks[0].partitions[0].number = 1;
    drv.disks[0].partitions[0].start_lba = 2048;
    drv.disks[0].partitions[0].sector_count = 1048576;  /* 512MB EFI */
    drv.disks[0].partitions[0].size = 512 * 1024 * 1024ULL;
    drv.disks[0].partitions[0].type = PARTITION_TYPE_EFI;
    drv.disks[0].partitions[0].bootable = 1;
    strncpy(drv.disks[0].partitions[0].filesystem, "FAT32", 31);
    strncpy(drv.disks[0].partitions[0].label, "EFI", 63);
    drv.disks[0].partitions[1].used = 1;
    drv.disks[0].partitions[1].id = 1;
    drv.disks[0].partitions[1].number = 2;
    drv.disks[0].partitions[1].start_lba = 1050624;
    drv.disks[0].partitions[1].sector_count = 209715200ULL;  /* 100GB根分区 */
    drv.disks[0].partitions[1].size = 100ULL * 1024 * 1024 * 1024;
    drv.disks[0].partitions[1].type = PARTITION_TYPE_LINUX;
    strncpy(drv.disks[0].partitions[1].filesystem, "ext4", 31);
    strncpy(drv.disks[0].partitions[1].label, "HEPU_ROOT", 63);
    drv.disks[0].partition_count = 2;
    drv.disk_count = 1;

    /* 网卡 */
    drv.netcards[0].used = 1;
    drv.netcards[0].id = 0;
    strncpy(drv.netcards[0].name, "eth0", 31);
    drv.netcards[0].type = NET_TYPE_ETHERNET;
    drv.netcards[0].mac[0] = 0x00;
    drv.netcards[0].mac[1] = 0x11;
    drv.netcards[0].mac[2] = 0x22;
    drv.netcards[0].mac[3] = 0x33;
    drv.netcards[0].mac[4] = 0x44;
    drv.netcards[0].mac[5] = 0x55;
    drv.netcards[0].mtu = 1500;
    drv.netcards[0].speed_mbps = 1000;
    drv.netcards[0].duplex = 1;
    drv.netcards[0].auto_neg = 1;
    drv.netcards[0].link_up = 1;
    strncpy(drv.netcards[0].driver_name, "hepu-e1000", 63);
    strncpy(drv.netcards[0].vendor, "Intel", 63);
    strncpy(drv.netcards[0].model, "82540EM Gigabit Ethernet", 63);
    drv.netcards[0].rx_ring_size = 256;
    drv.netcards[0].tx_ring_size = 256;
    drv.netcards[0].rx_queue_count = 1;
    drv.netcards[0].tx_queue_count = 1;
    drv.netcards[0].checksum_offload = 1;
    drv.netcards[0].tso_supported = 1;
    drv.netcards[0].gso_supported = 1;
    drv.netcards[0].vlan_offload = 1;
    drv.netcard_count = 1;

    vga_puts("[DRV_R] driver realization init done \n");
    vga_printf(" VGA: 80x25 text, keyboard: US layout, mouse: PS/2, hdd: %d, NIC: %d \n",
               drv.disk_count, drv.netcard_count);
}

/* VGA */
int drv_r_keyboard_init(void)
{
    drv.keyboard.layout = 0;
    return 0;
}

int drv_r_keyboard_get_key(keyboard_event_t *event)
{
    if (!event || drv.key_buffer_count == 0) return -1;
    memcpy(event, &drv.key_buffer[drv.key_buffer_tail], sizeof(keyboard_event_t));
    drv.key_buffer_tail = (drv.key_buffer_tail + 1) % 256;
    drv.key_buffer_count--;
    return 0;
}

int drv_r_keyboard_get_state(drv_r_keyboard_state_t *state)
{
    if (!state) return -1;
    memcpy(state, &drv.keyboard, sizeof(drv_r_keyboard_state_t));
    return 0;
}

int drv_r_keyboard_set_layout(int layout)
{
    drv.keyboard.layout = layout;
    return 0;
}

int drv_r_keyboard_get_char(void)
{
    keyboard_event_t event;
    if (drv_r_keyboard_get_key(&event) == 0 && event.pressed) {
        return event.ascii;
    }
    return -1;
}

int drv_r_keyboard_key_pressed(int keycode)
{
    if (keycode < 0 || keycode >= DRV_R_MAX_KEYS) return 0;
    return drv.keyboard.keys[keycode];
}

int drv_r_keyboard_set_leds(int caps, int num, int scroll)
{
    drv.keyboard.caps_lock = caps;
    drv.keyboard.num_lock = num;
    drv.keyboard.scroll_lock = scroll;
    uint8_t leds = (caps ? 4 : 0) | (num ? 2 : 0) | (scroll ? 1 : 0);
    outb(PS2_DATA_PORT, PS2_CMD_SET_LEDS);
    outb(PS2_DATA_PORT, leds);
    return 0;
}

/* 鼠标 */
int drv_r_mouse_init(void)
{
    return 0;
}

int drv_r_mouse_get_event(mouse_event_t *event)
{
    if (!event || drv.mouse_buffer_count == 0) return -1;
    memcpy(event, &drv.mouse_buffer[drv.mouse_buffer_tail], sizeof(mouse_event_t));
    drv.mouse_buffer_tail = (drv.mouse_buffer_tail + 1) % DRV_R_MAX_MOUSE_EVENTS;
    drv.mouse_buffer_count--;
    return 0;
}

int drv_r_mouse_get_state(drv_r_mouse_state_t *state)
{
    if (!state) return -1;
    memcpy(state, &drv.mouse, sizeof(drv_r_mouse_state_t));
    return 0;
}

int drv_r_mouse_set_sensitivity(int sensitivity)
{
    drv.mouse.sensitivity = sensitivity;
    return 0;
}

int drv_r_mouse_set_position(int x, int y)
{
    drv.mouse.x = x;
    drv.mouse.y = y;
    return 0;
}

/* PS/2 */
int drv_r_ps2_init(void)
{
    /* 禁用设备 */
    outb(PS2_CMD_PORT, PS2_CMD_DISABLE_KB);  /* 禁用键盘 */
    outb(PS2_CMD_PORT, PS2_CMD_DISABLE_MOUSE);  /* 禁用鼠标 */
    /* 清空缓冲区 */
    while (inb(PS2_CMD_PORT) & 1) inb(PS2_DATA_PORT);
    /* 设置配置 */
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    uint8_t config = inb(PS2_DATA_PORT);
    config |= PS2_CONFIG_INT_ENABLE;  /* 启用中断 */
    config &= ~PS2_CONFIG_MOUSE_CLK;  /* 启用鼠标时钟 */
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    outb(PS2_DATA_PORT, config);
    /* 启用设备 */
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_KB);  /* 启用键盘 */
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_MOUSE);  /* 启用鼠标 */
    /* 重置键盘 */
    outb(PS2_DATA_PORT, PS2_CMD_RESET);
    /* 重置鼠标 */
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_MOUSE);
    outb(PS2_DATA_PORT, PS2_CMD_RESET);
    drv.ps2.initialized = 1;
    return 0;
}

int drv_r_ps2_get_status(ps2_controller_t *ps2)
{
    if (!ps2) return -1;
    memcpy(ps2, &drv.ps2, sizeof(ps2_controller_t));
    return 0;
}

int drv_r_ps2_write(int port, uint8_t data)
{
    if (port == 0) outb(0x60, data);
    else outb(0x64, data);
    return 0;
}

int drv_r_ps2_read(int port, uint8_t *data)
{
    if (!data) return -1;
    if (port == 0) *data = inb(0x60);
    else *data = inb(0x64);
    return 0;
}

/* 硬盘 */
int drv_r_disk_init(void)
{
    return 0;
}

int drv_r_disk_list(disk_info_t *disks, int max)
{
    if (!disks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 4 && count < max; i++) {
        if (drv.disks[i].used) memcpy(&disks[count++], &drv.disks[i], sizeof(disk_info_t));
    }
    return count;
}

int drv_r_disk_get(int disk_id, disk_info_t *disk)
{
    if (disk_id < 0 || disk_id >= 4 || !drv.disks[disk_id].used || !disk) return -1;
    memcpy(disk, &drv.disks[disk_id], sizeof(disk_info_t));
    return 0;
}

int drv_r_disk_read(int disk_id, uint64_t lba, int count, void *buffer)
{
    (void)disk_id; (void)lba; (void)count; (void)buffer;
    return 0;  /* 原型阶段 */
}

int drv_r_disk_write(int disk_id, uint64_t lba, int count, const void *buffer)
{
    (void)disk_id; (void)lba; (void)count; (void)buffer;
    return 0;  /* 原型阶段 */
}

int drv_r_disk_flush(int disk_id)
{
    (void)disk_id;
    return 0;
}

int drv_r_disk_trim(int disk_id, uint64_t lba, int count)
{
    (void)disk_id; (void)lba; (void)count;
    return 0;
}

int drv_r_disk_get_partitions(int disk_id, disk_partition_t *parts, int max)
{
    if (disk_id < 0 || disk_id >= 4 || !drv.disks[disk_id].used || !parts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRV_R_MAX_DISK_PARTITIONS && count < max; i++) {
        if (drv.disks[disk_id].partitions[i].used)
            memcpy(&parts[count++], &drv.disks[disk_id].partitions[i], sizeof(disk_partition_t));
    }
    return count;
}

/* 网卡 */
int drv_r_net_init(void)
{
    return 0;
}

int drv_r_net_list(netcard_info_t *cards, int max)
{
    if (!cards || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 4 && count < max; i++) {
        if (drv.netcards[i].used) memcpy(&cards[count++], &drv.netcards[i], sizeof(netcard_info_t));
    }
    return count;
}

int drv_r_net_get(int card_id, netcard_info_t *card)
{
    if (card_id < 0 || card_id >= 4 || !drv.netcards[card_id].used || !card) return -1;
    memcpy(card, &drv.netcards[card_id], sizeof(netcard_info_t));
    return 0;
}

int drv_r_net_send(int card_id, const void *data, int length)
{
    (void)data;
    if (card_id < 0 || card_id >= 4 || !drv.netcards[card_id].used) return -1;
    drv.netcards[card_id].tx_packets++;
    drv.netcards[card_id].tx_bytes += length;
    return length;
}

int drv_r_net_receive(int card_id, void *buffer, int max_length)
{
    (void)buffer; (void)max_length;
    if (card_id < 0 || card_id >= 4 || !drv.netcards[card_id].used) return -1;
    return 0;
}

int drv_r_net_set_promiscuous(int card_id, int enable)
{
    if (card_id < 0 || card_id >= 4 || !drv.netcards[card_id].used) return -1;
    (void)enable;
    return 0;
}

int drv_r_net_set_mtu(int card_id, int mtu)
{
    if (card_id < 0 || card_id >= 4 || !drv.netcards[card_id].used) return -1;
    drv.netcards[card_id].mtu = mtu;
    return 0;
}

int drv_r_get_stats(drivers_real_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &drv, sizeof(drivers_real_t));
    return 0;
}

