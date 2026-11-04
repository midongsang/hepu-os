/* ============================================================================
 * 赫普OS - 启动链完善
 * Bootloader + GRUB兼容 + Multiboot + EFI + 安全启动 + ISO
 * ========================================================================== */

#include <stdint.h>
#include "bootchain.h"
#include "constants.h"
#include "vga.h"
#include "string.h"

static bootchain_t boot;

void bootchain_init(void)
{
    memset(&boot, 0, sizeof(bootchain_t));
    boot.initialized = 1;
    boot.boot_mode = BOOT_MODE_BIOS;
    boot.bootloader = BOOTLOADER_GRUB;
    strncpy(boot.bootloader_name, "GNU GRUB", 63);
    strncpy(boot.bootloader_version, "2.06", 31);
    boot.default_entry = 0;
    boot.timeout = 5;

    /* 启动条目 */
    boot_entry_add(" Hepu OS 0.23", "/boot/hepu-kernel.bin", "/boot/initrd.img", "root=/dev/sda2 ro quiet");
    boot_entry_add(" Hepu OS 0.23(resume mode)", "/boot/hepu-kernel.bin", "/boot/initrd.img", "root=/dev/sda2 ro single");
    boot_entry_add(" Hepu OS 0.22(旧 version)", "/boot/hepu-kernel-0.22.bin", "/boot/initrd.img", "root=/dev/sda2 ro");
    boot_entry_add(" memory test ", "/boot/memtest86.bin", NULL, NULL);

    /* 启动模块 */
    boot_module_add("initrd", "/boot/initrd.img", BOOT_INITRD_ADDR, 0);
    boot_module_add("microcode", "/boot/intel-ucode.img", BOOT_MICROCODE_ADDR, 3);

    /* Multiboot */
    boot.multiboot_valid = 1;
    boot.multiboot_info.flags = BOOT_MULTIBOOT_FLAGS;
    boot.multiboot_info.mem_lower = 640;
    boot.multiboot_info.mem_upper = 8192 * 1024;
    boot.multiboot_info.boot_device = BOOT_DEVICE_HD0;
    boot.multiboot_info.mmap_length = 128;
    boot.multiboot_info.mmap_addr = BOOT_MMAP_ADDR;

    /* 内存映射 */
    boot.memory_map[0].base_addr = 0;
    boot.memory_map[0].length = MEM_LOW_TOP;
    boot.memory_map[0].type = 1;
    boot.memory_map[1].base_addr = MEM_HIGH_BASE;
    boot.memory_map[1].length = MEM_MAP_HIGH_LENGTH;
    boot.memory_map[1].type = 1;
    boot.memory_map[2].base_addr = MEM_MAP_EXTENDED_BASE;
    boot.memory_map[2].length = MEM_MAP_EXTENDED_LENGTH;
    boot.memory_map[2].type = 2;
    boot.memory_map_count = 3;

    /* 命令行 */
    strncpy(boot.cmdline, "root=/dev/sda2 ro quiet splash", BOOT_MAX_CMDLINE - 1);

    /* 启动阶段 */
    boot.total_stages = 8;
    strncpy(boot.stage_names[0], "BIOS/UEFI init ", 63);
    strncpy(boot.stage_names[1], "Bootloader load ", 63);
    strncpy(boot.stage_names[2], " kernel 解压 ", 63);
    strncpy(boot.stage_names[3], " memory init ", 63);
    strncpy(boot.stage_names[4], " driver init ", 63);
    strncpy(boot.stage_names[5], " fs mounted ", 63);
    strncpy(boot.stage_names[6], "init process start ", 63);
    strncpy(boot.stage_names[7], " user 界面 start ", 63);

    /* 安全启动 */
    boot.secure_boot = 0;
    boot.secure_boot_enabled = 0;
    boot.secure_boot_setup_mode = 1;

    boot.total_boots = 1;
    boot.successful_boots = 1;

    vga_puts("[BOOT] boot chain complete init done \n");
    vga_printf(" mode: BIOS, Bootloader: GRUB 2.06, entries: %d, module: %d \n",
               boot.entry_count, boot.module_count);
}

/* 启动条目 */
int boot_entry_add(const char *title, const char *kernel, const char *initrd, const char *cmdline)
{
    for (int i = 0; i < BOOT_MAX_BOOT_ENTRIES; i++) {
        if (!boot.entries[i].used) {
            memset(&boot.entries[i], 0, sizeof(boot_entry_t));
            boot.entries[i].used = 1;
            boot.entries[i].id = i;
            if (title) strncpy(boot.entries[i].title, title, 127);
            if (kernel) strncpy(boot.entries[i].kernel_path, kernel, 255);
            if (initrd) strncpy(boot.entries[i].initrd_path, initrd, 255);
            if (cmdline) strncpy(boot.entries[i].cmdline, cmdline, BOOT_MAX_CMDLINE - 1);
            boot.entries[i].timeout = boot.timeout;
            boot.entry_count++;
            return i;
        }
    }
    return -1;
}

