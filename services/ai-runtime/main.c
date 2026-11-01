/* ============================================================================
 * 赫普OS - AI运行时基座 (用户态系统服务)
 * AI不是独立APP，是系统底层服务
 * 支持本地离线推理、跨应用信息整合、NPU算力调度
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu_service.h"

#define SERVICE_NAME "ai-runtime"

/* AI硬件能力 */
typedef enum {
    AI_HW_NONE = 0,     /* 无NPU，CPU推理或关闭 */
    AI_HW_NPU,          /* 专用NPU */
    AI_HW_GPU,          /* GPU推理 */
    AI_HW_DSP           /* DSP推理 */
} ai_hw_type_t;

/* AI任务 */
typedef struct {
    uint32_t task_id;
    uint32_t owner_pid;
    uint32_t model_id;
    ai_hw_type_t hw_type;
    int      priority;
    int      running;
} ai_task_t;

#define MAX_AI_TASKS 32
static ai_task_t tasks[MAX_AI_TASKS];
static ai_hw_type_t current_hw = AI_HW_NONE;
static int ai_enabled = 1;

static int ai_runtime_init(void)
{
    for (int i = 0; i < MAX_AI_TASKS; i++) {
        tasks[i].running = 0;
    }

    /* 检测NPU硬件 */
    /* 原型阶段：假设无NPU，老设备自动降级 */
    current_hw = AI_HW_NONE;
    ai_enabled = 0; /* 无NPU时默认关闭，避免高耗电 */

    hepu_log(SERVICE_NAME, "AI运行时基座初始化完成");
    if (current_hw == AI_HW_NONE) {
        hepu_log(SERVICE_NAME, "未检测到NPU，AI能力已降级/关闭 (省电模式)");
    } else {
        hepu_log(SERVICE_NAME, "NPU检测成功，本地离线推理已启用");
    }
    return 0;
}

/* AI权限检查：AI访问用户数据必须经过授权 */
static int ai_check_permission(uint32_t owner_pid, const char *data_type)
{
    /* 原型：始终返回需要授权
     * 实际实现中查询隐私金库的授权表 */
    return 0; /* 0=未授权，拒绝访问 */
}

static void ai_runtime_handle_msg(ipc_message_t *msg)
{
    switch (msg->type) {
        case 0x4001: /* 推理请求 */
            if (!ai_enabled) {
                /* 返回AI不可用 */
                break;
            }
            break;
        case 0x4002: /* 加载模型 */
            break;
        case 0x4003: /* 跨应用信息查询 (需权限检查) */
            break;
        case 0x4004: /* 通知分级 */
            break;
        case 0x4005: /* 算力调度查询 */
            break;
        default:
            break;
    }
}

int hepu_service_main(int argc, char **argv)
{
    if (ai_runtime_init() != 0) return -1;

    while (1) {
        ipc_message_t msg;
        if (hepu_ipc_receive(&msg) == 0) {
            ai_runtime_handle_msg(&msg);
        }
        hepu_yield();
    }
    return 0;
}
