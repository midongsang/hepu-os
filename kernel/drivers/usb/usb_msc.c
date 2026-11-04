/* ============================================================================
 * 赫普OS - USB大容量存储类驱动实现
 * USB Mass Storage Class (Bulk-Only Transport + SCSI)
 * V8.33: 初始实现
 * ========================================================================== */

#include "usb_msc.h"
#include "../../include/vga.h"

/* 内存分配 */
extern void *kmalloc(uint32_t size);
extern void *kfree(void *ptr);
extern void *memset(void *dst, int val, uint32_t n);
extern void *memcpy(void *dst, const void *src, uint32_t n);

/* 全局MSC设备 */
#define MAX_MSC_DEVICES 4
static msc_device_t msc_devices[MAX_MSC_DEVICES];
static int num_msc_devices = 0;

/* 字节序转换（大端） */
static uint16_t be16(uint16_t v) {
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF);
}
static uint32_t be32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

/* Bulk-Only Mass Storage Reset */
int usb_msc_bulk_only_reset(usb_device_t *dev)
{
    if (!dev) return -1;

    return hepu_usb_control_request(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        MSC_REQ_BOMSR,
        0, 0, NULL, 0);
}

/* 获取最大LUN */
int usb_msc_get_max_lun(usb_device_t *dev)
{
    if (!dev) return -1;

    uint8_t max_lun = 0;
    int ret = hepu_usb_control_request(dev,
        USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        MSC_REQ_GET_MAX_LUN,
        0, 0, &max_lun, 1);

    if (ret < 0) return 0;  /* 默认0 */
    return max_lun;
}

/* 执行Bulk-Only传输 */
int usb_msc_command(usb_device_t *dev, msc_device_t *msc,
                     const void *cb, int cb_len,
                     void *data, int data_len, int direction)
{
    if (!dev || !msc || !cb || cb_len <= 0) return -1;

    /* 1. 构造CBW */
    msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = MSC_CBW_SIGNATURE;
    cbw.tag = ++msc->tag;
    cbw.data_transfer_length = data_len;
    cbw.flags = (direction == USB_DIR_IN) ? MSC_CBW_FLAG_IN : MSC_CBW_FLAG_OUT;
    cbw.lun = 0;
    cbw.cb_length = cb_len;
    memcpy(cbw.cb, cb, cb_len);

    /* 2. 发送CBW（批量输出） */
    extern int uhci_bulk_transfer(usb_controller_t *ctrl,
                                   usb_device_t *dev, int endpoint,
                                   void *data, int length, int direction);
    int ret = uhci_bulk_transfer((usb_controller_t *)dev->controller, dev,
                                  msc->bulk_out_ep, &cbw, sizeof(cbw), USB_DIR_OUT);
    if (ret < 0) {
        vga_puts("[MSC] Failed to send CBW\n");
        return -1;
    }

    /* 3. 数据阶段（如果有） */
    if (data_len > 0 && data) {
        ret = uhci_bulk_transfer((usb_controller_t *)dev->controller, dev,
                                  (direction == USB_DIR_IN) ? msc->bulk_in_ep : msc->bulk_out_ep,
                                  data, data_len, direction);
        if (ret < 0) {
            vga_puts("[MSC] Data phase failed\n");
            return -1;
        }
    }

    /* 4. 接收CSW（批量输入） */
    msc_csw_t csw;
    memset(&csw, 0, sizeof(csw));
    ret = uhci_bulk_transfer((usb_controller_t *)dev->controller, dev,
                              msc->bulk_in_ep, &csw, sizeof(csw), USB_DIR_IN);
    if (ret < 0) {
        vga_puts("[MSC] Failed to receive CSW\n");
        return -1;
    }

    /* 5. 验证CSW */
    if (csw.signature != MSC_CSW_SIGNATURE) {
        vga_printf("[MSC] Invalid CSW signature: 0x%08x\n", csw.signature);
        return -1;
    }

    if (csw.tag != cbw.tag) {
        vga_printf("[MSC] CSW tag mismatch: expected %u, got %u\n",
                   cbw.tag, csw.tag);
        return -1;
    }

    if (csw.status != MSC_CSW_STATUS_PASSED) {
        vga_printf("[MSC] Command failed: status=%d, residue=%u\n",
                   csw.status, csw.data_residue);
        return -1;
    }

    return 0;
}

