/* ============================================================================
 * 赫普OS - 手机模式 - 共享工具函数
 * 被 perm_mgr、privacy 等多个模块使用
 * ========================================================================== */

#include "mobile.h"
#include "permission_manager.h"
#include "string.h"
#include <stdint.h>

/* 权限名转英文缩写 */
const char *perm_en(int perm)
{
    switch (perm) {
        case PM_PERM_CAMERA: return "Camera";
        case PM_PERM_MICROPHONE: return "Mic";
        case PM_PERM_LOCATION: return "Location";
        case PM_PERM_STORAGE: return "Storage";
        case PM_PERM_CONTACTS: return "Contacts";
        case PM_PERM_SMS: return "SMS";
        case PM_PERM_PHONE: return "Phone";
        case PM_PERM_CALL_LOG: return "CallLog";
        case PM_PERM_CALENDAR: return "Calendar";
        case PM_PERM_SENSORS: return "Sensors";
        case PM_PERM_BLUETOOTH: return "Bluetooth";
        case PM_PERM_NEARBY_WIFI: return "Nearby";
        case PM_PERM_NOTIFICATION: return "Notify";
        case PM_PERM_CLIPBOARD: return "Clipboard";
        case PM_PERM_BODY_SENSORS: return "BodySens";
        case PM_PERM_ACTIVITY_RECOGNITION: return "Motion";
        case PM_PERM_INSTALL_PACKAGES: return "Install";
        case PM_PERM_SYSTEM_ALERT: return "Overlay";
        case PM_PERM_ACCESSIBILITY: return "A11y";
        case PM_PERM_DEVICE_ADMIN: return "DevAdmin";
        default: return "?";
    }
}

/* 授权状态名 */
const char *perm_grant_name(int g)
{
    switch (g) {
        case PM_GRANT_ALLOWED: return "Allow";
        case PM_GRANT_ONETIME: return "Once";
        case PM_GRANT_WHILE_USING: return "While";
        case PM_GRANT_FUZZY: return "Fuzzy";
        case PM_GRANT_EMPTY: return "Empty";
        default: return "Deny";
    }
}

/* 从 package 提取短名: com.hepu.browser -> browser */
const char *pkg_short(const char *pkg)
{
    const char *n = pkg;
    if (strncmp(n, "com.hepu.", 9) == 0) n += 9;
    return n;
}

/* 从应用列表中查找应用名，未找到返回 app<id> */
const char *privacy_app_name(int app_id, prm_app_brief_t *apps, int na, char *buf)
{
    for (int i = 0; i < na; i++)
        if (apps[i].app_id == app_id) return pkg_short(apps[i].package);
    /* 应用已注销(如 selftest): 回退显示 app<id> */
    buf[0] = 'a'; buf[1] = 'p'; buf[2] = 'p';
    buf[3] = (char)('0' + (app_id / 10) % 10);
    buf[4] = (char)('0' + app_id % 10);
    buf[5] = 0;
    return buf;
}
