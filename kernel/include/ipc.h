#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include <stddef.h>

#define IPC_MAX_MESSAGES 128  /* V6.77: 从32扩展到128 */
#define IPC_MSG_SIZE 256
#define IPC_MAX_QUEUES 64     /* V6.77: 从16扩展到64 */
#define IPC_MAX_PIPES 32      /* V6.77: 从8扩展到32 */
#define IPC_PIPE_BUF 4096
#define IPC_MAX_SHM 32        /* V6.77: 从8扩展到32 */

/* 消息优先级（数值越大优先级越高） */
#define IPC_PRIO_LOW      0
#define IPC_PRIO_NORMAL   1
#define IPC_PRIO_HIGH     2
#define IPC_PRIO_URGENT   3

/* 消息结构 */
typedef struct {
    uint32_t sender_pid;
    uint32_t type;
    uint8_t  priority;    /* 消息优先级 (V1.02新增) */
    uint32_t length;
    uint8_t  data[IPC_MSG_SIZE];
} ipc_message_t;

/* 消息队列 */
typedef struct {
    int used;
    uint32_t owner_pid;
    ipc_message_t messages[IPC_MAX_MESSAGES];
    int head, tail;
    int count;
} ipc_queue_t;

/* 管道 */
typedef struct {
    int used;
    uint8_t buffer[IPC_PIPE_BUF];
    int read_pos, write_pos;
    int count;
    int read_end_open;
    int write_end_open;
    uint32_t reader_pid;
    uint32_t writer_pid;
} ipc_pipe_t;

/* 共享内存段 */
typedef struct {
    int used;
    uint32_t key;
    void *phys_addr;
    uint32_t size;
    int attach_count;
    /* V7.70安全修复: 添加owner和权限位 */
    uint32_t owner_pid;   /* 创建者PID */
    uint32_t mode;        /* 权限位 (类似UNIX: 0644=owner读写,其他只读) */
} ipc_shm_t;

/* 信号编号 */
#define SIGINT    2
#define SIGKILL   9
#define SIGTERM   15
#define SIGUSR1   10
#define SIGUSR2   12
#define SIGCHLD   17
#define NSIG      32

void ipc_init(void);

/* 消息队列 */
int ipc_queue_create(uint32_t pid);
int ipc_queue_destroy(uint32_t pid);
int ipc_send(uint32_t target_pid, uint32_t type, const void *data, uint32_t length);
int ipc_send_prio(uint32_t target_pid, uint8_t priority, uint32_t type, const void *data, uint32_t length);
int ipc_recv(uint32_t pid, ipc_message_t *msg);
int ipc_try_recv(uint32_t pid, ipc_message_t *msg);
int ipc_pending(uint32_t pid);

/* 管道 */
int ipc_pipe(int *read_fd, int *write_fd);
int ipc_pipe_read(int pipe_id, void *buf, uint32_t size);
int ipc_pipe_write(int pipe_id, const void *buf, uint32_t size);

/* 共享内存 */
int ipc_shm_get(uint32_t key, uint32_t size);
void *ipc_shm_attach(int shm_id, uint32_t pid);
int ipc_shm_detach(int shm_id, uint32_t pid);
int ipc_shm_release(int shm_id, uint32_t pid);   /* V3.77: 释放共享内存段 */

/* 信号 */
int ipc_kill(uint32_t pid, int sig);
int ipc_signal(int sig, void (*handler)(int));
int ipc_sigpending(uint32_t pid);

#endif /* IPC_H */
