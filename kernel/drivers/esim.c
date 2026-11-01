/* ============================================================================
 * 赫普OS - eSIM管理（从telecom2.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "telecom2.h"
#include "vga.h"
#include "string.h"

extern telecom2_t t2;

int telecom2_esim_download_profile(const char *activation_code, const char *confirmation_code)
{
    (void)activation_code; (void)confirmation_code;
    for (int i = 0; i < T2_MAX_ESIM_PROFILES; i++) {
        if (!t2.esim_profiles[i].used) {
            memset(&t2.esim_profiles[i], 0, sizeof(esim_profile_t));
            t2.esim_profiles[i].used = 1;
            t2.esim_profiles[i].id = i;
            strncpy(t2.esim_profiles[i].profile_name, " 新 config ", 63);
            t2.esim_profile_count++;
            vga_puts("[TELECOM2] eSIM profile download done \n");
            return i;
        }
    }
    return -1;
}

int telecom2_esim_enable_profile(int profile_id)
{
    if (profile_id < 0 || profile_id >= T2_MAX_ESIM_PROFILES || !t2.esim_profiles[profile_id].used) return -1;
    for (int i = 0; i < T2_MAX_ESIM_PROFILES; i++) t2.esim_profiles[i].active = 0;
    t2.esim_profiles[profile_id].active = 1;
    t2.esim_profiles[profile_id].enabled = 1;
    return 0;
}

int telecom2_esim_disable_profile(int profile_id)
{
    if (profile_id < 0 || profile_id >= T2_MAX_ESIM_PROFILES || !t2.esim_profiles[profile_id].used) return -1;
    t2.esim_profiles[profile_id].active = 0;
    t2.esim_profiles[profile_id].enabled = 0;
    return 0;
}

int telecom2_esim_delete_profile(int profile_id)
{
    if (profile_id < 0 || profile_id >= T2_MAX_ESIM_PROFILES || !t2.esim_profiles[profile_id].used) return -1;
    t2.esim_profiles[profile_id].used = 0;
    t2.esim_profile_count--;
    return 0;
}

int telecom2_esim_list_profiles(esim_profile_t *profiles, int max)
{
    if (!profiles || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < T2_MAX_ESIM_PROFILES && count < max; i++) {
        if (t2.esim_profiles[i].used) memcpy(&profiles[count++], &t2.esim_profiles[i], sizeof(esim_profile_t));
    }
    return count;
}

int telecom2_esim_get_active_profile(esim_profile_t *profile)
{
    if (!profile) return -1;
    for (int i = 0; i < T2_MAX_ESIM_PROFILES; i++) {
        if (t2.esim_profiles[i].used && t2.esim_profiles[i].active) {
            memcpy(profile, &t2.esim_profiles[i], sizeof(esim_profile_t));
            return i;
        }
    }
    return -1;
}
