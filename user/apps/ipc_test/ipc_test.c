/* ============================================================================
 * 赫普OS - IPC 进程间通信测试应用
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

/* IPC消息结构 (与内核一致) */
typedef struct {
    uint32_t sender_pid;
    uint32_t type;
    uint32_t length;
    uint8_t  data[256];
} ipc_msg_t;

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("=== IPC 进程间通信测试 ===\n");
    printf("当前 PID: %d\n", hepu_getpid());

    /* 测试1: 消息队列 - 给自己发消息 */
    printf("\n[测试1] 消息队列 (自发自收)\n");
    const char *msg_data = "Hello from IPC!";
    int ret = hepu_ipc_send(hepu_getpid(), 1, msg_data, strlen(msg_data) + 1);
    printf("  发送: ret=%d, data='%s'\n", ret, msg_data);

    ipc_msg_t recv_msg;
    ret = hepu_ipc_recv(&recv_msg);
    printf("  接收: ret=%d, sender=%d, type=%d, len=%d\n",
           ret, recv_msg.sender_pid, recv_msg.type, recv_msg.length);
    printf("  数据: '%s'\n", recv_msg.data);

    /* 测试2: 管道 */
    printf("\n[测试2] 管道\n");
    int fds[2];
    ret = hepu_pipe(fds);
    printf("  创建管道: ret=%d, read_fd=%d, write_fd=%d\n", ret, fds[0], fds[1]);

    const char *pipe_data = "Pipe data test";
    int w = hepu_write(fds[1], pipe_data, strlen(pipe_data) + 1);
    printf("  写入: %d bytes, '%s'\n", w, pipe_data);

    char pipe_buf[64];
    int r = hepu_read(fds[0], pipe_buf, 64);
    printf("  读取: %d bytes, '%s'\n", r, pipe_buf);

    /* 测试3: 共享内存 */
    printf("\n[测试3] 共享内存\n");
    int shm_id = hepu_shmget(0x1234, 4096);
    printf("  shmget: id=%d\n", shm_id);

    char *shm_ptr = (char *)hepu_shmat(shm_id);
    printf("  shmat: ptr=0x%x\n", (uint32_t)shm_ptr);

    const char *shm_data = "Shared memory content";
    strcpy(shm_ptr, shm_data);
    printf("  写入共享内存: '%s'\n", shm_ptr);

    /* 再次附加验证 */
    char *shm_ptr2 = (char *)hepu_shmat(shm_id);
    printf("  再次附加: ptr=0x%x, 内容='%s'\n", (uint32_t)shm_ptr2, shm_ptr2);

    /* 测试4: 信号 */
    printf("\n[测试4] 信号\n");
    ret = hepu_kill(hepu_getpid(), 10);  /* SIGUSR1 */
    printf("  kill(SIGUSR1): ret=%d\n", ret);

    ret = hepu_kill(hepu_getpid(), 2);   /* SIGINT */
    printf("  kill(SIGINT): ret=%d\n", ret);

    printf("\n=== IPC 测试完成 ===\n");
    return 0;
}
