/* ============================================================================
 * 赫普OS 64位 - devfs 设备文件系统实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "devfs64.h"

extern void vga_puts(const char *s);
extern void vga_putchar(char c);
extern char keyboard64_getchar(void);
extern int  keyboard64_haschar(void);

static devfs_node_t nodes[DEVFS64_MAX_NODES];
static int devfs_initialized = 0;

/* ---------- 设备操作 ---------- */

/* /dev/null */
static int dev_null_read(uint32_t offset, void *buf, uint32_t size)
{
    (void)offset; (void)buf; (void)size;
    return 0;  /* EOF */
}

static int dev_null_write(uint32_t offset, const void *buf, uint32_t size)
{
    (void)offset; (void)buf;
    return size;  /* 丢弃 */
}

/* /dev/zero */
static int dev_zero_read(uint32_t offset, void *buf, uint32_t size)
{
    (void)offset;
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) p[i] = 0;
    return size;
}

/* /dev/console */
static int dev_console_read(uint32_t offset, void *buf, uint32_t size)
{
    (void)offset;
    uint8_t *out = (uint8_t *)buf;
    uint32_t n = 0;
    while (n < size && keyboard64_haschar()) {
        out[n++] = (uint8_t)keyboard64_getchar();
    }
    return (int)n;
}

static int dev_console_write(uint32_t offset, const void *buf, uint32_t size)
{
    (void)offset;
    const char *s = (const char *)buf;
    for (uint32_t i = 0; i < size; i++) vga_putchar(s[i]);
    return (int)size;
}

/* ---------- 内部工具 ---------- */

static int devfs_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void devfs_strncpy(char *dst, const char *src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ---------- 公共 API ---------- */

void devfs64_init(void)
{
    for (int i = 0; i < DEVFS64_MAX_NODES; i++) {
        nodes[i].active = 0;
        nodes[i].capability_id = -1;
    }

    devfs_ops_t null_ops = { .read = dev_null_read, .write = dev_null_write };
    devfs_ops_t zero_ops = { .read = dev_zero_read, .write = dev_null_write };
    devfs_ops_t console_ops = { .read = dev_console_read, .write = dev_console_write };

    devfs64_register("null", null_ops, 0);
    devfs64_register("zero", zero_ops, 0);
    devfs64_register("console", console_ops, 0);

    devfs_initialized = 1;
    vga_puts("[DEVFS64] device fs init: null, zero, console\n");
}

int devfs64_register(const char *name, devfs_ops_t ops, uint64_t size)
{
    for (int i = 0; i < DEVFS64_MAX_NODES; i++) {
        if (!nodes[i].active) {
            devfs_strncpy(nodes[i].name, name, DEVFS_NAME_LEN);
            nodes[i].ops = ops;
            nodes[i].size = size;
            nodes[i].active = 1;
            nodes[i].capability_id = -1;
            return i;
        }
    }
    return -1;
}

int devfs64_find(const char *name)
{
    for (int i = 0; i < DEVFS64_MAX_NODES; i++) {
        if (nodes[i].active && devfs_strcmp(nodes[i].name, name) == 0)
            return i;
    }
    return -1;
}

int devfs64_read(int node_idx, uint32_t offset, void *buf, uint32_t size)
{
    if (node_idx < 0 || node_idx >= DEVFS64_MAX_NODES) return -1;
    if (!nodes[node_idx].active) return -1;
    if (!nodes[node_idx].ops.read) return -1;
    return nodes[node_idx].ops.read(offset, buf, size);
}

int devfs64_write(int node_idx, uint32_t offset, const void *buf, uint32_t size)
{
    if (node_idx < 0 || node_idx >= DEVFS64_MAX_NODES) return -1;
    if (!nodes[node_idx].active) return -1;
    if (!nodes[node_idx].ops.write) return -1;
    return nodes[node_idx].ops.write(offset, buf, size);
}

int devfs64_list(devfs_node_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < DEVFS64_MAX_NODES && n < max; i++) {
        if (nodes[i].active) out[n++] = nodes[i];
    }
    return n;
}
