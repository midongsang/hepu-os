/* ============================================================================
 * 赫普OS - H-DPK社区模板机制
 * ========================================================================== */

#include <stdint.h>
#include "hdpk_template.h"
#include "vga.h"
#include "string.h"

static hdpk_template_t ht;

static const char *platform_names[] = {
    " unknown ", " high 通骁龙 ", " 联发科天玑 ", " 联发科 Helio", " 展锐虎贲 ",
    " 海思麒麟 ", " 三星 Exynos", " 谷歌 Tensor", " 苹果 A 系列 ", "x86",
    "RISC-V", " 通用 ARM"
};

static const char *component_names[] = {
    "CPU", "GPU", "Modem", "WiFi", " BT ", " camera ", " audio ", " sensors ",
    " 触控 ", " display ", " battery ", "USB", "NFC", " fingerprint ", "GPS", " 其他 "
};

static const char *status_names[] = {
    " not supports ", " 开发 in ", " 部分 supports ", " 可用 ", " 完美 supports "
};

void hdpk_template_init(void)
{
    memset(&ht, 0, sizeof(hdpk_template_t));
    ht.initialized = 1;
    ht.enabled = 1;
    ht.community_active = 1;

    /* 注册官方平台模板 */
    struct {
        ht_platform_t platform;
        const char *name;
        const char *chip;
    } official_templates[] = {
        {HT_PLATFORM_QUALCOMM_SNAPDRAGON, " high 通骁龙通用 templates ", "Snapdragon"},
        {HT_PLATFORM_MEDIATEK_DIMENSITY, " 联发科天玑通用 templates ", "Dimensity"},
        {HT_PLATFORM_MEDIATEK_HELIO, " 联发科 Helio 通用 templates ", "Helio"},
        {HT_PLATFORM_UNISOC_TIGER, " 展锐虎贲通用 templates ", "Tiger"},
        {HT_PLATFORM_GENERIC_ARM, " 通用 ARM templates ", "Generic-ARM"},
    };

    for (int i = 0; i < 5; i++) {
        int tid = hdpk_template_register(official_templates[i].platform,
                                         official_templates[i].name,
                                         official_templates[i].chip);
        if (tid >= 0) {
            /* 为模板添加默认组件 */
            ht_component_t comps[] = {HT_COMP_CPU, HT_COMP_GPU, HT_COMP_MODEM,
                HT_COMP_WIFI, HT_COMP_BLUETOOTH, HT_COMP_CAMERA, HT_COMP_AUDIO,
                HT_COMP_SENSOR, HT_COMP_TOUCH, HT_COMP_DISPLAY, HT_COMP_BATTERY,
                HT_COMP_USB};
            for (int j = 0; j < 12; j++) {
                hdpk_template_update_component(tid, comps[j],
                    component_names[comps[j]], HT_STATUS_WIP, " 社 region pending 填充 ");
            }
            ht.templates[tid].maintainer[0] = 'H'; ht.templates[tid].maintainer[1] = 'E';
            ht.templates[tid].maintainer[2] = 'P'; ht.templates[tid].maintainer[3] = 'U';
            ht.templates[tid].maintainer[4] = 0;
            ht.templates[tid].verified = 1;
            ht.templates[tid].version = 1;
        }
    }

    /* 注册示例机型 */
    hdpk_device_register(" Hepu ", "HDK-1", "hepu_hdk1", 0);
    hdpk_device_set_support_level(0, 1);  /* 完整支持 */

    vga_puts("[HDPK] H-DPK community template init done \n");
    vga_printf(" platform templates: %d, sample devices: %d, community mode: %s\n",
               ht.template_count, ht.device_count,
               ht.community_active ? " on " : " close ");
}

int hdpk_template_register(ht_platform_t platform, const char *name, const char *chip)
{
    for (int i = 0; i < HT_MAX_TEMPLATES; i++) {
        if (!ht.templates[i].used) {
            memset(&ht.templates[i], 0, sizeof(ht_template_t));
            ht.templates[i].used = 1;
            ht.templates[i].id = i;
            ht.templates[i].platform = platform;
            if (name) strncpy(ht.templates[i].platform_name, name, HT_NAME_LEN - 1);
            if (chip) strncpy(ht.templates[i].chip_model, chip, 63);
            ht.templates[i].kernel_base_addr = 0x80000000;
            ht.templates[i].ram_start = 0x80000000;
            ht.templates[i].ram_size = 0x40000000;  /* 1GB默认 */
            ht.template_count++;
            return i;
        }
    }
    return -1;
}

int hdpk_template_get(int id, ht_template_t *tmpl)
{
    if (id < 0 || id >= HT_MAX_TEMPLATES || !ht.templates[id].used || !tmpl) return -1;
    memcpy(tmpl, &ht.templates[id], sizeof(ht_template_t));
    return 0;
}

int hdpk_template_list(ht_template_t *tmpls, int max)
{
    if (!tmpls || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HT_MAX_TEMPLATES && count < max; i++) {
        if (ht.templates[i].used) memcpy(&tmpls[count++], &ht.templates[i], sizeof(ht_template_t));
    }
    return count;
}

