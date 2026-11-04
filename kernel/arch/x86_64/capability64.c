/* ============================================================================
 * 赫普OS 64位 - 能力（Capability）系统实现
 *
 * 全局能力表 + 每域句柄表。
 * mint 权当前仅 kernel/system 域持有（后续由权限 broker 接管）。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "capability64.h"
#include "domain64.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

static capability_t caps[CAP64_MAX_CAPS];
static handle_table_t handle_tables[DOMAIN64_MAX_SLOTS];  /* 每域句柄表 */
static int caps_initialized = 0;

/* ---------- 内部工具 ---------- */

static int cap64_find_free_slot(void)
{
    for (int i = 0; i < CAP64_MAX_CAPS; i++) {
        if (!caps[i].active) return i;
    }
    return -1;
}

static int handle_find_free(int domain_id)
{
    if (domain_id < 0 || domain_id >= DOMAIN64_MAX_SLOTS) return -1;
    for (int i = 0; i < CAP64_HANDLES_PER_DOMAIN; i++) {
        if (!handle_tables[domain_id].handles[i].active) return i;
    }
    return -1;
}

/* 判断请求权利是否为父权利的子集（衰减派生约束） */
static int rights_is_subset(uint32_t requested, uint32_t parent)
{
    return (requested & ~parent) == 0;
}

/* ---------- 公共 API ---------- */

void cap64_init(void)
{
    for (int i = 0; i < CAP64_MAX_CAPS; i++) {
        caps[i].active = 0;
        caps[i].revoked = 0;
        caps[i].generation = 0;
    }
    for (int d = 0; d < DOMAIN64_MAX_SLOTS; d++) {
        for (int h = 0; h < CAP64_HANDLES_PER_DOMAIN; h++) {
            handle_tables[d].handles[h].active = 0;
        }
    }
    caps_initialized = 1;
    vga_puts("[CAP64] Capability subsystem initialized\n");
}

int cap64_mint(int owner_domain_id, int type, uint32_t base_rights, uint64_t resource_id)
{
    if (!caps_initialized) return -1;
    if (type < 0 || type >= CAP_TYPE_COUNT) return -1;

    /* mint 权：当前仅 kernel / system 域可创建能力 */
    domain_t *d = domain64_get(owner_domain_id);
    if (!d) return -1;
    if (d->type != DOMAIN_TYPE_KERNEL && d->type != DOMAIN_TYPE_SYSTEM) {
        vga_puts("[CAP64] mint denied: domain ");
        vga_put_hex(owner_domain_id);
        vga_puts(" lacks mint authority\n");
        return -1;
    }

    int idx = cap64_find_free_slot();
    if (idx < 0) return -1;

    caps[idx].id = idx;
    caps[idx].type = type;
    caps[idx].owner_domain_id = owner_domain_id;
    caps[idx].base_rights = base_rights;
    caps[idx].generation = 1;
    caps[idx].revoked = 0;
    caps[idx].active = 1;
    caps[idx].resource_id = resource_id;

    return idx;
}

int cap64_derive(int capability_id, int target_domain_id, uint32_t rights)
{
    if (!caps_initialized) return -1;
    if (capability_id < 0 || capability_id >= CAP64_MAX_CAPS) return -1;
    if (!caps[capability_id].active || caps[capability_id].revoked) return -1;
    if (!domain64_get(target_domain_id)) return -1;

    /* 衰减：请求权利必须是能力 base_rights 的子集 */
    if (!rights_is_subset(rights, caps[capability_id].base_rights)) {
        vga_puts("[CAP64] derive denied: rights not subset of base_rights\n");
        return -1;
    }

    int h = handle_find_free(target_domain_id);
    if (h < 0) return -1;

    handle_tables[target_domain_id].handles[h].capability_id = capability_id;
    handle_tables[target_domain_id].handles[h].rights_mask = rights;
    handle_tables[target_domain_id].handles[h].generation = caps[capability_id].generation;
    handle_tables[target_domain_id].handles[h].active = 1;

    return h;
}

int cap64_revoke(int capability_id)
{
    if (capability_id < 0 || capability_id >= CAP64_MAX_CAPS) return -1;
    if (!caps[capability_id].active) return -1;

    caps[capability_id].revoked = 1;
    caps[capability_id].generation++;  /* 级联：所有旧句柄代际失效 */

    /* 主动清空所有域中指向该能力的句柄 */
    for (int d = 0; d < DOMAIN64_MAX_SLOTS; d++) {
        for (int h = 0; h < CAP64_HANDLES_PER_DOMAIN; h++) {
            if (handle_tables[d].handles[h].active &&
                handle_tables[d].handles[h].capability_id == capability_id) {
                handle_tables[d].handles[h].active = 0;
            }
        }
    }

    return 0;
}

int cap64_check(int domain_id, int handle_idx, uint32_t required_rights)
{
    capability_t *cap = cap64_get_valid(domain_id, handle_idx);
    if (!cap) return 0;
    handle_t *h = &handle_tables[domain_id].handles[handle_idx];
    return (h->rights_mask & required_rights) == required_rights;
}

capability_t *cap64_get_valid(int domain_id, int handle_idx)
{
    if (domain_id < 0 || domain_id >= DOMAIN64_MAX_SLOTS) return NULL;
    if (handle_idx < 0 || handle_idx >= CAP64_HANDLES_PER_DOMAIN) return NULL;

    handle_t *h = &handle_tables[domain_id].handles[handle_idx];
    if (!h->active) return NULL;

    capability_t *cap = &caps[h->capability_id];
    if (!cap->active || cap->revoked) return NULL;
    if (cap->generation != h->generation) return NULL;  /* 代际过期 */

    return cap;
}

int cap64_release(int domain_id, int handle_idx)
{
    if (domain_id < 0 || domain_id >= DOMAIN64_MAX_SLOTS) return -1;
    if (handle_idx < 0 || handle_idx >= CAP64_HANDLES_PER_DOMAIN) return -1;
    if (!handle_tables[domain_id].handles[handle_idx].active) return -1;

    handle_tables[domain_id].handles[handle_idx].active = 0;
    return 0;
}

int cap64_count(void)
{
    int count = 0;
    for (int i = 0; i < CAP64_MAX_CAPS; i++) {
        if (caps[i].active && !caps[i].revoked) count++;
    }
    return count;
}

int cap64_list(capability_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < CAP64_MAX_CAPS && n < max; i++) {
        if (caps[i].active && !caps[i].revoked) {
            out[n++] = caps[i];
        }
    }
    return n;
}

int cap64_list_domain(int domain_id, handle_t *out, int max)
{
    if (domain_id < 0 || domain_id >= DOMAIN64_MAX_SLOTS) return 0;
    int n = 0;
    for (int h = 0; h < CAP64_HANDLES_PER_DOMAIN && n < max; h++) {
        handle_t *hd = &handle_tables[domain_id].handles[h];
        if (hd->active) {
            out[n++] = *hd;
        }
    }
    return n;
}
