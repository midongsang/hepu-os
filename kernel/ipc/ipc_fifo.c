/* ============================================================================
 * 赫普OS - IPC命名管道FIFO (V0.96新增)
 * 实现：有名字的管道，支持文件系统路径访问，多读者多写者
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "ipc_fifo.h"
#include "vga.h"
#include "string.h"

#define FIFO_MAX 8
#define FIFO_NAME_MAX 32
#define FIFO_BUF_SIZE 4096
#define FIFO_MAGIC 0x4649464F  /* "FIFO" */

typedef struct {
    uint32_t magic;
    char     name[FIFO_NAME_MAX];
    uint8_t  buffer[FIFO_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint8_t  used;
    uint8_t  read_open;
    uint8_t  write_open;
    uint32_t reader_count;
    uint32_t writer_count;
    uint32_t bytes_read;
    uint32_t bytes_written;
} fifo_t;

static fifo_t fifos[FIFO_MAX];
static uint32_t fifo_count = 0;

void ipc_fifo_init(void)
{
    memset(fifos, 0, sizeof(fifos));
    fifo_count = 0;
    vga_puts("[IPC-FIFO] named pipe subsystem init done \n");
}

/* 创建命名管道 */
int ipc_fifo_create(const char *name)
{
    if (!name || strlen(name) >= FIFO_NAME_MAX) return -1;

    /* 查找已存在的 */
    for (int i = 0; i < FIFO_MAX; i++) {
        if (fifos[i].used && strcmp(fifos[i].name, name) == 0) {
            return i;
        }
    }
    /* 创建新的 */
    for (int i = 0; i < FIFO_MAX; i++) {
        if (!fifos[i].used) {
            fifos[i].magic = FIFO_MAGIC;
            strncpy(fifos[i].name, name, FIFO_NAME_MAX - 1);
            fifos[i].read_pos = 0;
            fifos[i].write_pos = 0;
            fifos[i].count = 0;
            fifos[i].used = 1;
            fifos[i].read_open = 0;
            fifos[i].write_open = 0;
            fifos[i].reader_count = 0;
            fifos[i].writer_count = 0;
            fifos[i].bytes_read = 0;
            fifos[i].bytes_written = 0;
            fifo_count++;
            return i;
        }
    }
    return -1;
}

/* 打开读端 */
int ipc_fifo_open_read(int fifo_id)
{
    if (fifo_id < 0 || fifo_id >= FIFO_MAX || !fifos[fifo_id].used) return -1;
    if (fifos[fifo_id].magic != FIFO_MAGIC) return -1;
    fifos[fifo_id].read_open = 1;
    fifos[fifo_id].reader_count++;
    return 0;
}

/* 打开写端 */
int ipc_fifo_open_write(int fifo_id)
{
    if (fifo_id < 0 || fifo_id >= FIFO_MAX || !fifos[fifo_id].used) return -1;
    if (fifos[fifo_id].magic != FIFO_MAGIC) return -1;
    fifos[fifo_id].write_open = 1;
    fifos[fifo_id].writer_count++;
    return 0;
}

/* 从命名管道读取 */
int ipc_fifo_read(int fifo_id, void *buf, uint32_t size)
{
    if (fifo_id < 0 || fifo_id >= FIFO_MAX || !fifos[fifo_id].used) return -1;
    if (!fifos[fifo_id].read_open) return -1;
    if (!buf || size == 0) return 0;

    /* 没有数据且没有写者，返回0（EOF） */
    if (fifos[fifo_id].count == 0) {
        if (!fifos[fifo_id].write_open) return 0;
        return -2;  /* 需要等待数据 */
    }

    uint32_t avail = fifos[fifo_id].count;
    if (size > avail) size = avail;

    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) {
        dst[i] = fifos[fifo_id].buffer[fifos[fifo_id].read_pos];
        fifos[fifo_id].read_pos = (fifos[fifo_id].read_pos + 1) % FIFO_BUF_SIZE;
    }
    fifos[fifo_id].count -= size;
    fifos[fifo_id].bytes_read += size;
    return (int)size;
}

/* 写入命名管道 */
int ipc_fifo_write(int fifo_id, const void *buf, uint32_t size)
{
    if (fifo_id < 0 || fifo_id >= FIFO_MAX || !fifos[fifo_id].used) return -1;
    if (!fifos[fifo_id].write_open) return -1;
    if (!buf || size == 0) return 0;

    uint32_t space = FIFO_BUF_SIZE - fifos[fifo_id].count;
    if (size > space) size = space;
    if (size == 0) return -2;  /* 缓冲区满，需要等待 */

    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) {
        fifos[fifo_id].buffer[fifos[fifo_id].write_pos] = src[i];
        fifos[fifo_id].write_pos = (fifos[fifo_id].write_pos + 1) % FIFO_BUF_SIZE;
    }
    fifos[fifo_id].count += size;
    fifos[fifo_id].bytes_written += size;
    return (int)size;
}

/* 关闭读端 */
int ipc_fifo_close_read(int fifo_id)
{
    if (fifo_id < 0 || fifo_id >= FIFO_MAX || !fifos[fifo_id].used) return -1;
    if (fifos[fifo_id].reader_count > 0) {
        fifos[fifo_id].reader_count--;
    }
    if (fifos[fifo_id].reader_count == 0) {
        fifos[fifo_id].read_open = 0;
    }
    return 0;
}

/* 关闭写端 */
int ipc_fifo_close_write(int fifo_id)
{
    if (fifo_id < 0 || fifo_id >= FIFO_MAX || !fifos[fifo_id].used) return -1;
    if (fifos[fifo_id].writer_count > 0) {
        fifos[fifo_id].writer_count--;
    }
    if (fifos[fifo_id].writer_count == 0) {
        fifos[fifo_id].write_open = 0;
    }
    return 0;
}

/* 删除命名管道 */
int ipc_fifo_unlink(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < FIFO_MAX; i++) {
        if (fifos[i].used && strcmp(fifos[i].name, name) == 0) {
            fifos[i].used = 0;
            fifos[i].magic = 0;
            fifo_count--;
            return 0;
        }
    }
    return -1;
}

/* 命名管道统计 */
void ipc_fifo_stats(uint32_t *total, uint32_t *used)
{
    if (total) *total = FIFO_MAX;
    if (used) *used = fifo_count;
}

/* 根据名字查找 */
int ipc_fifo_lookup(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < FIFO_MAX; i++) {
        if (fifos[i].used && strcmp(fifos[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