/* SCSI TEST UNIT READY */
int usb_msc_test_unit_ready(msc_device_t *msc)
{
    if (!msc) return -1;

    uint8_t cb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return usb_msc_command(msc->usb_dev, msc, cb, 6, NULL, 0, USB_DIR_OUT);
}

/* SCSI INQUIRY */
int usb_msc_inquiry(msc_device_t *msc, scsi_inquiry_t *inquiry)
{
    if (!msc || !inquiry) return -1;

    uint8_t cb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    int ret = usb_msc_command(msc->usb_dev, msc, cb, 6,
                               inquiry, sizeof(scsi_inquiry_t), USB_DIR_IN);
    if (ret == 0) {
        /* 复制厂商/产品/版本信息 */
        memcpy(msc->vendor, inquiry->vendor, 8);
        msc->vendor[8] = 0;
        memcpy(msc->product, inquiry->product, 16);
        msc->product[16] = 0;
        memcpy(msc->revision, inquiry->revision, 4);
        msc->revision[4] = 0;
        msc->removable = (inquiry->removable & 0x80) ? 1 : 0;

        vga_printf("[MSC] Vendor: %.8s Product: %.16s Rev: %.4s\n",
                   msc->vendor, msc->product, msc->revision);
    }
    return ret;
}

/* SCSI READ CAPACITY */
int usb_msc_read_capacity(msc_device_t *msc)
{
    if (!msc) return -1;

    uint8_t cb[10] = { SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    scsi_capacity_t cap;
    int ret = usb_msc_command(msc->usb_dev, msc, cb, 10,
                               &cap, sizeof(cap), USB_DIR_IN);
    if (ret == 0) {
        msc->block_count = be32(cap.last_lba) + 1;
        msc->block_size = be32(cap.block_size);
        msc->capacity = (uint64_t)msc->block_count * msc->block_size;

        vga_printf("[MSC] Capacity: %u blocks x %u bytes = %llu MB\n",
                   msc->block_count, msc->block_size,
                   (unsigned long long)(msc->capacity / (1024 * 1024)));
    }
    return ret;
}

/* SCSI READ(10) */
int usb_msc_read_blocks(msc_device_t *msc, uint32_t lba,
                         uint16_t count, void *buffer)
{
    if (!msc || !buffer || count == 0) return -1;

    scsi_read10_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.opcode = SCSI_READ_10;
    cb.lba = be32(lba);
    cb.transfer_length = be16(count);

    int data_len = count * msc->block_size;
    return usb_msc_command(msc->usb_dev, msc, &cb, sizeof(cb),
                            buffer, data_len, USB_DIR_IN);
}

/* SCSI WRITE(10) */
int usb_msc_write_blocks(msc_device_t *msc, uint32_t lba,
                          uint16_t count, const void *buffer)
{
    if (!msc || !buffer || count == 0) return -1;

    scsi_write10_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.opcode = SCSI_WRITE_10;
    cb.lba = be32(lba);
    cb.transfer_length = be16(count);

    int data_len = count * msc->block_size;
    return usb_msc_command(msc->usb_dev, msc, &cb, sizeof(cb),
                            (void *)buffer, data_len, USB_DIR_OUT);
}

/* SCSI REQUEST SENSE */
int usb_msc_request_sense(msc_device_t *msc, void *sense, int len)
{
    if (!msc || !sense || len <= 0) return -1;

    uint8_t cb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, (uint8_t)len, 0 };
    return usb_msc_command(msc->usb_dev, msc, cb, 6, sense, len, USB_DIR_IN);
}

