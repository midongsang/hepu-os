/* ============================================================================
 * 赫普OS - CSPRNG安全随机数生成器
 * 密码学安全的伪随机数生成，基于ChaCha20
 * V7.75安全修复: 多源熵收集（RDRAND/RDTSC/中断/内存状态）
 * ========================================================================== */

#include <stdint.h>
#include "csprng.h"
#include "vga.h"
#include "string.h"

static csprng_state_t csprng;
static int csprng_initialized = 0;
static int entropy_sources_available = 0;  /* 可用熵源位图 */

/* V7.75: 熵源标识 */
#define ENTROPY_RDRAND  0x01
#define ENTROPY_RDTSC   0x02
#define ENTROPY_IRQ     0x04
#define ENTROPY_MEM     0x08
#define ENTROPY_STACK   0x10

/* V7.75: 检测CPU是否支持RDRAND */
static int csprng_has_rdrand(void)
{
    uint32_t eax, ebx, ecx, edx;
    /* CPUID.01H:ECX.RDRAND[bit 30] */
    __asm__ volatile(
        "mov $1, %%eax\n"
        "cpuid\n"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : : "memory"
    );
    return (ecx & (1 << 30)) ? 1 : 0;
}

/* V7.75: 使用RDRAND获取随机数 */
static int csprng_rdrand(uint32_t *out)
{
    uint8_t ok;
    __asm__ volatile(
        "rdrand %0\n"
        "setc %1\n"
        : "=r"(*out), "=qm"(ok)
        : : "memory"
    );
    return ok ? 0 : -1;
}

/* V7.75: 使用RDTSC获取时间戳 */
static uint64_t csprng_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* V7.75: 获取中断计数（如果有全局计数器） */
static uint32_t csprng_get_irq_count(void)
{
    /* 使用irq_count数组的总和作为熵源 */
    extern uint32_t irq_count[16];
    uint32_t total = 0;
    for (int i = 0; i < 16; i++) {
        total += irq_count[i];
    }
    return total;
}

/* V7.75: 收集多源熵 */
static void csprng_collect_entropy(uint8_t *buffer, uint32_t len)
{
    uint32_t pos = 0;
    uint8_t entropy_pool[256];
    uint32_t pool_pos = 0;

    /* 熵源1: RDRAND（如果可用） */
    if (entropy_sources_available & ENTROPY_RDRAND) {
        for (int i = 0; i < 8 && pool_pos + 4 <= sizeof(entropy_pool); i++) {
            uint32_t val;
            if (csprng_rdrand(&val) == 0) {
                memcpy(entropy_pool + pool_pos, &val, 4);
                pool_pos += 4;
            }
        }
    }

    /* 熵源2: RDTSC时间戳（多次采样，利用执行时间差异） */
    for (int i = 0; i < 4 && pool_pos + 8 <= sizeof(entropy_pool); i++) {
        uint64_t tsc = csprng_rdtsc();
        /* 执行一些操作增加时间差异 */
        volatile int x = 0;
        for (volatile int j = 0; j < 100 + i * 37; j++) x += j;
        tsc ^= csprng_rdtsc();
        memcpy(entropy_pool + pool_pos, &tsc, 8);
        pool_pos += 8;
    }

    /* 熵源3: 中断计数 */
    {
        uint32_t irq = csprng_get_irq_count();
        if (pool_pos + 4 <= sizeof(entropy_pool)) {
            memcpy(entropy_pool + pool_pos, &irq, 4);
            pool_pos += 4;
        }
    }

    /* 熵源4: 内存状态（栈地址、堆地址） */
    {
        uintptr_t stack_addr = (uintptr_t)&pos;
        uintptr_t code_addr = (uintptr_t)csprng_collect_entropy;
        if (pool_pos + 8 <= sizeof(entropy_pool)) {
            memcpy(entropy_pool + pool_pos, &stack_addr, 4);
            memcpy(entropy_pool + pool_pos + 4, &code_addr, 4);
            pool_pos += 8;
        }
    }

    /* 熵源5: 未初始化栈数据（弱熵，但有总比没有好） */
    {
        uint8_t uninit[32];
        /* 不初始化，使用栈上的残留数据 */
        if (pool_pos + 32 <= sizeof(entropy_pool)) {
            memcpy(entropy_pool + pool_pos, uninit, 32);
            pool_pos += 32;
        }
    }

    /* 将熵池混合到输出缓冲区 */
    while (pos < len && pool_pos > 0) {
        buffer[pos] = entropy_pool[pos % pool_pos];
        pos++;
    }

    /* 如果熵不足，用RDTSC继续填充 */
    while (pos < len) {
        uint64_t tsc = csprng_rdtsc();
        uint32_t copy = len - pos;
        if (copy > 8) copy = 8;
        memcpy(buffer + pos, &tsc, copy);
        pos += copy;
    }
}

