/* ============================================================================
 * 赫普OS 64位 - 隔离域（Domain）对象实现
 *
 * 域表管理在此文件；与进程相关的统计（mem_usage/refresh_stats）在
 * process64_full.c 中实现，因为只有它能访问 processes[]。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "spinlock.h"
#include "domain64.h"
#include "shared/qos/hepu_qos.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

static domain_t domains[DOMAIN64_MAX_SLOTS];
static spinlock_t domain_lock;
static int domain_initialized = 0;
static int next_domain_id = 0;

static void dom_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static void dom_memset(void *dst, uint8_t val, int n) {
    uint8_t *d = (uint8_t *)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

static int dom_strlen(const char *s) {
    int i = 0;
    while (s && s[i]) i++;
    return i;
}

/* 初始化域表，创建预置域 */
void domain64_init(void)
{
    if (domain_initialized) return;

    for (int i = 0; i < DOMAIN64_MAX_SLOTS; i++) {
        domains[i].active = 0;
        domains[i].id = DOMAIN_ID_INVALID;
    }
    spinlock_init(&domain_lock);
    next_domain_id = 0;

    /* 预置域：这些域在启动时即存在 */
    (void)domain64_create("kernel",   DOMAIN_TYPE_KERNEL,        -1);
    (void)domain64_create("system",   DOMAIN_TYPE_SYSTEM,        0);
    (void)domain64_create("gui",      DOMAIN_TYPE_GUI,           0);
    (void)domain64_create("linux-compat-0", DOMAIN_TYPE_LINUX_COMPAT, 1);

    domain_initialized = 1;
    vga_puts("[DOMAIN] initialized (kernel/system/gui/linux-compat-0)\n");
}

/* 创建域 */
int domain64_create(const char *name, int type, int parent_id)
{
    if (type < 0 || type >= DOMAIN_TYPE_COUNT) return DOMAIN_ID_INVALID;

    spinlock_lock(&domain_lock);

    int idx = -1;
    for (int i = 0; i < DOMAIN64_MAX_SLOTS; i++) {
        if (!domains[i].active) { idx = i; break; }
    }
    if (idx == -1) {
        spinlock_unlock(&domain_lock);
        vga_puts("[DOMAIN] no free domain slot\n");
        return DOMAIN_ID_INVALID;
    }

    int id = next_domain_id++;
    domains[idx].id = id;
    domains[idx].type = type;
    domains[idx].parent_id = parent_id;
    domains[idx].active = 1;
    domains[idx].proc_count = 0;
    domains[idx].mem_pages = 0;
    /* P2-3: 默认 QoS 等级由域类型决定 */
    domains[idx].qos_class = domain_default_qos[type];
    domains[idx].cpu_weight = qos_class_to_weight(domains[idx].qos_class);

    dom_memset(domains[idx].name, 0, DOMAIN_NAME_LEN);
    if (name) {
        int n = dom_strlen(name);
        if (n > DOMAIN_NAME_LEN - 1) n = DOMAIN_NAME_LEN - 1;
        dom_memcpy(domains[idx].name, name, n);
    }

    spinlock_unlock(&domain_lock);
    return id;
}

domain_t *domain64_get(int id)
{
    if (id < 0) return (void *)0;
    for (int i = 0; i < DOMAIN64_MAX_SLOTS; i++) {
        if (domains[i].active && domains[i].id == id) return &domains[i];
    }
    return (void *)0;
}

domain_t *domain64_get_by_type(int type)
{
    if (type < 0 || type >= DOMAIN_TYPE_COUNT) return (void *)0;
    for (int i = 0; i < DOMAIN64_MAX_SLOTS; i++) {
        if (domains[i].active && domains[i].type == type) return &domains[i];
    }
    return (void *)0;
}

int domain64_count(void)
{
    int count = 0;
    for (int i = 0; i < DOMAIN64_MAX_SLOTS; i++) {
        if (domains[i].active) count++;
    }
    return count;
}

int domain64_list(domain_t *out, int max)
{
    int count = 0;
    if (!out || max <= 0) return 0;
    for (int i = 0; i < DOMAIN64_MAX_SLOTS && count < max; i++) {
        if (domains[i].active) {
            out[count++] = domains[i];
        }
    }
    return count;
}

/* env_type → 预置 domain_id 映射 */
int domain64_env_to_id(int env_type)
{
    switch (env_type) {
        case 1: return DOMAIN_ID_LINUX_COMPAT0;  /* linux_compat */
        case 2: return DOMAIN_ID_LINUX_COMPAT0;  /* win_compat(预留)，暂用 linux 兼容域 */
        case 0:
        default: return DOMAIN_ID_SYSTEM;         /* native 归入 system */
    }
}

/* ============================================================================
 * P2-3: 域级 QoS 实现
 * ========================================================================== */

void qos_init(void)
{
    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        if (!domains[i].active) continue;
        domains[i].cpu_weight = qos_class_to_weight(domains[i].qos_class);
    }
    vga_puts("[QoS] initialized\n");
}

int qos_set_class(int domain_id, qos_class_t cls)
{
    if (cls < 0 || cls >= QOS_CLASS_COUNT) return -1;
    domain_t *d = domain64_get(domain_id);
    if (!d) return -1;

    spinlock_lock(&domain_lock);
    d->qos_class = cls;
    d->cpu_weight = qos_class_to_weight(cls);
    spinlock_unlock(&domain_lock);
    return 0;
}

qos_class_t qos_get_class(int domain_id)
{
    domain_t *d = domain64_get(domain_id);
    if (!d) return QOS_CLASS_BACKGROUND;
    return d->qos_class;
}

uint32_t qos_get_weight(int domain_id)
{
    domain_t *d = domain64_get(domain_id);
    if (!d) return 1;
    return d->cpu_weight;
}
