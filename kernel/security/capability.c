/* ============================================================================
 * 赫普OS 32位 - Capability 实现（细粒度资源授权）
 *
 * 实现 shared/capability/hepu_cap.h 统一接口。
 * 模型与64位 capability64.c 完全一致：
 *   capability_t（资源对象） + handle_t（域对能力的引用，带权利）
 *   generation（代际计数，revoke 时递增，旧句柄自动失效）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "shared/capability/hepu_cap.h"
#include "shared/domain/hepu_domain.h"

/* ---- 数据 ---- */
static capability_t g_caps[CAP_MAX_CAPS];
static handle_table_t g_handles[DOMAIN_MAX_SLOTS];  /* 每域一份句柄表 */
static int g_cap_initialized = 0;

/* ---- 内部：找空能力槽 ---- */
static int find_empty_cap_idx(void)
{
    for (int i = 0; i < CAP_MAX_CAPS; i++) {
        if (!g_caps[i].active && !g_caps[i].revoked) return i;
    }
    return -1;
}

/* ---- 内部：找域的空句柄槽 ---- */
static int find_empty_handle_idx(int domain_id)
{
    if (domain_id < 0 || domain_id >= DOMAIN_MAX_SLOTS) return -1;
    for (int i = 0; i < CAP_HANDLES_PER_DOMAIN; i++) {
        if (!g_handles[domain_id].handles[i].active) return i;
    }
    return -1;
}

void cap_init(void)
{
    if (g_cap_initialized) return;
    for (int i = 0; i < CAP_MAX_CAPS; i++) {
        g_caps[i].active = 0; g_caps[i].revoked = 0;
        g_caps[i].generation = 0;
        g_caps[i].id = i;
    }
    for (int d = 0; d < DOMAIN_MAX_SLOTS; d++) {
        for (int h = 0; h < CAP_HANDLES_PER_DOMAIN; h++) {
            g_handles[d].handles[h].active = 0;
        }
    }
    g_cap_initialized = 1;
}

int cap_mint(int owner_domain_id, int type, uint32_t base_rights, uint64_t resource_id)
{
    if (!g_cap_initialized) return -1;
    if (owner_domain_id < 0 || owner_domain_id >= DOMAIN_MAX_SLOTS) return -1;
    if (type < 0 || type >= CAP_TYPE_COUNT) return -1;

    /* mint 权当前仅 kernel/system 域持有（与64位一致） */
    if (owner_domain_id != DOMAIN_ID_KERNEL && owner_domain_id != DOMAIN_ID_SYSTEM) {
        return -1;
    }

    int idx = find_empty_cap_idx();
    if (idx < 0) return -1;

    g_caps[idx].id = idx;
    g_caps[idx].type = type;
    g_caps[idx].owner_domain_id = owner_domain_id;
    g_caps[idx].base_rights = base_rights;
    g_caps[idx].generation = 0;
    g_caps[idx].revoked = 0;
    g_caps[idx].active = 1;
    g_caps[idx].resource_id = resource_id;

    /* mint 时自动给 owner_domain 一个 CAP_RIGHT_ALL 的句柄 */
    int h_idx = find_empty_handle_idx(owner_domain_id);
    if (h_idx >= 0) {
        g_handles[owner_domain_id].handles[h_idx].capability_id = idx;
        g_handles[owner_domain_id].handles[h_idx].rights_mask = base_rights;
        g_handles[owner_domain_id].handles[h_idx].generation = 0;
        g_handles[owner_domain_id].handles[h_idx].active = 1;
    }

    return idx;
}