/* MSC设备探测 */
int usb_msc_probe(usb_device_t *dev)
{
    if (!dev) return -1;

    vga_printf("[MSC] Probing device: VID=0x%04x PID=0x%04x\n",
               dev->vendor_id, dev->product_id);

    /* 检查是否是大容量存储设备 */
    int is_msc = 0;
    int subclass = 0;
    int protocol = 0;

    if (dev->device_class == 0x08) {  /* Mass Storage */
        is_msc = 1;
        subclass = dev->device_subclass;
        protocol = dev->device_protocol;
    }

    /* 检查接口描述符 */
    for (int cfg = 0; cfg < dev->num_configs && cfg < USB_MAX_CONFIGS; cfg++) {
        for (int iface = 0; iface < dev->configs[cfg].num_interfaces; iface++) {
            usb_interface_t *itf = &dev->configs[cfg].interfaces[iface];
            if (itf->class_code == 0x08) {  /* Mass Storage */
                is_msc = 1;
                subclass = itf->sub_class;
                protocol = itf->protocol;

                /* 查找批量端点 */
                for (int ep = 0; ep < itf->num_endpoints; ep++) {
                    usb_endpoint_t *endpoint = &itf->endpoints[ep];
                    if (endpoint->type == USB_EP_BULK) {
                        if (endpoint->address & USB_DIR_IN) {
                            /* 批量输入端点 */
                        } else {
                            /* 批量输出端点 */
                        }
                    }
                }
            }
        }
    }

    if (!is_msc) return -1;

    if (num_msc_devices >= MAX_MSC_DEVICES) {
        vga_puts("[MSC] Too many MSC devices\n");
        return -1;
    }

    /* 分配MSC设备结构 */
    msc_device_t *msc = &msc_devices[num_msc_devices++];
    memset(msc, 0, sizeof(msc_device_t));
    msc->usb_dev = dev;
    msc->subclass = subclass;
    msc->protocol = protocol;
    msc->tag = 0;

    vga_printf("[MSC] Mass storage device: subclass=0x%02x protocol=0x%02x\n",
               subclass, protocol);

    /* 只支持Bulk-Only Transport */
    if (protocol != MSC_PROTOCOL_BOT) {
        vga_printf("[MSC] Unsupported protocol: 0x%02x (only BOT supported)\n",
                   protocol);
        return -1;
    }

    /* 简化：使用默认端点地址 */
    msc->bulk_in_ep = 0x81;   /* 批量输入端点1 */
    msc->bulk_out_ep = 0x02;  /* 批量输出端点2 */
    msc->bulk_in_ep_size = 64;
    msc->bulk_out_ep_size = 64;

    /* Bulk-Only Reset */
    usb_msc_bulk_only_reset(dev);

    /* 获取最大LUN */
    msc->max_lun = usb_msc_get_max_lun(dev);
    vga_printf("[MSC] Max LUN: %d\n", msc->max_lun);

    /* 等待设备就绪 */
    for (int i = 0; i < 10; i++) {
        if (usb_msc_test_unit_ready(msc) == 0) break;
        for (volatile int j = 0; j < 100000; j++);
    }

    /* INQUIRY */
    scsi_inquiry_t inquiry;
    if (usb_msc_inquiry(msc, &inquiry) == 0) {
        vga_printf("[MSC] Device: %.8s %.16s\n", msc->vendor, msc->product);
    }

    /* READ CAPACITY */
    if (usb_msc_read_capacity(msc) == 0) {
        vga_printf("[MSC] Capacity: %llu MB\n",
                   (unsigned long long)(msc->capacity / (1024 * 1024)));
    }

    return 0;
}

/* 初始化MSC驱动 */
int usb_msc_init(void)
{
    memset(msc_devices, 0, sizeof(msc_devices));
    num_msc_devices = 0;

    vga_puts("[MSC] USB Mass Storage class driver initialized\n");
    return 0;
}

/* 获取MSC设备数量 */
int usb_msc_get_count(void)
{
    return num_msc_devices;
}

/* 获取MSC设备 */
msc_device_t *usb_msc_get_device(int index)
{
    if (index < 0 || index >= num_msc_devices) return NULL;
    return &msc_devices[index];
}
