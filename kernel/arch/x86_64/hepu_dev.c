/* ============================================================================
 * 赫普OS 64位 - Dev capability 实现
 *
 * P3 初版用 per-process dev_mode 标志。
 * 升级：dev 是 SERVICE_ENDPOINT 类型 capability，KERNEL 域 mint，
 *      其他域 derive 获取。hepu_is_dev() 查句柄。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu_dev.h"
#include "shared/capability/hepu_cap.h"
#include "shared/domain/hepu_domain.h"

/* 全局 dev capability ID，-1=未初始化 */
int g_hepu_dev_cap_id = -1;

static int current_domain_id(void)
{
    extern int process64_get_current_domain_id(void);
    return process64_get_current_domain_id();
}

void hepu_dev_cap_init(void)
{
    /* KERNEL 域 mint dev capability，权利：READ|EXECUTE|DERIVE|REVOKE */
    uint32_t rights = CAP_RIGHT_READ | CAP_RIGHT_EXECUTE |
                      CAP_RIGHT_DERIVE | CAP_RIGHT_REVOKE;
    g_hepu_dev_cap_id = cap_mint(DOMAIN_ID_KERNEL, CAP_TYPE_SERVICE_ENDPOINT,
                                 rights, HEPU_DEV_CAP_RES_ID);
}

int hepu_is_dev(void)
{
    if (g_hepu_dev_cap_id < 0) return 0;

    int dom_id = current_domain_id();
    if (dom_id < 0) return 0;

    /* 遍历该域句柄表，找引用 dev cap 且有 EXECUTE 权利的有效句柄 */
    for (int i = 0; i < CAP_HANDLES_PER_DOMAIN; i++) {
        capability_t *cap = cap_get_valid(dom_id, i);
        if (cap && cap->id == g_hepu_dev_cap_id) {
            if (cap_check(dom_id, i, CAP_RIGHT_EXECUTE)) {
                return 1;
            }
        }
    }
    return 0;
}

void hepu_set_dev_mode(int on)
{
    if (g_hepu_dev_cap_id < 0) return;

    int dom_id = current_domain_id();
    if (dom_id < 0) return;

    if (on) {
        /* derive：给当前域派生 dev 句柄，权利 READ|EXECUTE */
        uint32_t rights = CAP_RIGHT_READ | CAP_RIGHT_EXECUTE;
        cap_derive(g_hepu_dev_cap_id, dom_id, rights);
    } else {
        /* release：遍历找 dev cap 句柄并释放 */
        for (int i = 0; i < CAP_HANDLES_PER_DOMAIN; i++) {
            capability_t *cap = cap_get_valid(dom_id, i);
            if (cap && cap->id == g_hepu_dev_cap_id) {
                cap_release(dom_id, i);
                break;
            }
        }
    }
}
