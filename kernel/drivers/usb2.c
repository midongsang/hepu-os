/* ============================================================================
 * 赫普OS - USB深化
 * 主机/设备模式 + ADB + MTP + USB网卡 + 传输管理
 * ========================================================================== */

#include <stdint.h>
#include "usb2.h"
#include "vga.h"
#include "string.h"

static usb2_t usb2;

void usb2_init(void)
{
    memset(&usb2, 0, sizeof(usb2_t));
    usb2.initialized = 1;
    usb2.role = USB_ROLE_DEVICE;
    usb2.host_controller_count = 1;
    usb2.otg_supported = 1;

    /* ADB默认启用 */
    usb2.adb.enabled = 1;
    strncpy(usb2.adb.device_serial, "HEPU-001", 63);

    /* MTP默认启用 */
    usb2.mtp.enabled = 1;
    usb2.mtp.storage_count = 1;
    strncpy(usb2.mtp.storage_names[0], "Internal Storage", 63);
    usb2.mtp.total_storage[0] = 64 * 1024 * 1024 * 1024ULL;  /* 64GB */
    usb2.mtp.free_storage[0] = 32 * 1024 * 1024 * 1024ULL;   /* 32GB */

    /* USB网卡 */
    usb2.ethernet.mac_address[0] = 0x52;
    usb2.ethernet.mac_address[1] = 0x54;
    usb2.ethernet.mac_address[2] = 0x00;
    usb2.ethernet.mac_address[3] = 0x12;
    usb2.ethernet.mac_address[4] = 0x34;
    usb2.ethernet.mac_address[5] = 0x57;

    vga_puts("[USB2] USB deep init done \n");
    vga_printf(" role: %s, ADB: %s, MTP: %s\n",
               usb2.role == USB_ROLE_HOST ? " master 机 " : " device ",
               usb2.adb.enabled ? " enabled " : " disabled ",
               usb2.mtp.enabled ? " enabled " : " disabled ");
}

int usb2_set_role(usb_role_t role)
{
    usb2.role = role;
    vga_printf("[USB2] USB role: %d\n", role);
    return 0;
}

usb_role_t usb2_get_role(void) { return usb2.role; }
int usb2_otg_supported(void) { return usb2.otg_supported; }

int usb2_get_device(int device_id, usb2_device_t *device)
{
    if (device_id < 0 || device_id >= USB2_MAX_DEVICES || !usb2.devices[device_id].used || !device) return -1;
    memcpy(device, &usb2.devices[device_id], sizeof(usb2_device_t));
    return 0;
}

int usb2_list_devices(usb2_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < USB2_MAX_DEVICES && count < max; i++) {
        if (usb2.devices[i].used) memcpy(&devices[count++], &usb2.devices[i], sizeof(usb2_device_t));
    }
    return count;
}

int usb2_reset_device(int device_id)
{
    if (device_id < 0 || device_id >= USB2_MAX_DEVICES || !usb2.devices[device_id].used) return -1;
    usb2.resets++;
    vga_printf("[USB2] reset device #%d\n", device_id);
    return 0;
}

int usb2_suspend_device(int device_id)
{
    if (device_id < 0 || device_id >= USB2_MAX_DEVICES || !usb2.devices[device_id].used) return -1;
    usb2.devices[device_id].suspended = 1;
    return 0;
}

int usb2_resume_device(int device_id)
{
    if (device_id < 0 || device_id >= USB2_MAX_DEVICES || !usb2.devices[device_id].used) return -1;
    usb2.devices[device_id].suspended = 0;
    return 0;
}

int usb2_get_device_count(void) { return usb2.device_count; }

int usb2_control_transfer(int device_id, uint8_t request_type, uint8_t request,
                          uint16_t value, uint16_t index, uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)device_id; (void)request_type; (void)request;
    (void)value; (void)index; (void)data; (void)timeout;
    usb2.total_transfers++;
    return length;
}

int usb2_bulk_transfer(int device_id, uint8_t endpoint, uint8_t *data, int length, int *actual, uint32_t timeout)
{
    (void)device_id; (void)endpoint; (void)data; (void)timeout;
    usb2.total_transfers++;
    usb2.total_bytes += length;
    if (actual) *actual = length;
    return 0;
}

int usb2_interrupt_transfer(int device_id, uint8_t endpoint, uint8_t *data, int length, int *actual, uint32_t timeout)
{
    (void)device_id; (void)endpoint; (void)data; (void)timeout;
    usb2.total_transfers++;
    if (actual) *actual = length;
    return 0;
}

int usb2_async_transfer(usb2_transfer_t *transfer)
{
    if (!transfer) return -1;
    usb2.total_transfers++;
    transfer->status = 0;
    transfer->actual_length = transfer->length;
    if (transfer->callback) transfer->callback(transfer->id, 0, transfer->length, transfer->callback_arg);
    return 0;
}

int usb2_cancel_transfer(int transfer_id)
{
    (void)transfer_id;
    return 0;
}

