#ifndef IPC_FIFO_H
#define IPC_FIFO_H

#include <stdint.h>

/* 初始化命名管道子系统 */
void ipc_fifo_init(void);

/* 创建命名管道 */
int ipc_fifo_create(const char *name);

/* 打开/关闭 */
int ipc_fifo_open_read(int fifo_id);
int ipc_fifo_open_write(int fifo_id);
int ipc_fifo_close_read(int fifo_id);
int ipc_fifo_close_write(int fifo_id);

/* 读写 */
int ipc_fifo_read(int fifo_id, void *buf, uint32_t size);
int ipc_fifo_write(int fifo_id, const void *buf, uint32_t size);

/* 删除 */
int ipc_fifo_unlink(const char *name);

/* 查找 */
int ipc_fifo_lookup(const char *name);

/* 统计 */
void ipc_fifo_stats(uint32_t *total, uint32_t *used);

#endif
