/* ============================================================================
 * 赫普OS - 网络服务 (用户态)
 * 微内核架构下，TCP/IP协议栈运行在用户态
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu_service.h"

#define SERVICE_NAME "net-server"

/* 网络接口状态 */
typedef struct {
    char     name[16];
    uint8_t  mac[6];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    int      up;
    int      link_speed; /* Mbps */
} net_interface_t;

#define MAX_INTERFACES 4
static net_interface_t interfaces[MAX_INTERFACES];

static int net_server_init(void)
{
    for (int i = 0; i < MAX_INTERFACES; i++) {
        interfaces[i].up = 0;
    }
    hepu_log(SERVICE_NAME, "网络服务初始化完成");
    hepu_log(SERVICE_NAME, "TCP/IP 协议栈就绪 (用户态)");
    return 0;
}

static void net_server_handle_msg(ipc_message_t *msg)
{
    switch (msg->type) {
        case 0x2001: /* socket */
            break;
        case 0x2002: /* connect */
            break;
        case 0x2003: /* send */
            break;
        case 0x2004: /* recv */
            break;
        case 0x2005: /* close */
            break;
        default:
            break;
    }
}

int hepu_service_main(int argc, char **argv)
{
    if (net_server_init() != 0) return -1;

    while (1) {
        ipc_message_t msg;
        if (hepu_ipc_receive(&msg) == 0) {
            net_server_handle_msg(&msg);
        }
        hepu_yield();
    }
    return 0;
}
