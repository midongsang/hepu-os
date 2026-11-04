/* ============================================================================
 * 赫普OS 32位 - 隔离域（Domain）实现
 *
 * 实现 shared/domain/hepu_domain.h 定义的统一接口。
 * 与64位 domain64.c 功能对等，上层代码可无缝切换。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "smp.h"           /* spinlock_t / spin_lock / spin_unlock */
#include "process.h"        /* process_t / proc_table / MAX_PROCESSES / PROC_UNUSED */
#include "shared/domain/hepu_domain.h"
#include "shared/qos/hepu_qos.h"

extern void vga_puts(const char *s);

static domain_t domains[DOMAIN_MAX_SLOTS];
static spinlock_t domain_lock = SPINLOCK_INIT;
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
void domain_init(void)
{
    if (domain_initialized) return;

    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        domains[i].active = 0;
        domains[i].id = DOMAIN_ID_INVALID;
    }
    next_domain_id = 0;

    /* 预置域：这些域在启动时即存在 */
    (void)domain_create("kernel",   DOMAIN_TYPE_KERNEL,        -1);
    (void)domain_create("system",   DOMAIN_TYPE_SYSTEM,        0);
    (void)domain_create("gui",      DOMAIN_TYPE_GUI,           0);
    (void)domain_create("linux-compat-0", DOMAIN_TYPE_LINUX_COMPAT, 1);

    domain_initialized = 1;
    vga_puts("[DOMAIN] initialized (kernel/system/gui/linux-compat-0)\n");
}

/* 创建域 */
int domain_create(const char *name, int type, int parent_id)
{
    if (type < 0 || type >= DOMAIN_TYPE_COUNT) return DOMAIN_ID_INVALID;

    spin_lock(&domain_lock);

    int idx = -1;
    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        if (!domains[i].active) { idx = i; break; }
    }
    if (idx == -1) {
        spin_unlock(&domain_lock);
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

    spin_unlock(&domain_lock);
    return id;
}

domain_t *domain_get(int id)
{
    if (id < 0) return (void *)0;
    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        if (domains[i].active && domains[i].id == id) return &domains[i];
    }
    return (void *)0;
}

domain_t *domain_get_by_type(int type)
{
    if (type < 0 || type >= DOMAIN_TYPE_COUNT) return (void *)0;
    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        if (domains[i].active && domains[i].type == type) return &domains[i];
    }
    return (void *)0;
}

int domain_count(void)
{
    int count = 0;
    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        if (domains[i].active) count++;
    }
    return count;
}

int domain_list(domain_t *out, int max)
{
    int count = 0;
    if (!out || max <= 0) return 0;
    for (int i = 0; i < DOMAIN_MAX_SLOTS && count < max; i++) {
        if (domains[i].active) {
            out[count++] = domains[i];
        }
    }
    return count;
}

/* env_type → 预置 domain_id 映射 */
int domain_env_to_id(int env_type)
{
    switch (env_type) {
        case 1: return DOMAIN_ID_LINUX_COMPAT0;  /* linux_compat */
        case 2: return DOMAIN_ID_LINUX_COMPAT0;  /* win_compat(预留)，暂用 linux 兼容域 */
        case 0:
        default: return DOMAIN_ID_SYSTEM;         /* native 归入 system */
    }
}

/* 按域统计内存占用（页）
 * P2-2: 32位实现——遍历进程表累加同域进程的 mem_used */
uint64_t domain_mem_usage(int domain_id)
{
    extern process_t proc_table[];
    if (domain_id < 0) return 0;

    uint64_t pages = 0;
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) continue;
        if (proc_table[i].domain_id == domain_id) {
            pages += proc_table[i].mem_used;
        }
    }
    return pages;
}

/* 刷新域的统计（proc_count / mem_pages） */
void domain_refresh_stats(void)
{
    extern process_t proc_table[];

    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        if (!domains[i].active) continue;
        domains[i].proc_count = 0;
        domains[i].mem_pages = 0;
    }

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) continue;
        int did = proc_table[i].domain_id;
        if (did < 0) continue;
        for (int j = 0; j < DOMAIN_MAX_SLOTS; j++) {
            if (domains[j].active && domains[j].id == did) {
                domains[j].proc_count++;
                domains[j].mem_pages += proc_table[i].mem_used;
                break;
            }
        }
    }
}

/* ============================================================================
 * P2-3: 域级 QoS 实现
 * ========================================================================== */

void qos_init(void)
{
    /* 预置域在 domain_create 时已设置默认 qos_class/cpu_weight，
     * 此处仅刷新所有活动域的权重（确保与映射表一致） */
    for (int i = 0; i < DOMAIN_MAX_SLOTS; i++) {
        if (!domains[i].active) continue;
        domains[i].cpu_weight = qos_class_to_weight(domains[i].qos_class);
    }
    vga_puts("[QoS] initialized\n");
}

int qos_set_class(int domain_id, qos_class_t cls)
{
    if (cls < 0 || cls >= QOS_CLASS_COUNT) return -1;
    domain_t *d = domain_get(domain_id);
    if (!d) return -1;

    spin_lock(&domain_lock);
    d->qos_class = cls;
    d->cpu_weight = qos_class_to_weight(cls);
    spin_unlock(&domain_lock);
    return 0;
}

qos_class_t qos_get_class(int domain_id)
{
    domain_t *d = domain_get(domain_id);
    if (!d) return QOS_CLASS_BACKGROUND;
    return d->qos_class;
}

uint32_t qos_get_weight(int domain_id)
{
    domain_t *d = domain_get(domain_id);
    if (!d) return 1;
    return d->cpu_weight;
}