int cap_derive(int capability_id, int target_domain_id, uint32_t rights)
{
    if (!g_cap_initialized) return -1;
    if (capability_id < 0 || capability_id >= CAP_MAX_CAPS) return -1;
    if (target_domain_id < 0 || target_domain_id >= DOMAIN_MAX_SLOTS) return -1;
    if (!g_caps[capability_id].active || g_caps[capability_id].revoked) return -1;

    /* 衰减检查：rights 必须是 base_rights 的子集 */
    if ((rights & ~g_caps[capability_id].base_rights) != 0) return -1;

    int h_idx = find_empty_handle_idx(target_domain_id);
    if (h_idx < 0) return -1;

    g_handles[target_domain_id].handles[h_idx].capability_id = capability_id;
    g_handles[target_domain_id].handles[h_idx].rights_mask = rights;
    g_handles[target_domain_id].handles[h_idx].generation = g_caps[capability_id].generation;
    g_handles[target_domain_id].handles[h_idx].active = 1;

    return h_idx;
}

int cap_revoke(int capability_id)
{
    if (!g_cap_initialized) return -1;
    if (capability_id < 0 || capability_id >= CAP_MAX_CAPS) return -1;
    if (!g_caps[capability_id].active) return -1;

    g_caps[capability_id].revoked = 1;
    g_caps[capability_id].generation++;

    /* 级联：所有域的所有句柄里引用此能力的，generation 过期即失效 */
    for (int d = 0; d < DOMAIN_MAX_SLOTS; d++) {
        for (int h = 0; h < CAP_HANDLES_PER_DOMAIN; h++) {
            handle_t *hdl = &g_handles[d].handles[h];
            if (hdl->active && hdl->capability_id == capability_id) {
                hdl->active = 0;  /* 显式失效 */
            }
        }
    }
    return 0;
}

capability_t *cap_get_valid(int domain_id, int handle_idx)
{
    if (!g_cap_initialized) return NULL;
    if (domain_id < 0 || domain_id >= DOMAIN_MAX_SLOTS) return NULL;
    if (handle_idx < 0 || handle_idx >= CAP_HANDLES_PER_DOMAIN) return NULL;

    handle_t *hdl = &g_handles[domain_id].handles[handle_idx];
    if (!hdl->active) return NULL;

    capability_t *cap = &g_caps[hdl->capability_id];
    if (!cap->active || cap->revoked) return NULL;
    if (hdl->generation != cap->generation) return NULL;

    return cap;
}

int cap_check(int domain_id, int handle_idx, uint32_t required_rights)
{
    capability_t *cap = cap_get_valid(domain_id, handle_idx);
    if (!cap) return 0;

    handle_t *hdl = &g_handles[domain_id].handles[handle_idx];
    /* 句柄权利 ⊇ 请求权利，且句柄权利 ⊆ 能力 base_rights */
    if ((hdl->rights_mask & required_rights) != required_rights) return 0;
    if ((hdl->rights_mask & ~cap->base_rights) != 0) return 0;

    return 1;
}

int cap_release(int domain_id, int handle_idx)
{
    if (!g_cap_initialized) return -1;
    if (domain_id < 0 || domain_id >= DOMAIN_MAX_SLOTS) return -1;
    if (handle_idx < 0 || handle_idx >= CAP_HANDLES_PER_DOMAIN) return -1;

    g_handles[domain_id].handles[handle_idx].active = 0;
    return 0;
}

int cap_count(void)
{
    int n = 0;
    if (!g_cap_initialized) return 0;
    for (int i = 0; i < CAP_MAX_CAPS; i++) {
        if (g_caps[i].active && !g_caps[i].revoked) n++;
    }
    return n;
}

int cap_list(capability_t *out, int max)
{
    if (!g_cap_initialized || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < CAP_MAX_CAPS && n < max; i++) {
        if (g_caps[i].active && !g_caps[i].revoked) {
            out[n++] = g_caps[i];
        }
    }
    return n;
}

int cap_list_domain(int domain_id, handle_t *out, int max)
{
    if (!g_cap_initialized || !out || max <= 0) return 0;
    if (domain_id < 0 || domain_id >= DOMAIN_MAX_SLOTS) return 0;
    int n = 0;
    for (int i = 0; i < CAP_HANDLES_PER_DOMAIN && n < max; i++) {
        if (g_handles[domain_id].handles[i].active) {
            out[n++] = g_handles[domain_id].handles[i];
        }
    }
    return n;
}
