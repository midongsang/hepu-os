/* ============================================================================
 * 赫普OS - 服务注册表（微内核服务发现机制）
 * V6.64: 支持用户态服务注册和查找
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "vga.h"
#include "sched.h"

#define MAX_SERVICES 16
#define SERVICE_NAME_LEN 32

typedef struct {
    int used;
    char name[SERVICE_NAME_LEN];
    uint32_t pid;
    uint32_t type;       // 服务类型
    uint32_t version;    // 服务版本
} service_entry_t;

static service_entry_t service_table[MAX_SERVICES];

/* 初始化服务注册表 */
void service_init(void) {
    for (int i = 0; i < MAX_SERVICES; i++) {
        service_table[i].used = 0;
    }
    vga_puts("[SERVICE] service registry initialized (max 16 services)\n");
}

/* 注册服务
 * name: 服务名称（最多31字符）
 * type: 服务类型
 * version: 服务版本
 * 返回: 0成功，-1失败
 */
int sys_service_register(const char *name, uint32_t type, uint32_t version) {
    if (!name) return -1;

    uint32_t pid = sched_current_pid();

    // 检查是否已注册（同一PID可以更新）
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (service_table[i].used && service_table[i].pid == pid) {
            // 更新已有注册
            strncpy(service_table[i].name, name, SERVICE_NAME_LEN - 1);
            service_table[i].name[SERVICE_NAME_LEN - 1] = 0;
            service_table[i].type = type;
            service_table[i].version = version;
            vga_printf("[SERVICE] updated: %s (pid=%d, type=%d, ver=%d)\n",
                       service_table[i].name, pid, type, version);
            return 0;
        }
    }

    // 查找空槽位
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (!service_table[i].used) {
            service_table[i].used = 1;
            strncpy(service_table[i].name, name, SERVICE_NAME_LEN - 1);
            service_table[i].name[SERVICE_NAME_LEN - 1] = 0;
            service_table[i].pid = pid;
            service_table[i].type = type;
            service_table[i].version = version;
            vga_printf("[SERVICE] registered: %s (pid=%d, type=%d, ver=%d)\n",
                       service_table[i].name, pid, type, version);
            return 0;
        }
    }

    vga_puts("[SERVICE] ERROR: registry full\n");
    return -1;
}

/* 查找服务
 * name: 服务名称
 * 返回: 服务PID，0表示未找到
 */
uint32_t sys_service_lookup(const char *name) {
    if (!name) return 0;

    for (int i = 0; i < MAX_SERVICES; i++) {
        if (service_table[i].used &&
            strncmp(service_table[i].name, name, SERVICE_NAME_LEN) == 0) {
            return service_table[i].pid;
        }
    }
    return 0;
}

/* 注销服务（进程退出时自动调用） */
void service_unregister_pid(uint32_t pid) {
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (service_table[i].used && service_table[i].pid == pid) {
            vga_printf("[SERVICE] unregistered: %s (pid=%d)\n",
                       service_table[i].name, pid);
            service_table[i].used = 0;
        }
    }
}

/* 列出所有已注册服务（调试用） */
void service_list(void) {
    vga_puts("[SERVICE] registered services:\n");
    int count = 0;
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (service_table[i].used) {
            vga_printf("  [%d] %s pid=%d type=%d ver=%d\n",
                       i, service_table[i].name,
                       service_table[i].pid,
                       service_table[i].type,
                       service_table[i].version);
            count++;
        }
    }
    if (count == 0) {
        vga_puts("  (none)\n");
    }
}
