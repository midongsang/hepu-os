/* ============================================================================
 * 赫普OS - CPU特性检测 (V0.99新增)
 * 实现：CPUID指令检测CPU厂商、特性、缓存、核心数
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "cpu.h"
#include "vga.h"
#include "string.h"

/* CPU信息结构 */
typedef struct {
    char     vendor[13];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t features_ecx;
    uint32_t features_edx;
    uint32_t ext_features_ecx;
    uint32_t ext_features_edx;
    uint32_t max_function;
    uint32_t max_ext_function;
    uint32_t cache_line_size;
    uint32_t cache_size_kb;
    uint32_t core_count;
    uint8_t  has_cpuid;
} cpu_info_t;

static cpu_info_t cpu;

/* 执行CPUID指令 */
static void cpuid(uint32_t func, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(func)
    );
}

/* 检测CPUID是否可用 */
static int detect_cpuid(void)
{
    uint32_t eflags;
    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %%eax\n\t"
        "xorl $0x200000, %%eax\n\t"
        "pushl %%eax\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %%eax\n\t"
        "xorl %0, %%eax\n\t"
        : "=r"(eflags)
        :
        : "eax"
    );
    return (eflags & 0x200000) ? 1 : 0;
}

/* 初始化CPU检测 */
void cpu_init(void)
{
    memset(&cpu, 0, sizeof(cpu));
    cpu.has_cpuid = detect_cpuid();

    if (!cpu.has_cpuid) {
        vga_puts("[CPU] CPU not supports CPUID instructions \n");
        return;
    }

    uint32_t eax, ebx, ecx, edx;

    /* Function 0: 厂商ID + 最大功能号 */
    cpuid(0, &eax, &ebx, &ecx, &edx);
    cpu.max_function = eax;
    ((uint32_t *)cpu.vendor)[0] = ebx;
    ((uint32_t *)cpu.vendor)[1] = edx;
    ((uint32_t *)cpu.vendor)[2] = ecx;
    cpu.vendor[12] = '\0';

    /* Function 1: 特性标志 */
    if (cpu.max_function >= 1) {
        cpuid(1, &eax, &ebx, &ecx, &edx);
        cpu.family = (eax >> 8) & 0xF;
        cpu.model = (eax >> 4) & 0xF;
        cpu.stepping = eax & 0xF;
        cpu.features_ecx = ecx;
        cpu.features_edx = edx;
        cpu.cache_line_size = ((ebx >> 8) & 0xFF) * 8;
        cpu.core_count = (ebx >> 16) & 0xFF;
    }

    /* 扩展功能 */
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    cpu.max_ext_function = eax;

    if (cpu.max_ext_function >= 0x80000001) {
        cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        cpu.ext_features_ecx = ecx;
        cpu.ext_features_edx = edx;
    }

    /* 缓存信息（Intel） */
    if (cpu.max_function >= 4 && strncmp(cpu.vendor, "GenuineIntel", 12) == 0) {
        cpuid(4, &eax, &ebx, &ecx, &edx);
        uint32_t ways = (ebx >> 22) + 1;
        uint32_t partitions = ((ebx >> 12) & 0x3FF) + 1;
        uint32_t line_size = (ebx & 0xFFF) + 1;
        uint32_t sets = ecx + 1;
        cpu.cache_size_kb = (ways * partitions * line_size * sets) / 1024;
    }

    vga_printf("[CPU] vendor: %s\n", cpu.vendor);
    vga_printf("[CPU] family: %d model: %d stepping: %d\n",
               cpu.family, cpu.model, cpu.stepping);
    vga_printf("[CPU] cores count: %d cache line: %d bytes \n",
               cpu.core_count, cpu.cache_line_size);
    if (cpu.cache_size_kb > 0) {
        vga_printf("[CPU] L2 cache: %d KB\n", cpu.cache_size_kb);
    }
}

/* 特性检测 */
int cpu_has_feature(cpu_feature_t feature)
{
    if (!cpu.has_cpuid) return 0;
    switch (feature) {
        case CPU_FEAT_FPU:    return (cpu.features_edx & (1 << 0)) ? 1 : 0;
        case CPU_FEAT_PSE:    return (cpu.features_edx & (1 << 3)) ? 1 : 0;
        case CPU_FEAT_TSC:    return (cpu.features_edx & (1 << 4)) ? 1 : 0;
        case CPU_FEAT_MSR:    return (cpu.features_edx & (1 << 5)) ? 1 : 0;
        case CPU_FEAT_PAE:    return (cpu.features_edx & (1 << 6)) ? 1 : 0;
        case CPU_FEAT_APIC:   return (cpu.features_edx & (1 << 9)) ? 1 : 0;
        case CPU_FEAT_SEP:    return (cpu.features_edx & (1 << 11)) ? 1 : 0;
        case CPU_FEAT_MTRR:   return (cpu.features_edx & (1 << 12)) ? 1 : 0;
        case CPU_FEAT_PGE:    return (cpu.features_edx & (1 << 13)) ? 1 : 0;
        case CPU_FEAT_MMX:    return (cpu.features_edx & (1 << 23)) ? 1 : 0;
        case CPU_FEAT_SSE:    return (cpu.features_edx & (1 << 25)) ? 1 : 0;
        case CPU_FEAT_SSE2:   return (cpu.features_edx & (1 << 26)) ? 1 : 0;
        case CPU_FEAT_HTT:    return (cpu.features_edx & (1 << 28)) ? 1 : 0;
        case CPU_FEAT_SSE3:   return (cpu.features_ecx & (1 << 0)) ? 1 : 0;
        case CPU_FEAT_SSSE3:  return (cpu.features_ecx & (1 << 9)) ? 1 : 0;
        case CPU_FEAT_SSE4_1: return (cpu.features_ecx & (1 << 19)) ? 1 : 0;
        case CPU_FEAT_SSE4_2: return (cpu.features_ecx & (1 << 20)) ? 1 : 0;
        case CPU_FEAT_AVX:    return (cpu.features_ecx & (1 << 28)) ? 1 : 0;
        case CPU_FEAT_NX:     return (cpu.ext_features_edx & (1 << 20)) ? 1 : 0;
        case CPU_FEAT_64BIT:  return (cpu.ext_features_edx & (1 << 29)) ? 1 : 0;
        default: return 0;
    }
}

/* 获取CPU信息 */
void cpu_get_info(cpu_info_data_t *info)
{
    if (!info) return;
    memcpy(info->vendor, cpu.vendor, 12);
    info->vendor[12] = '\0';
    info->family = cpu.family;
    info->model = cpu.model;
    info->stepping = cpu.stepping;
    info->core_count = cpu.core_count;
    info->cache_line_size = cpu.cache_line_size;
    info->cache_size_kb = cpu.cache_size_kb;
    info->has_cpuid = cpu.has_cpuid;
}

/* 读取时间戳计数器 */
uint64_t cpu_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* 读取MSR */
uint64_t cpu_rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/* 写入MSR */
void cpu_wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}
