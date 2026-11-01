/* ============================================================================
 * 赫普OS - 多文件系统注册框架
 * ========================================================================== */

#include <stdint.h>
#include "fs_registry.h"
#include "vga.h"
#include "string.h"

static fs_driver_t *filesystems[MAX_FILESYSTEMS];
static int fs_count = 0;
static int fs_initialized = 0;

void fs_registry_init(void)
{
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        filesystems[i] = NULL;
    }
    fs_count = 0;
    fs_initialized = 1;
    vga_puts("[FSREG] fs registry init \n");
}

int fs_register(fs_driver_t *fs)
{
    if (!fs_initialized || !fs || !fs->name) return -1;
    if (fs_count >= MAX_FILESYSTEMS) return -1;

    /* 检查是否已注册 */
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(filesystems[i]->name, fs->name) == 0) {
            return -1;  /* 已存在 */
        }
    }

    filesystems[fs_count++] = fs;
    vga_printf("[FSREG] register fs: %s\n", fs->name);
    return 0;
}

fs_driver_t *fs_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < fs_count; i++) {
        if (strcmp(filesystems[i]->name, name) == 0) {
            return filesystems[i];
        }
    }
    return NULL;
}

int fs_list(fs_driver_t **list, int max)
{
    if (!list || max <= 0) return 0;
    int count = fs_count < max ? fs_count : max;
    for (int i = 0; i < count; i++) {
        list[i] = filesystems[i];
    }
    return count;
}

int fs_mount(const char *fs_name, const char *device, const char *mountpoint)
{
    fs_driver_t *fs = fs_find(fs_name);
    if (!fs) {
        vga_printf("[FSREG] fs not found: %s\n", fs_name);
        return -1;
    }
    if (!fs->mount) return -1;
    return fs->mount(device, mountpoint);
}