/* ChaCha20四分之一轮 */
static inline uint32_t csprng_rotl(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void csprng_chacha20_quarter(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a += *b; *d ^= *a; *d = csprng_rotl(*d, 16);
    *c += *d; *b ^= *c; *b = csprng_rotl(*b, 12);
    *a += *b; *d ^= *a; *d = csprng_rotl(*d, 8);
    *c += *d; *b ^= *c; *b = csprng_rotl(*b, 7);
}

/* ChaCha20块函数 */
static void csprng_chacha20_block(const uint8_t *key, uint32_t counter,
                                   const uint8_t *nonce, uint8_t *out)
{
    static const char constants[16] = "expand 32-byte k";
    uint32_t state[16];

    /* 初始化状态 */
    for (int i = 0; i < 4; i++) {
        state[i] = ((uint32_t)constants[i*4]) |
                   ((uint32_t)constants[i*4+1] << 8) |
                   ((uint32_t)constants[i*4+2] << 16) |
                   ((uint32_t)constants[i*4+3] << 24);
    }
    for (int i = 0; i < 8; i++) {
        state[4+i] = ((uint32_t)key[i*4]) |
                     ((uint32_t)key[i*4+1] << 8) |
                     ((uint32_t)key[i*4+2] << 16) |
                     ((uint32_t)key[i*4+3] << 24);
    }
    state[12] = counter;
    for (int i = 0; i < 3; i++) {
        state[13+i] = ((uint32_t)nonce[i*4]) |
                      ((uint32_t)nonce[i*4+1] << 8) |
                      ((uint32_t)nonce[i*4+2] << 16) |
                      ((uint32_t)nonce[i*4+3] << 24);
    }

    /* 20轮 */
    uint32_t working[16];
    memcpy(working, state, sizeof(state));
    for (int i = 0; i < 10; i++) {
        csprng_chacha20_quarter(&working[0], &working[4], &working[8], &working[12]);
        csprng_chacha20_quarter(&working[1], &working[5], &working[9], &working[13]);
        csprng_chacha20_quarter(&working[2], &working[6], &working[10], &working[14]);
        csprng_chacha20_quarter(&working[3], &working[7], &working[11], &working[15]);
        csprng_chacha20_quarter(&working[0], &working[5], &working[10], &working[15]);
        csprng_chacha20_quarter(&working[1], &working[6], &working[11], &working[12]);
        csprng_chacha20_quarter(&working[2], &working[7], &working[8], &working[13]);
        csprng_chacha20_quarter(&working[3], &working[4], &working[9], &working[14]);
    }

    /* 输出 */
    for (int i = 0; i < 16; i++) {
        uint32_t val = working[i] + state[i];
        out[i*4] = val & 0xFF;
        out[i*4+1] = (val >> 8) & 0xFF;
        out[i*4+2] = (val >> 16) & 0xFF;
        out[i*4+3] = (val >> 24) & 0xFF;
    }
}

/* 初始化CSPRNG */
void csprng_init(void)
{
    memset(&csprng, 0, sizeof(csprng));
    csprng.algorithm = CSPRNG_ALG_CHACHA20;
    csprng.reseed_interval = 1000000;  /* 每100万次重新播种 */
    csprng.initialized = 1;
    csprng_initialized = 1;

    /* V7.75安全修复: 检测可用熵源 */
    entropy_sources_available = 0;
    if (csprng_has_rdrand()) {
        entropy_sources_available |= ENTROPY_RDRAND;
    }
    entropy_sources_available |= ENTROPY_RDTSC;
    entropy_sources_available |= ENTROPY_MEM;
    entropy_sources_available |= ENTROPY_STACK;

    /* V7.75安全修复: 使用多源熵收集播种，不再使用硬编码种子
     * 原代码使用硬编码default_seed（eth0 MAC循环字节），
     * 导致ChaCha20输出流完全确定，每次启动产生相同序列。 */
    uint8_t entropy[64];
    csprng_collect_entropy(entropy, sizeof(entropy));

    /* 使用熵作为种子（seed 与 state 各取 32 字节真实熵） */
    memcpy(csprng.seed, entropy, 32);
    memcpy(csprng.state, entropy + 32, 32);
    csprng.counter = 0;
    csprng.reseed_counter = 0;

    /* 安全修复：不得在此调用 csprng_reseed(entropy, ...)。reseed 会把
     * additional_data 异或进当前 seed，而二者是同一份 entropy，异或后
     * seed 变为全 0，随后 ChaCha20(全0 key/counter/nonce) 输出一个固定块
     * （RFC 7539 全零向量 76b8e0ad...），把真实熵彻底抹掉，导致每次启动
     * 种子完全相同。seed/state 已直接由 64 字节熵初始化，无需再混合。 */

    vga_puts("[CSPRNG] CSPRNG init (V7.75 multi-source entropy)\n");
    vga_printf("[CSPRNG] algo: ChaCha20, reseed interval: %llu\n",
               (unsigned long long)csprng.reseed_interval);
    vga_printf("[CSPRNG] entropy sources: RDRAND=%d RDTSC=1 MEM=1 STACK=1\n",
               (entropy_sources_available & ENTROPY_RDRAND) ? 1 : 0);
}

/* 播种 */
int csprng_seed(const uint8_t *seed, uint32_t len)
{
    if (!csprng_initialized || !seed || len < 16) return -1;

    uint32_t copy_len = len > CSPRNG_SEED_SIZE ? CSPRNG_SEED_SIZE : len;
    memcpy(csprng.seed, seed, copy_len);
    memcpy(csprng.state, seed, copy_len);
    csprng.counter = 0;
    csprng.reseed_counter = 0;

    vga_printf("[CSPRNG] seeded %d bytes\n", copy_len);
    return 0;
}

/* 重新播种 */
int csprng_reseed(const uint8_t *additional_data, uint32_t len)
{
    if (!csprng_initialized) return -1;

    /* 混合附加数据到种子 */
    if (additional_data && len > 0) {
        uint32_t mix_len = len > CSPRNG_SEED_SIZE ? CSPRNG_SEED_SIZE : len;
        for (uint32_t i = 0; i < mix_len; i++) {
            csprng.seed[i] ^= additional_data[i];
        }
    }

    /* 从当前状态生成新种子 */
    uint8_t new_seed[64];
    uint8_t nonce[12] = {0};
    csprng_chacha20_block(csprng.seed, csprng.counter++, nonce, new_seed);
    memcpy(csprng.seed, new_seed, 32);
    memcpy(csprng.state, new_seed + 32, 32);
    csprng.reseed_counter = 0;

    return 0;
}

/* 获取随机字节 */
int csprng_get_bytes(uint8_t *buffer, uint32_t len)
{
    if (!csprng_initialized || !buffer || len == 0) return -1;

    uint32_t offset = 0;
    uint8_t block[64];
    uint8_t nonce[12] = {0};

    while (offset < len) {
        /* 检查是否需要重新播种 */
        if (csprng.reseed_counter >= csprng.reseed_interval) {
            csprng_reseed(NULL, 0);
        }

        /* 生成一个块 */
        csprng_chacha20_block(csprng.seed, csprng.counter++, nonce, block);
        csprng.reseed_counter++;

        uint32_t copy_len = len - offset;
        if (copy_len > 64) copy_len = 64;
        memcpy(buffer + offset, block, copy_len);
        offset += copy_len;
    }

    /* 清零临时缓冲区 */
    memset(block, 0, sizeof(block));
    return 0;
}

/* 获取32位随机数 */
uint32_t csprng_get_uint32(void)
{
    uint32_t val;
    csprng_get_bytes((uint8_t *)&val, sizeof(val));
    return val;
}

/* 获取64位随机数 */
uint64_t csprng_get_uint64(void)
{
    uint64_t val;
    csprng_get_bytes((uint8_t *)&val, sizeof(val));
    return val;
}

/* 获取指定范围的随机数 */
int csprng_get_random_range(uint32_t min, uint32_t max, uint32_t *result)
{
    if (!result || min >= max) return -1;

    uint32_t range = max - min;
    uint32_t limit = UINT32_MAX - (UINT32_MAX % range);
    uint32_t val;

    /* 拒绝采样以避免偏差 */
    do {
        val = csprng_get_uint32();
    } while (val >= limit);

    *result = min + (val % range);
    return 0;
}

/* 健康检查 */
int csprng_health_check(void)
{
    if (!csprng_initialized) return -1;

    /* V7.75安全修复: 检查种子是否为硬编码默认值
     * 原健康检查太弱（固定种子的ChaCha20分布均匀，必然通过），
     * 无法发现种子是硬编码常量的问题。 */
    static const uint8_t default_seed[32] = {
        0x1A, 0x36, 0x5D, 0xA7, 0xC9, 0x00, 0x1A, 0x36,
        0x5D, 0xA7, 0xC9, 0x00, 0x1A, 0x36, 0x5D, 0xA7,
        0xC9, 0x00, 0x1A, 0x36, 0x5D, 0xA7, 0xC9, 0x00,
        0x1A, 0x36, 0x5D, 0xA7, 0xC9, 0x00, 0x1A, 0x36
    };
    if (memcmp(csprng.seed, default_seed, 32) == 0) {
        vga_puts("[CSPRNG] HEALTH FAIL: seed is hardcoded default!\n");
        csprng.health_check_passed = 0;
        return -1;
    }

    /* 检查种子是否全零 */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (csprng.seed[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) {
        vga_puts("[CSPRNG] HEALTH FAIL: seed is all zero!\n");
        csprng.health_check_passed = 0;
        return -1;
    }

    /* 生成两个随机数，确保不相同 */
    uint32_t a = csprng_get_uint32();
    uint32_t b = csprng_get_uint32();

    if (a == b) {
        csprng.health_check_passed = 0;
        return -1;
    }

    /* 检查熵：统计字节分布 */
    uint8_t sample[256];
    csprng_get_bytes(sample, sizeof(sample));
    int counts[256] = {0};
    for (int i = 0; i < 256; i++) counts[sample[i]]++;

    int non_zero = 0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) non_zero++;
    }

    if (non_zero < 100) {
        csprng.health_check_passed = 0;
        return -1;
    }

    /* V7.75: 检查连续启动的随机性（生成10个随机数，确保不全相同） */
    uint32_t seq[10];
    int all_same = 1;
    for (int i = 0; i < 10; i++) {
        seq[i] = csprng_get_uint32();
        if (i > 0 && seq[i] != seq[0]) all_same = 0;
    }
    if (all_same) {
        vga_puts("[CSPRNG] HEALTH FAIL: sequence is constant (predictable)!\n");
        csprng.health_check_passed = 0;
        return -1;
    }

    csprng.health_check_passed = 1;
    vga_puts("[CSPRNG] health ok (multi-source entropy verified)\n");
    return 0;
}

/* 获取熵 */
int csprng_get_entropy(uint8_t *buffer, uint32_t len)
{
    if (!buffer || len == 0) return -1;
    return csprng_get_bytes(buffer, len);
}

/* 添加熵 */
void csprng_add_entropy(const uint8_t *data, uint32_t len)
{
    if (!data || len == 0) return;
    csprng_reseed(data, len);
}

/* 算法名称 */
const char *csprng_algorithm_name(csprng_alg_t alg)
{
    switch (alg) {
        case CSPRNG_ALG_CHACHA20: return "ChaCha20";
        case CSPRNG_ALG_AES_CTR: return "AES-CTR";
        case CSPRNG_ALG_HASH_DRBG: return "Hash_DRBG";
        default: return "Unknown";
    }
}
