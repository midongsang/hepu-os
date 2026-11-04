/* ============================================================================
 * 赫普OS - initrd 内存文件系统
 * 简单的只读文件系统，用于启动时加载用户程序和.hug应用
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "initrd.h"
#include "vga.h"
#include "string.h"

static initrd_header_t      *initrd_header = NULL;
static initrd_file_entry_t  *initrd_files  = NULL;
static uint8_t              *initrd_base   = NULL;
static uint32_t              initrd_total  = 0;
static int                   initrd_loaded = 0;

#define MAX_FDS 16
static initrd_fd_t fds[MAX_FDS];

int initrd_init(uint32_t addr, uint32_t size)
{
    if (!addr || !size) {
        return -1;
    }

    initrd_base = (uint8_t *)addr;
    initrd_total = size;

    /* V3.82 诊断: 打印加载地址与头尾数据 */
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, "\n[INITRD] base="); serial_puthex(0x3F8, addr);
    serial_puts(0x3F8, " size="); serial_puthex(0x3F8, size);
    serial_puts(0x3F8, " head="); serial_puthex(0x3F8, *(volatile uint32_t *)addr);
    serial_puts(0x3F8, " tail="); serial_puthex(0x3F8, *(volatile uint32_t *)(addr + size - 4));
    serial_puts(0x3F8, "\n");

    /* 验证魔数 */
    initrd_header = (initrd_header_t *)addr;
    if (memcmp(initrd_header->magic, INITRD_MAGIC, INITRD_MAGIC_LEN) != 0) {
        vga_puts("[INITRD] error: magic mismatch \n");
        initrd_loaded = 0;
        return -1;
    }

    if (initrd_header->version != INITRD_VERSION) {
        vga_printf("[INITRD] error: version mismatch(expected %d, actual %d)\n",
                   INITRD_VERSION, initrd_header->version);
        return -1;
    }

    /* V3.22修复: file_count 必须校验——超 INITRD_MAX_FILES(64) 时
     * initrd_find/initrd_read 会越界读文件表 */
    if (initrd_header->file_count > INITRD_MAX_FILES) {
        vga_printf("[INITRD] error: file_count too large(%d > %d)\n",
                   initrd_header->file_count, INITRD_MAX_FILES);
        return -1;
    }
    /* V3.22: 文件表必须落在 initrd 数据区内（64 位乘法防溢出） */
    if ((uint64_t)sizeof(initrd_header_t) +
        (uint64_t)initrd_header->file_count * sizeof(initrd_file_entry_t) > size) {
        vga_printf("[INITRD] error: file table out of range\n");
        return -1;
    }

    /* 文件表紧跟在头部之后 */
    initrd_files = (initrd_file_entry_t *)(addr + sizeof(initrd_header_t));

    /* 初始化文件描述符表 */
    for (int i = 0; i < MAX_FDS; i++) {
        fds[i].used = 0;
    }

    initrd_loaded = 1;
    vga_printf("[INITRD] load ok: %d file, %d bytes \n",
               initrd_header->file_count, size);
    return 0;
}

int initrd_ready(void)
{
    return initrd_loaded;
}

int initrd_find(const char *path)
{
    if (!initrd_loaded || !path) return -1;

    for (uint32_t i = 0; i < initrd_header->file_count; i++) {
        if (strcmp(initrd_files[i].name, path) == 0) {
            return i;
        }
    }
    return -1;
}

int initrd_open(const char *path)
{
    if (!initrd_loaded) return -1;

    int index = initrd_find(path);
    if (index < 0) return -1;

    /* 找一个空闲的文件描述符 */
    for (int i = 0; i < MAX_FDS; i++) {
        if (!fds[i].used) {
            fds[i].used = 1;
            fds[i].index = index;
            fds[i].position = 0;
            return i;
        }
    }
    return -1;
}

int initrd_close(int fd)
{
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used) return -1;
    fds[fd].used = 0;
    return 0;
}

int initrd_read(int fd, void *buf, uint32_t count)
{
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used || !buf) return -1;

    initrd_file_entry_t *entry = &initrd_files[fds[fd].index];
    uint32_t remaining = entry->size - fds[fd].position;

    if (count > remaining) count = remaining;
    if (count == 0) return 0;

    memcpy(buf, initrd_base + entry->offset + fds[fd].position, count);
    fds[fd].position += count;
    return count;
}

int initrd_size(int fd)
{
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used) return -1;
    return initrd_files[fds[fd].index].size;
}

void *initrd_get_data(int index)
{
    if (!initrd_loaded || index < 0 ||
        (uint32_t)index >= initrd_header->file_count) {
        return NULL;
    }
    return initrd_base + initrd_files[index].offset;
}

initrd_file_entry_t *initrd_get_entry(int index)
{
    if (!initrd_loaded || index < 0 ||
        (uint32_t)index >= initrd_header->file_count) {
        return NULL;
    }
    return &initrd_files[index];
}

void initrd_list(void)
{
    if (!initrd_loaded) {
        vga_puts("[INITRD] not load \n");
        return;
    }

    vga_printf("[INITRD] file list(%d):\n", initrd_header->file_count);
    for (uint32_t i = 0; i < initrd_header->file_count; i++) {
        vga_printf("  %-32s %8d bytes\n",
                   initrd_files[i].name, initrd_files[i].size);
    }
}
