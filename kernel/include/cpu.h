#ifndef CPU_H
#define CPU_H

#include <stdint.h>

/* CPU特性 */
typedef enum {
    CPU_FEAT_FPU = 0,
    CPU_FEAT_PSE,
    CPU_FEAT_TSC,
    CPU_FEAT_MSR,
    CPU_FEAT_PAE,
    CPU_FEAT_APIC,
    CPU_FEAT_SEP,
    CPU_FEAT_MTRR,
    CPU_FEAT_PGE,
    CPU_FEAT_MMX,
    CPU_FEAT_SSE,
    CPU_FEAT_SSE2,
    CPU_FEAT_HTT,
    CPU_FEAT_SSE3,
    CPU_FEAT_SSSE3,
    CPU_FEAT_SSE4_1,
    CPU_FEAT_SSE4_2,
    CPU_FEAT_AVX,
    CPU_FEAT_NX,
    CPU_FEAT_64BIT
} cpu_feature_t;

/* CPU信息 */
typedef struct {
    char     vendor[13];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t core_count;
    uint32_t cache_line_size;
    uint32_t cache_size_kb;
    uint8_t  has_cpuid;
} cpu_info_data_t;

/* 初始化CPU检测 */
void cpu_init(void);

/* 特性检测 */
int cpu_has_feature(cpu_feature_t feature);

/* 获取CPU信息 */
void cpu_get_info(cpu_info_data_t *info);

/* 读取时间戳计数器 */
uint64_t cpu_rdtsc(void);

/* 读取/写入MSR */
uint64_t cpu_rdmsr(uint32_t msr);
void     cpu_wrmsr(uint32_t msr, uint64_t value);

#endif