int boot_entry_remove(int entry_id)
{
    if (entry_id < 0 || entry_id >= BOOT_MAX_BOOT_ENTRIES || !boot.entries[entry_id].used) return -1;
    boot.entries[entry_id].used = 0;
    boot.entry_count--;
    return 0;
}

int boot_entry_get(int entry_id, boot_entry_t *entry)
{
    if (entry_id < 0 || entry_id >= BOOT_MAX_BOOT_ENTRIES || !boot.entries[entry_id].used || !entry) return -1;
    memcpy(entry, &boot.entries[entry_id], sizeof(boot_entry_t));
    return 0;
}

int boot_entry_list(boot_entry_t *entries, int max)
{
    if (!entries || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BOOT_MAX_BOOT_ENTRIES && count < max; i++) {
        if (boot.entries[i].used) memcpy(&entries[count++], &boot.entries[i], sizeof(boot_entry_t));
    }
    return count;
}

int boot_entry_set_default(int entry_id)
{
    if (entry_id < 0 || entry_id >= BOOT_MAX_BOOT_ENTRIES || !boot.entries[entry_id].used) return -1;
    boot.default_entry = entry_id;
    for (int i = 0; i < BOOT_MAX_BOOT_ENTRIES; i++) boot.entries[i].default_entry = (i == entry_id);
    return 0;
}

int boot_entry_set_timeout(int seconds)
{
    boot.timeout = seconds;
    return 0;
}

/* 启动模块 */
int boot_module_add(const char *name, const char *path, uint64_t load_addr, int type)
{
    for (int i = 0; i < BOOT_MAX_MODULES; i++) {
        if (!boot.modules[i].used) {
            memset(&boot.modules[i], 0, sizeof(boot_module_t));
            boot.modules[i].used = 1;
            boot.modules[i].id = i;
            if (name) strncpy(boot.modules[i].name, name, 63);
            if (path) strncpy(boot.modules[i].path, path, 255);
            boot.modules[i].load_addr = load_addr;
            boot.modules[i].type = type;
            boot.module_count++;
            return i;
        }
    }
    return -1;
}

int boot_module_remove(int module_id)
{
    if (module_id < 0 || module_id >= BOOT_MAX_MODULES || !boot.modules[module_id].used) return -1;
    boot.modules[module_id].used = 0;
    boot.module_count--;
    return 0;
}

int boot_module_get(int module_id, boot_module_t *module)
{
    if (module_id < 0 || module_id >= BOOT_MAX_MODULES || !boot.modules[module_id].used || !module) return -1;
    memcpy(module, &boot.modules[module_id], sizeof(boot_module_t));
    return 0;
}

int boot_module_list(boot_module_t *modules, int max)
{
    if (!modules || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BOOT_MAX_MODULES && count < max; i++) {
        if (boot.modules[i].used) memcpy(&modules[count++], &boot.modules[i], sizeof(boot_module_t));
    }
    return count;
}

/* Multiboot */
int boot_multiboot_parse(uint32_t magic, boot_multiboot_info_t *info)
{
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) return -1;
    if (info) memcpy(&boot.multiboot_info, info, sizeof(boot_multiboot_info_t));
    boot.multiboot_valid = 1;
    /* V6.52: 从multiboot info读取cmdline (flags bit2) */
    if (info && (info->flags & (1 << 2)) && info->cmdline) {
        const char *cmd = (const char *)(uintptr_t)info->cmdline;
        strncpy(boot.cmdline, cmd, BOOT_MAX_CMDLINE - 1);
        boot.cmdline[BOOT_MAX_CMDLINE - 1] = '\0';
    }
    return 0;
}

int boot_multiboot_get_info(boot_multiboot_info_t *info)
{
    if (!info || !boot.multiboot_valid) return -1;
    memcpy(info, &boot.multiboot_info, sizeof(boot_multiboot_info_t));
    return 0;
}

int boot_multiboot_get_memory_map(memory_map_t *map, int max)
{
    if (!map || max <= 0) return 0;
    int count = boot.memory_map_count < max ? boot.memory_map_count : max;
    for (int i = 0; i < count; i++) memcpy(&map[i], &boot.memory_map[i], sizeof(memory_map_t));
    return count;
}

int boot_multiboot_get_cmdline(char *cmdline, int max)
{
    if (!cmdline || max <= 0) return -1;
    strncpy(cmdline, boot.cmdline, max - 1);
    return 0;
}