int hdpk_template_list_by_platform(ht_platform_t platform, ht_template_t *tmpls, int max)
{
    if (!tmpls || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HT_MAX_TEMPLATES && count < max; i++) {
        if (ht.templates[i].used && ht.templates[i].platform == platform)
            memcpy(&tmpls[count++], &ht.templates[i], sizeof(ht_template_t));
    }
    return count;
}

int hdpk_template_update_component(int template_id, ht_component_t type,
                                   const char *name, ht_status_t status, const char *notes)
{
    if (template_id < 0 || template_id >= HT_MAX_TEMPLATES || !ht.templates[template_id].used)
        return -1;
    for (int i = 0; i < HT_MAX_COMPONENTS; i++) {
        if (ht.templates[template_id].components[i].used &&
            ht.templates[template_id].components[i].type == type) {
            if (name) strncpy(ht.templates[template_id].components[i].name, name, HT_NAME_LEN - 1);
            ht.templates[template_id].components[i].status = status;
            if (notes) strncpy(ht.templates[template_id].components[i].notes, notes, 255);
            return 0;
        }
    }
    /* 新增组件 */
    for (int i = 0; i < HT_MAX_COMPONENTS; i++) {
        if (!ht.templates[template_id].components[i].used) {
            memset(&ht.templates[template_id].components[i], 0, sizeof(ht_component_config_t));
            ht.templates[template_id].components[i].used = 1;
            ht.templates[template_id].components[i].type = type;
            if (name) strncpy(ht.templates[template_id].components[i].name, name, HT_NAME_LEN - 1);
            ht.templates[template_id].components[i].status = status;
            if (notes) strncpy(ht.templates[template_id].components[i].notes, notes, 255);
            ht.templates[template_id].component_count++;
            return 0;
        }
    }
    return -1;
}

int hdpk_template_verify(int template_id)
{
    if (template_id < 0 || template_id >= HT_MAX_TEMPLATES || !ht.templates[template_id].used)
        return -1;
    ht.templates[template_id].verified = 1;
    return 0;
}

int hdpk_device_register(const char *brand, const char *model, const char *codename,
                         int template_id)
{
    for (int i = 0; i < HT_MAX_DEVICES; i++) {
        if (!ht.devices[i].used) {
            memset(&ht.devices[i], 0, sizeof(ht_device_t));
            ht.devices[i].used = 1;
            ht.devices[i].id = i;
            if (brand) strncpy(ht.devices[i].brand, brand, 63);
            if (model) strncpy(ht.devices[i].model, model, 63);
            if (codename) strncpy(ht.devices[i].codename, codename, 63);
            ht.devices[i].template_id = template_id;
            if (template_id >= 0 && template_id < HT_MAX_TEMPLATES && ht.templates[template_id].used)
                ht.devices[i].platform = ht.templates[template_id].platform;
            ht.devices[i].ram_size_mb = 2048;
            ht.devices[i].storage_gb = 32;
            ht.devices[i].support_level = 3;  /* 默认社区支持 */
            ht.devices[i].create_time = 1;
            ht.device_count++;
            return i;
        }
    }
    return -1;
}

int hdpk_device_get(int id, ht_device_t *device)
{
    if (id < 0 || id >= HT_MAX_DEVICES || !ht.devices[id].used || !device) return -1;
    memcpy(device, &ht.devices[id], sizeof(ht_device_t));
    return 0;
}

int hdpk_device_list(ht_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HT_MAX_DEVICES && count < max; i++) {
        if (ht.devices[i].used) memcpy(&devices[count++], &ht.devices[i], sizeof(ht_device_t));
    }
    return count;
}

int hdpk_device_list_by_template(int template_id, ht_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HT_MAX_DEVICES && count < max; i++) {
        if (ht.devices[i].used && ht.devices[i].template_id == template_id)
            memcpy(&devices[count++], &ht.devices[i], sizeof(ht_device_t));
    }
    return count;
}

int hdpk_device_list_by_support(int level, ht_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HT_MAX_DEVICES && count < max; i++) {
        if (ht.devices[i].used && ht.devices[i].support_level == level)
            memcpy(&devices[count++], &ht.devices[i], sizeof(ht_device_t));
    }
    return count;
}

int hdpk_device_search(const char *keyword, ht_device_t *devices, int max)
{
    if (!keyword || !devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HT_MAX_DEVICES && count < max; i++) {
        if (ht.devices[i].used &&
            (strstr(ht.devices[i].brand, keyword) ||
             strstr(ht.devices[i].model, keyword) ||
             strstr(ht.devices[i].codename, keyword))) {
            memcpy(&devices[count++], &ht.devices[i], sizeof(ht_device_t));
        }
    }
    return count;
}

