/* ============================================================================
 * 赫普OS - 用户态服务公共头文件
 * 所有用户态服务通过此头文件使用系统服务接口
 * ========================================================================== */

#ifndef HEPU_SERVICE_H
#define HEPU_SERVICE_H

#include <stdint.h>
#include <stddef.h>
#include "ipc.h"

/* 服务主函数入口 */
int hepu_service_main(int argc, char **argv);

/* 系统调用封装 */
void     hepu_log(const char *service, const char *msg);
void     hepu_yield(void);
int      hepu_ipc_receive(ipc_message_t *msg);
int      hepu_ipc_send(uint32_t target, uint32_t type,
                       const void *data, uint32_t length);
uint32_t hepu_get_pid(void);

/* 服务注册 */
int hepu_register_service(const char *name, uint32_t version);

#endif /* HEPU_SERVICE_H */
