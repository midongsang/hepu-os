/* ============================================================================
 * 赫普OS - 文件服务 (用户态)
 * 微内核架构下，文件系统运行在用户态，通过IPC与内核通信
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu_service.h"

#define SERVICE_NAME "file-server"
#define SERVICE_VERSION "0.0.1"

/* 文件系统类型 */
typedef enum {
    FS_TYPE_HEPUFS = 0,   /* 赫普原生文件系统 */
    FS_TYPE_FAT32,        /* 兼容老设备 */
    FS_TYPE_EXT4,         /* 兼容Linux */
    FS_TYPE_ISO9660       /* 光盘 */
} fs_type_t;

/* 挂载点 */
typedef struct {
    char     path[64];
    fs_type_t type;
    uint32_t device_id;
    int      mounted;
} mount_point_t;

#define MAX_MOUNTS 16
static mount_point_t mounts[MAX_MOUNTS];

/* 服务初始化 */
static int file_server_init(void)
{
    for (int i = 0; i < MAX_MOUNTS; i++) {
        mounts[i].mounted = 0;
    }

    /* 注册根文件系统 */
    mounts[0].type = FS_TYPE_HEPUFS;
    mounts[0].device_id = 0;
    mounts[0].mounted = 1;
    /* strcpy 简化 */
    const char *root = "/";
    for (int i = 0; root[i]; i++) mounts[0].path[i] = root[i];
    mounts[0].path[1] = '\0';

    hepu_log(SERVICE_NAME, "文件服务初始化完成");
    hepu_log(SERVICE_NAME, "根文件系统已挂载 (HEPUFS)");
    return 0;
}

/* 处理IPC消息 */
static void file_server_handle_msg(ipc_message_t *msg)
{
    switch (msg->type) {
        case 0x1001: /* 打开文件 */
            break;
        case 0x1002: /* 读取文件 */
            break;
        case 0x1003: /* 写入文件 */
            break;
        case 0x1004: /* 关闭文件 */
            break;
        case 0x1005: /* 挂载 */
            break;
        default:
            break;
    }
}

/* 服务主函数 */
int hepu_service_main(int argc, char **argv)
{
    if (file_server_init() != 0) {
        return -1;
    }

    /* 主循环：等待IPC消息 */
    while (1) {
        ipc_message_t msg;
        if (hepu_ipc_receive(&msg) == 0) {
            file_server_handle_msg(&msg);
        }
        hepu_yield();
    }

    return 0;
}