int usb2_get_device_descriptor(int device_id, uint8_t *desc, int len)
{
    (void)device_id;
    if (!desc || len < 18) return -1;
    /* 标准设备描述符 */
    desc[0] = 18;   /* bLength */
    desc[1] = 1;    /* bDescriptorType */
    desc[2] = 0x00; /* bcdUSB */
    desc[3] = 0x02;
    desc[4] = 0;    /* bDeviceClass */
    desc[5] = 0;    /* bDeviceSubClass */
    desc[6] = 0;    /* bDeviceProtocol */
    desc[7] = 64;   /* bMaxPacketSize0 */
    desc[8] = 0x18; /* idVendor */
    desc[9] = 0x18;
    desc[10] = 0x01; /* idProduct */
    desc[11] = 0x00;
    desc[12] = 0x00; /* bcdDevice */
    desc[13] = 0x01;
    desc[14] = 1;    /* iManufacturer */
    desc[15] = 2;    /* iProduct */
    desc[16] = 3;    /* iSerialNumber */
    desc[17] = 1;    /* bNumConfigurations */
    return 18;
}

int usb2_get_config_descriptor(int device_id, int config, uint8_t *desc, int len)
{
    (void)device_id; (void)config;
    if (!desc || len < 9) return -1;
    desc[0] = 9;    /* bLength */
    desc[1] = 2;    /* bDescriptorType */
    desc[2] = 0x12; /* wTotalLength */
    desc[3] = 0x00;
    desc[4] = 1;    /* bNumInterfaces */
    desc[5] = 1;    /* bConfigurationValue */
    desc[6] = 0;    /* iConfiguration */
    desc[7] = 0x80; /* bmAttributes */
    desc[8] = 50;   /* bMaxPower (100mA) */
    return 9;
}

int usb2_get_string_descriptor(int device_id, int index, uint8_t *desc, int len)
{
    (void)device_id; (void)index;
    if (!desc || len < 4) return -1;
    desc[0] = 4;
    desc[1] = 3;
    desc[2] = 0x09;
    desc[3] = 0x04;
    return 4;
}

int adb_enable(int enable)
{
    usb2.adb.enabled = enable ? 1 : 0;
    vga_printf("[USB2] ADB %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int adb_is_enabled(void) { return usb2.adb.enabled; }

int adb_authorize(int authorize)
{
    usb2.adb.authorized = authorize ? 1 : 0;
    return 0;
}

int adb_is_connected(void) { return usb2.adb.connected; }

int adb_send_command(const char *command, char *response, int response_len)
{
    (void)command;
    if (response && response_len > 0) {
        strncpy(response, "OK", response_len - 1);
        response[response_len - 1] = '\0';
    }
    return 0;
}

int adb_push_file(const char *local, const char *remote)
{
    (void)local; (void)remote;
    return 0;
}

int adb_pull_file(const char *remote, const char *local)
{
    (void)remote; (void)local;
    return 0;
}

int adb_shell(const char *command, char *output, int output_len)
{
    (void)command;
    if (output && output_len > 0) output[0] = '\0';
    return 0;
}

int mtp_enable(int enable)
{
    usb2.mtp.enabled = enable ? 1 : 0;
    vga_printf("[USB2] MTP %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int mtp_is_enabled(void) { return usb2.mtp.enabled; }
int mtp_is_connected(void) { return usb2.mtp.connected; }

int mtp_get_storage_info(int storage_id, uint64_t *total, uint64_t *free)
{
    if (storage_id < 0 || storage_id >= usb2.mtp.storage_count) return -1;
    if (total) *total = usb2.mtp.total_storage[storage_id];
    if (free) *free = usb2.mtp.free_storage[storage_id];
    return 0;
}

int mtp_list_files(int storage_id, const char *path, char *files, int max)
{
    (void)storage_id; (void)path; (void)files; (void)max;
    return 0;
}

int mtp_send_file(int storage_id, const char *local, const char *remote)
{
    (void)storage_id; (void)local; (void)remote;
    usb2.mtp.file_transfers++;
    return 0;
}

int mtp_receive_file(int storage_id, const char *remote, const char *local)
{
    (void)storage_id; (void)remote; (void)local;
    usb2.mtp.file_transfers++;
    return 0;
}

int mtp_delete_file(int storage_id, const char *path)
{
    (void)storage_id; (void)path;
    return 0;
}

int usb_ethernet_enable(int enable)
{
    usb2.ethernet.enabled = enable ? 1 : 0;
    vga_printf("[USB2] USB NIC %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int usb_ethernet_is_enabled(void) { return usb2.ethernet.enabled; }
int usb_ethernet_is_connected(void) { return usb2.ethernet.connected; }

int usb_ethernet_get_mac(uint8_t *mac)
{
    if (!mac) return -1;
    memcpy(mac, usb2.ethernet.mac_address, 6);
    return 0;
}

int usb_ethernet_get_stats(uint64_t *sent, uint64_t *received, uint64_t *bytes_sent, uint64_t *bytes_received)
{
    if (sent) *sent = usb2.ethernet.packets_sent;
    if (received) *received = usb2.ethernet.packets_received;
    if (bytes_sent) *bytes_sent = usb2.ethernet.bytes_sent;
    if (bytes_received) *bytes_received = usb2.ethernet.bytes_received;
    return 0;
}

int usb2_set_function(int function, int enable)
{
    (void)function; (void)enable;
    return 0;
}

int usb2_get_function(int function)
{
    (void)function;
    return 0;
}

int usb2_get_stats(uint64_t *transfers, uint64_t *bytes, uint64_t *errors)
{
    if (transfers) *transfers = usb2.total_transfers;
    if (bytes) *bytes = usb2.total_bytes;
    if (errors) *errors = usb2.errors;
    return 0;
}
