/* ============================================================================
 * 赫普OS - HepuShell IPC 接口
 * 基于内核消息队列的进程间通信
 * V0.10.164
 * ========================================================================== */

#ifndef HSIPC_H
#define HSIPC_H

#include <stdint.h>
#include <stddef.h>

/* 最大配置 */
#define HSIPC_MAX_MESSAGES  64
#define HSIPC_MAX_MSG_LEN   256
#define HSIPC_MAX_CHANNELS  8

/* 消息类型 */
typedef enum {
    HSIPC_MSG_NONE = 0,
    HSIPC_CMD_RELOAD,       /* 重新加载配置 */
    HSIPC_CMD_THEME,        /* 切换主题 */
    HSIPC_CMD_WORKSPACE,    /* 切换工作区 */
    HSIPC_CMD_LAUNCHER,     /* 切换启动器 */
    HSIPC_CMD_QUIT,         /* 退出HepuShell */
    HSIPC_CMD_EXEC,         /* 执行命令 */
    HSIPC_MSG_NOTIFY,       /* 通知消息 */
    HSIPC_MSG_RESPONSE,     /* 响应消息 */
} hsipc_msg_type_t;

/* 消息 */
typedef struct {
    hsipc_msg_type_t type;
    int sender;
    int target;
    char data[HSIPC_MAX_MSG_LEN];
    int data_len;
    int processed;
} hsipc_message_t;

/* 通道 */
typedef struct {
    int id;
    char name[32];
    int message_count;
    hsipc_message_t messages[HSIPC_MAX_MESSAGES];
    int read_pos;
    int write_pos;
} hsipc_channel_t;

/* API */
int hsipc_init(void);
int hsipc_create_channel(const char *name);
int hsipc_find_channel(const char *name);
int hsipc_send_message(int channel, hsipc_msg_type_t type,
                       const char *data, int len);
int hsipc_recv_message(int channel, hsipc_message_t *msg);
int hsipc_poll_channel(int channel);
void hsipc_process_messages(void);
int hsipc_send_command(const char *command);

/* HepuShell IPC 命令处理 */
void hsipc_handle_shell_command(const char *cmd);

#endif /* HSIPC_H */