int hdpk_device_update_component(int device_id, ht_component_t type,
                                 const char *name, ht_status_t status, const char *notes)
{
    if (device_id < 0 || device_id >= HT_MAX_DEVICES || !ht.devices[device_id].used) return -1;
    for (int i = 0; i < HT_MAX_COMPONENTS; i++) {
        if (ht.devices[device_id].components[i].used &&
            ht.devices[device_id].components[i].type == type) {
            if (name) strncpy(ht.devices[device_id].components[i].name, name, HT_NAME_LEN - 1);
            ht.devices[device_id].components[i].status = status;
            if (notes) strncpy(ht.devices[device_id].components[i].notes, notes, 255);
            return 0;
        }
    }
    for (int i = 0; i < HT_MAX_COMPONENTS; i++) {
        if (!ht.devices[device_id].components[i].used) {
            memset(&ht.devices[device_id].components[i], 0, sizeof(ht_component_config_t));
            ht.devices[device_id].components[i].used = 1;
            ht.devices[device_id].components[i].type = type;
            if (name) strncpy(ht.devices[device_id].components[i].name, name, HT_NAME_LEN - 1);
            ht.devices[device_id].components[i].status = status;
            if (notes) strncpy(ht.devices[device_id].components[i].notes, notes, 255);
            ht.devices[device_id].component_count++;
            return 0;
        }
    }
    return -1;
}

int hdpk_device_set_support_level(int device_id, int level)
{
    if (device_id < 0 || device_id >= HT_MAX_DEVICES || !ht.devices[device_id].used) return -1;
    ht.devices[device_id].support_level = level;
    return 0;
}

int hdpk_device_rate(int device_id, int rating)
{
    if (device_id < 0 || device_id >= HT_MAX_DEVICES || !ht.devices[device_id].used) return -1;
    if (rating < 1) rating = 1;
    if (rating > 5) rating = 5;
    ht.devices[device_id].rating = rating;
    return 0;
}

int hdpk_device_verify(int device_id)
{
    if (device_id < 0 || device_id >= HT_MAX_DEVICES || !ht.devices[device_id].used) return -1;
    ht.devices[device_id].verified = 1;
    return 0;
}

int hdpk_contribute_template(const ht_template_t *tmpl)
{
    if (!tmpl) return -1;
    int id = hdpk_template_register(tmpl->platform, tmpl->platform_name, tmpl->chip_model);
    if (id >= 0) {
        ht.total_contributions++;
        if (tmpl->maintainer[0])
            strncpy(ht.templates[id].maintainer, tmpl->maintainer, 63);
    }
    return id;
}

int hdpk_contribute_device(const ht_device_t *device)
{
    if (!device) return -1;
    int id = hdpk_device_register(device->brand, device->model, device->codename,
                                  device->template_id);
    if (id >= 0) {
        ht.total_contributions++;
        if (device->contributor[0])
            strncpy(ht.devices[id].contributor, device->contributor, 63);
    }
    return id;
}

int hdpk_community_stats(int *nt, int *nd, uint32_t *contribs)
{
    if (nt) *nt = ht.template_count;
    if (nd) *nd = ht.device_count;
    if (contribs) *contribs = (uint32_t)ht.total_contributions;
    return 0;
}

int hdpk_template_summary(int id, char *name, int nsz, char *chip, int csz,
                          int *comps, int *ver)
{
    if (id < 0 || id >= HT_MAX_TEMPLATES || !ht.templates[id].used) return -1;
    if (name && nsz > 0) { strncpy(name, ht.templates[id].platform_name, nsz - 1); name[nsz - 1] = 0; }
    if (chip && csz > 0) { strncpy(chip, ht.templates[id].chip_model, csz - 1); chip[csz - 1] = 0; }
    if (comps) *comps = ht.templates[id].component_count;
    if (ver) *ver = ht.templates[id].verified;
    return 0;
}

int hdpk_device_summary(int id, char *brand, int bsz, char *model, int msz,
                        int *lvl, uint32_t *ram_mb)
{
    if (id < 0 || id >= HT_MAX_DEVICES || !ht.devices[id].used) return -1;
    if (brand && bsz > 0) { strncpy(brand, ht.devices[id].brand, bsz - 1); brand[bsz - 1] = 0; }
    if (model && msz > 0) { strncpy(model, ht.devices[id].model, msz - 1); model[msz - 1] = 0; }
    if (lvl) *lvl = ht.devices[id].support_level;
    if (ram_mb) *ram_mb = ht.devices[id].ram_size_mb;
    return 0;
}

int hdpk_get_community_stats(hdpk_template_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ht, sizeof(hdpk_template_t));
    return 0;
}

int hdpk_platform_name(ht_platform_t platform, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int idx = (int)platform;
    if (idx < 0 || idx >= 12) idx = 0;
    strncpy(buf, platform_names[idx], size - 1);
    buf[size - 1] = 0;
    return 0;
}

int hdpk_component_name(ht_component_t comp, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int idx = (int)comp;
    if (idx < 0 || idx >= 16) idx = 15;
    strncpy(buf, component_names[idx], size - 1);
    buf[size - 1] = 0;
    return 0;
}

int hdpk_status_name(ht_status_t status, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int idx = (int)status;
    if (idx < 0 || idx >= 5) idx = 0;
    strncpy(buf, status_names[idx], size - 1);
    buf[size - 1] = 0;
    return 0;
}