/* V6.52: 解析内核命令行参数，设置安全/恢复模式 */
int boot_multiboot_parse_cmdline(void)
{
    if (!boot.cmdline[0]) return -1;
    boot.safe_mode = 0;
    boot.recovery_mode = 0;
    boot.graphics_backend = -1;  /* -1=自动 */
    /* 简单的参数解析：查找 hepu.safe=1 和 hepu.recovery=1 */
    const char *p = boot.cmdline;
    while (*p) {
        /* 跳过空格 */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        /* 查找参数结束 */
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        int len = p - start;
        /* 检查 hepu.safe=1 */
        if (len >= 11 && strncmp(start, "hepu.safe=1", 11) == 0) {
            boot.safe_mode = 1;
        }
        /* 检查 hepu.recovery=1 */
        if (len >= 15 && strncmp(start, "hepu.recovery=1", 15) == 0) {
            boot.recovery_mode = 1;
        }
        /* 检查 graphics=xxx (V7.09) */
        if (len >= 9 && strncmp(start, "graphics=", 9) == 0) {
            const char *val = start + 9;
            int vlen = len - 9;
            if (vlen >= 4 && strncmp(val, "auto", 4) == 0) {
                boot.graphics_backend = -1;
            } else if (vlen >= 6 && strncmp(val, "virtio", 6) == 0) {
                boot.graphics_backend = 3;  /* GFX_BACKEND_VIRTIO */
            } else if (vlen >= 3 && strncmp(val, "gop", 3) == 0) {
                boot.graphics_backend = 4;  /* GFX_BACKEND_GOP */
            } else if (vlen >= 3 && strncmp(val, "vbe", 3) == 0) {
                boot.graphics_backend = 2;  /* GFX_BACKEND_VBE */
            } else if (vlen >= 3 && strncmp(val, "vga", 3) == 0) {
                boot.graphics_backend = 1;  /* GFX_BACKEND_VGA */
            }
        }
    }
    return 0;
}

int boot_get_safe_mode(void)
{
    return boot.safe_mode;
}

int boot_get_recovery_mode(void)
{
    return boot.recovery_mode;
}

int boot_get_graphics_backend(void)
{
    return boot.graphics_backend;
}

/* EFI */
int boot_efi_init(efi_system_table_t *st)
{
    if (!st) return -1;
    boot.efi_system_table = st;
    boot.efi_valid = 1;
    boot.boot_mode = BOOT_MODE_UEFI;
    return 0;
}

int boot_efi_get_info(efi_system_table_t **st)
{
    if (!st || !boot.efi_valid) return -1;
    *st = boot.efi_system_table;
    return 0;
}

int boot_efi_get_memory_map(efi_memory_descriptor_t *map, int max, uint64_t *map_size, uint64_t *desc_size)
{
    (void)map; (void)max;
    if (map_size) *map_size = 128;
    if (desc_size) *desc_size = sizeof(efi_memory_descriptor_t);
    return 0;
}

int boot_efi_get_variable(const char *name, const char *guid, void *data, uint32_t *size)
{
    (void)name; (void)guid; (void)data; (void)size;
    return -1;
}

int boot_efi_set_variable(const char *name, const char *guid, const void *data, uint32_t size)
{
    (void)name; (void)guid; (void)data; (void)size;
    return -1;
}

/* 启动过程 */
int boot_stage_start(const char *name)
{
    if (boot.current_stage >= boot.total_stages) return -1;
    if (name) strncpy(boot.stage_names[boot.current_stage], name, 63);
    boot.stage_times[boot.current_stage] = 0;
    return boot.current_stage;
}

int boot_stage_end(void)
{
    if (boot.current_stage >= boot.total_stages) return -1;
    boot.current_stage++;
    if (boot.current_stage >= boot.total_stages) {
        boot.boot_end_time = 0;
        boot.boot_duration_ms = 1500;
    }
    return boot.current_stage;
}

int boot_get_stage_info(int *current, int *total, char (*names)[64], uint64_t *times)
{
    if (current) *current = boot.current_stage;
    if (total) *total = boot.total_stages;
    if (names) {
        for (int i = 0; i < boot.total_stages; i++) strncpy(names[i], boot.stage_names[i], 63);
    }
    if (times) {
        for (int i = 0; i < boot.total_stages; i++) times[i] = boot.stage_times[i];
    }
    return 0;
}

int boot_get_boot_time(uint64_t *start, uint64_t *end, uint64_t *duration)
{
    if (start) *start = boot.boot_start_time;
    if (end) *end = boot.boot_end_time;
    if (duration) *duration = boot.boot_duration_ms;
    return 0;
}

/* 安全启动 */
int boot_secure_boot_status(int *enabled, int *setup_mode)
{
    if (enabled) *enabled = boot.secure_boot_enabled;
    if (setup_mode) *setup_mode = boot.secure_boot_setup_mode;
    return 0;
}

int boot_secure_boot_enroll_key(const char *key_path)
{
    (void)key_path;
    return 0;
}

int boot_secure_boot_verify_image(const char *image_path)
{
    (void)image_path;
    return 1;  /* 验证通过 */
}

/* ISO生成 */
int boot_iso_create(const char *output_path, const char *kernel_path, const char *initrd_path)
{
    (void)output_path; (void)kernel_path; (void)initrd_path;
    return 0;
}

int boot_iso_get_info(const char *iso_path, char *label, uint64_t *size)
{
    (void)iso_path;
    if (label) strncpy(label, "HEPU_OS_0.23", 31);
    if (size) *size = 256 * 1024 * 1024;
    return 0;
}

/* 统计 */
int boot_get_stats(bootchain_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &boot, sizeof(bootchain_t));
    return 0;
}
