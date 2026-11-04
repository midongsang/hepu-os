/* ============================================================================
 * 赫普OS - devfs 设备文件系统
 * /dev/null, /dev/zero, /dev/fb0 (帧缓冲), /dev/mem
 * ========================================================================== */

#include <stdint.h>
#include "vfs.h"
#include "vga.h"
#include "vga_gfx.h"
#include "string.h"

/* /dev/null: 读取返回0, 写入丢弃 */
static int dev_null_read(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size)
{
    (void)node; (void)offset; (void)buf; (void)size;
    return 0;  /* EOF */
}

static int dev_null_write(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size)
{
    (void)node; (void)offset; (void)buf;
    return size;  /* 丢弃所有数据 */
}

static vfs_file_ops_t null_ops = {
    .read = dev_null_read,
    .write = dev_null_write,
};

/* /dev/zero: 读取返回全0, 写入丢弃 */
static int dev_zero_read(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size)
{
    (void)node; (void)offset;
    memset(buf, 0, size);
    return size;
}

static vfs_file_ops_t zero_ops = {
    .read = dev_zero_read,
    .write = dev_null_write,
};

/* /dev/fb0: 帧缓冲设备 */
static int dev_fb0_read(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size)
{
    (void)node;
    uint8_t *fb = gfx_get_framebuffer();
    uint32_t fb_size = 320 * 200;

    if (offset >= fb_size) return 0;
    if (offset + size > fb_size) size = fb_size - offset;

    memcpy(buf, fb + offset, size);
    return size;
}

static int dev_fb0_write(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size)
{
    (void)node;
    uint8_t *fb = gfx_get_framebuffer();
    uint32_t fb_size = 320 * 200;

    if (offset >= fb_size) return 0;
    if (offset + size > fb_size) size = fb_size - offset;

    memcpy(fb + offset, buf, size);
    return size;
}

static vfs_file_ops_t fb0_ops = {
    .read = dev_fb0_read,
    .write = dev_fb0_write,
};

/* /dev/mem: 物理内存 */
static int dev_mem_read(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size)
{
    (void)node;
    memcpy(buf, (void *)offset, size);
    return size;
}

static int dev_mem_write(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size)
{
    (void)node;
    memcpy((void *)offset, buf, size);
    return size;
}

static vfs_file_ops_t mem_ops = {
    .read = dev_mem_read,
    .write = dev_mem_write,
};

/* ============================================================================
 * /dev/console: V3.87 兼容层标准流——Linux ELF 程序 write(1,..) 输出到
 * VGA 文本屏幕, read(0,..) 从键盘读取 (APK 式沙箱内虚拟终端).
 * ========================================================================== */
static int dev_console_read(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size)
{
    (void)node; (void)offset;
    extern int keyboard_try_getchar(char *c);   /* V3.87 B-3: 非阻塞读取, 无输入立即返回 0 (POSIX read 语义)
                                                 * 原 keyboard_getchar 阻塞 HLT 等键盘, QEMU 无输入时
                                                 * winargs ReadFile 永久卡死, 后续 GetTickCount/Sleep 不执行 */
    uint8_t *out = (uint8_t *)buf;
    uint32_t n = 0;
    while (n < size) {
        char c;
        if (!keyboard_try_getchar(&c)) break;
        out[n++] = (uint8_t)c;
    }
    return (int)n;
}

static int dev_console_write(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size)
{
    (void)node; (void)offset;
    const char *s = (const char *)buf;
    extern void serial_putc(uint16_t port, char c);
    for (uint32_t i = 0; i < size; i++) {
        vga_putc(s[i]);
        serial_putc(0x3F8, s[i]);
    }
    return (int)size;
}

vfs_file_ops_t console_ops = {
    .read = dev_console_read,
    .write = dev_console_write,
};

/* 初始化devfs */
int devfs_init(void)
{
    vfs_node_t *root = vfs_get_root();

    /* 创建 /dev 目录 */
    vfs_node_t *dev = vfs_create_node("dev", VFS_DIRECTORY);
    vfs_add_child(root, dev);

    /* /dev/null */
    vfs_node_t *null = vfs_create_node("null", VFS_CHAR_DEVICE);
    null->ops = &null_ops;
    vfs_add_child(dev, null);

    /* /dev/zero */
    vfs_node_t *zero = vfs_create_node("zero", VFS_CHAR_DEVICE);
    zero->ops = &zero_ops;
    vfs_add_child(dev, zero);

    /* /dev/fb0 */
    vfs_node_t *fb0 = vfs_create_node("fb0", VFS_CHAR_DEVICE);
    fb0->size = 320 * 200;
    fb0->ops = &fb0_ops;
    vfs_add_child(dev, fb0);

    /* /dev/console: V3.87 兼容层标准流 */
    vfs_node_t *console = vfs_create_node("console", VFS_CHAR_DEVICE);
    console->ops = &console_ops;
    vfs_add_child(dev, console);

    /* /dev/mem */
    vfs_node_t *mem = vfs_create_node("mem", VFS_CHAR_DEVICE);
    mem->size = 0xFFFFFFFF;
    mem->ops = &mem_ops;
    vfs_add_child(dev, mem);

    vga_puts("[DEVFS] device fs init: null, zero, fb0, mem\n");
    return 0;
}
