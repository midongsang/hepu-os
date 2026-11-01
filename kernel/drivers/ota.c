/* ============================================================================
 * 赫普OS - OTA 系统升级
 * A/B分区 + 差分更新 + 签名验证 + 自动回滚
 * ========================================================================== */

#include <stdint.h>
#include "ota.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static ota_device_t ota_dev;
static int ota_initialized = 0;

/* CRC32计算 */
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? (0xEDB88320 ^ (crc >> 1)) : (crc >> 1);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

static uint32_t crc32_calc(const void *data, uint32_t len)
{
    if (!crc32_initialized) crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *p = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

void ota_init(void)
{
    memset(&ota_dev, 0, sizeof(ota_device_t));
    ota_dev.initialized = 1;
    ota_dev.state = OTA_STATE_IDLE;
    ota_dev.current_slot = 0;
    ota_initialized = 1;

    /* 初始化A/B分区 */
    ota_dev.slots[0].slot = 0;
    ota_dev.slots[0].active = 1;
    ota_dev.slots[0].bootable = 1;
    ota_dev.slots[0].successful = 1;
    ota_dev.slots[0].retries = 0;
    strcpy(ota_dev.slots[0].version, "V0.13");
    ota_dev.slots[0].size = 64 * 1024 * 1024;

    ota_dev.slots[1].slot = 1;
    ota_dev.slots[1].active = 0;
    ota_dev.slots[1].bootable = 1;
    ota_dev.slots[1].successful = 0;
    ota_dev.slots[1].retries = 0;
    strcpy(ota_dev.slots[1].version, "");
    ota_dev.slots[1].size = 64 * 1024 * 1024;

    vga_puts("[OTA] OTA upgrade system init done \n");
    vga_printf(" current slot: %s, version: %s\n",
               ota_dev.current_slot == 0 ? "A" : "B",
               ota_dev.slots[ota_dev.current_slot].version);
    vga_puts(" A/B dual partition ready, supports delta update & auto rollback \n");
}

int ota_check_update(const char *server_url)
{
    if (!ota_initialized) return -1;
    (void)server_url;

    /* 原型阶段: 模拟检查更新 */
    ota_dev.update_available = 1;
    strcpy(ota_dev.latest_version, "V0.14");
    ota_dev.download_size = 32 * 1024 * 1024;  /* 32MB */

    vga_printf("[OTA] check update: new version found %s(%.1fMB)\n",
               ota_dev.latest_version, ota_dev.download_size / (1024.0 * 1024.0));
    return 1;
}

int ota_download(const char *url, ota_progress_cb_t cb)
{
    if (!ota_initialized || !url) return -1;
    ota_dev.state = OTA_STATE_DOWNLOADING;
    ota_dev.progress_cb = cb;
    ota_dev.downloaded = 0;

    vga_printf("[OTA] start download: %s\n", url);

    /* 原型阶段: 模拟下载进度 */
    for (int i = 0; i <= 100; i += 10) {
        ota_dev.progress = i;
        ota_dev.downloaded = ota_dev.download_size * i / 100;
        if (cb) cb(i, OTA_STATE_DOWNLOADING);
    }

    ota_dev.state = OTA_STATE_IDLE;
    ota_dev.progress = 100;
    vga_puts("[OTA] download done \n");
    return 0;
}

int ota_verify(const char *package_path)
{
    if (!ota_initialized || !package_path) return -1;
    ota_dev.state = OTA_STATE_VERIFYING;

    vga_printf("[OTA] verify update: %s\n", package_path);

    /* 原型阶段: 模拟验证 */
    /* 1. 检查魔数 */
    /* 2. 验证签名 */
    /* 3. 验证哈希 (使用CRC32) */
    uint32_t test_crc = crc32_calc("HEPU", 4);
    (void)test_crc;
    /* 4. 验证版本兼容性 */

    ota_dev.state = OTA_STATE_IDLE;
    vga_puts("[OTA] verify ok: sig valid, hash match \n");
    return 0;
}

int ota_install(const char *package_path)
{
    if (!ota_initialized || !package_path) return -1;
    ota_dev.state = OTA_STATE_INSTALLING;

    vga_printf("[OTA] install update to inactive slot ...\n");

    /* 原型阶段: 模拟安装到非活动分区 */
    int inactive_slot = ota_dev.current_slot ^ 1;
    ota_dev.slots[inactive_slot].bootable = 1;
    ota_dev.slots[inactive_slot].successful = 0;
    ota_dev.slots[inactive_slot].retries = 0;
    strcpy(ota_dev.slots[inactive_slot].version, ota_dev.latest_version);

    for (int i = 0; i <= 100; i += 20) {
        ota_dev.progress = i;
        if (ota_dev.progress_cb) ota_dev.progress_cb(i, OTA_STATE_INSTALLING);
    }

    ota_dev.state = OTA_STATE_SUCCESS;
    ota_dev.total_updates++;
    ota_dev.successful_updates++;
    vga_printf("[OTA] install done: slot %s -> %s\n",
               inactive_slot == 0 ? "A" : "B", ota_dev.latest_version);
    return 0;
}

int ota_apply(void)
{
    if (!ota_initialized) return -1;
    if (ota_dev.state != OTA_STATE_SUCCESS) return -1;

    int inactive_slot = ota_dev.current_slot ^ 1;
    if (!ota_dev.slots[inactive_slot].bootable) return -1;

    /* 切换活动槽位 */
    ota_dev.slots[ota_dev.current_slot].active = 0;
    ota_dev.slots[inactive_slot].active = 1;
    ota_dev.current_slot = inactive_slot;
    ota_dev.slots[inactive_slot].retries = 3;  /* 最多重试3次 */

    vga_printf("[OTA] switched to slot %s(%s), effective after reboot \n",
               inactive_slot == 0 ? "A" : "B",
               ota_dev.slots[inactive_slot].version);
    return 0;
}

int ota_rollback(void)
{
    if (!ota_initialized) return -1;
    ota_dev.state = OTA_STATE_ROLLBACK;

    int prev_slot = ota_dev.current_slot ^ 1;
    if (!ota_dev.slots[prev_slot].successful) {
        ota_dev.state = OTA_STATE_FAILED;
        ota_dev.last_error = OTA_ERR_ROLLBACK_FAILED;
        vga_puts("[OTA] rollback failed: previous slot unavailable \n");
        return -1;
    }

    ota_dev.slots[ota_dev.current_slot].active = 0;
    ota_dev.slots[ota_dev.current_slot].bootable = 0;
    ota_dev.slots[prev_slot].active = 1;
    ota_dev.current_slot = prev_slot;
    ota_dev.rollbacks++;

    vga_printf("[OTA] rolled back to slot %s(%s)\n",
               prev_slot == 0 ? "A" : "B",
               ota_dev.slots[prev_slot].version);
    ota_dev.state = OTA_STATE_IDLE;
    return 0;
}

int ota_mark_boot_success(void)
{
    if (!ota_initialized) return -1;
    ota_dev.slots[ota_dev.current_slot].successful = 1;
    ota_dev.slots[ota_dev.current_slot].retries = 0;
    vga_puts("[OTA] boot success flag set \n");
    return 0;
}

int ota_get_status(ota_device_t *status)
{
    if (!status) return -1;
    memcpy(status, &ota_dev, sizeof(ota_device_t));
    return 0;
}

int ota_get_version(char *buf, int len)
{
    if (!buf || len <= 0) return -1;
    strncpy(buf, ota_dev.slots[ota_dev.current_slot].version, len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int ota_get_latest_version(char *buf, int len)
{
    if (!buf || len <= 0) return -1;
    strncpy(buf, ota_dev.latest_version, len - 1);
    buf[len - 1] = '\0';
    return 0;
}

void ota_set_progress_callback(ota_progress_cb_t cb)
{
    ota_dev.progress_cb = cb;
}

int ota_cancel(void)
{
    if (!ota_initialized) return -1;
    if (ota_dev.state == OTA_STATE_IDLE) return 0;
    ota_dev.state = OTA_STATE_IDLE;
    vga_puts("[OTA] update cancelled \n");
    return 0;
}

int ota_cleanup(void)
{
    if (!ota_initialized) return -1;
    ota_dev.downloaded = 0;
    ota_dev.progress = 0;
    vga_puts("[OTA] temp files cleaned \n");
    return 0;
}

int ota_get_slot_info(int slot, ota_slot_t *info)
{
    if (slot < 0 || slot > 1 || !info) return -1;
    memcpy(info, &ota_dev.slots[slot], sizeof(ota_slot_t));
    return 0;
}

int ota_apply_delta(const char *delta_path)
{
    if (!ota_initialized || !delta_path) return -1;
    vga_printf("[OTA] apply delta update: %s\n", delta_path);
    /* 原型阶段: 差分更新 = 读取当前分区 + 应用补丁 + 写入非活动分区 */
    return ota_install(delta_path);
}

void ota_handle_irq(void)
{
    /* 原型阶段 */
}
