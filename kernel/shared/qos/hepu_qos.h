/* ============================================================================
 * 赫普OS - 域级 QoS 统一接口
 *
 * 上层系统共用此头文件，32位/64位内核各自实现 qos_* 接口。
 * QoS 挂靠到 domain，调度器按域的 cpu_weight 加权分配 CPU。
 * ========================================================================== */

#ifndef HEPU_SHARED_QOS_H
#define HEPU_SHARED_QOS_H

#include <stdint.h>
#include "shared/domain/hepu_domain.h"  /* qos_class_t */

/* QoS 等级 → CPU 权重映射表
 * 权重越大，同优先级下获得的 CPU 时间越多 */
static const uint32_t qos_class_weight[QOS_CLASS_COUNT] = {
    [QOS_CLASS_RT]         = 10,
    [QOS_CLASS_LATENCY]    = 8,
    [QOS_CLASS_THROUGHPUT] = 4,
    [QOS_CLASS_AI]         = 2,
    [QOS_CLASS_BACKGROUND] = 1,
};

/* 预置域的默认 QoS 等级 */
static const qos_class_t domain_default_qos[DOMAIN_TYPE_COUNT] = {
    [DOMAIN_TYPE_KERNEL]         = QOS_CLASS_RT,
    [DOMAIN_TYPE_SYSTEM]         = QOS_CLASS_LATENCY,
    [DOMAIN_TYPE_GUI]            = QOS_CLASS_LATENCY,
    [DOMAIN_TYPE_LINUX_COMPAT]   = QOS_CLASS_THROUGHPUT,
    [DOMAIN_TYPE_ANDROID_COMPAT] = QOS_CLASS_THROUGHPUT,
    [DOMAIN_TYPE_USER]           = QOS_CLASS_BACKGROUND,
};

/* 初始化 QoS 子系统（为所有预置域设置默认 class/weight） */
void qos_init(void);

/* 设置域的 QoS 等级，同时更新 cpu_weight */
int qos_set_class(int domain_id, qos_class_t cls);

/* 获取域的 QoS 等级 */
qos_class_t qos_get_class(int domain_id);

/* 获取域的 CPU 权重 */
uint32_t qos_get_weight(int domain_id);

/* class → weight 转换 */
static inline uint32_t qos_class_to_weight(qos_class_t cls)
{
    if (cls < 0 || cls >= QOS_CLASS_COUNT) return 1;
    return qos_class_weight[cls];
}

#endif /* HEPU_SHARED_QOS_H */
