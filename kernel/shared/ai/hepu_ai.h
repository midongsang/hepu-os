/* ============================================================================
 * 赫普OS - Hepu AI 助手（hepua）统一接口
 *
 * 上层系统共用此头文件，32位/64位内核各自实现。
 * 骨架阶段提供规则诊断（系统状态摘要、异常分析、进程检查），
 * 后续可接入真实 AI 后端。
 * ========================================================================== */

#ifndef HEPU_SHARED_AI_H
#define HEPU_SHARED_AI_H

#include <stdint.h>

/* 诊断类型 */
typedef enum {
    HEPUAI_SYSCHECK = 0,   /* 系统自检：内存/进程/域/QoS 状态摘要 */
    HEPUAI_PANIC    = 1,   /* panic 解析（最近一次崩溃信息） */
    HEPUAI_DEADLOCK = 2,   /* 死锁检测（进程/锁状态分析） */
    HEPUAI_EXPLAIN  = 3,   /* 自由文本解释（骨架返回模板） */
    HEPUAI_TYPE_COUNT
} hepua_diag_type_t;

/* 诊断结果（写入调用方缓冲区） */
typedef struct {
    int    type;           /* hepua_diag_type_t */
    int    ok;             /* 0=正常, 1=发现问题 */
    char   summary[128];   /* 一句话摘要 */
    char   detail[512];    /* 详细分析 */
} hepua_result_t;

/* 初始化 hepua */
void hepua_init(void);

/* 执行诊断，结果写入 result */
void hepua_diagnose(hepua_diag_type_t type, hepua_result_t *result);

/* 自由文本解释（骨架阶段返回通用模板） */
void hepua_explain(const char *query, char *out, int out_len);

/* 获取 hepua 状态字符串（用于 shell 显示） */
const char *hepua_status(void);

#endif /* HEPU_SHARED_AI_H */
