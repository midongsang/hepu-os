#ifndef BOOTCHAIN_H
#define BOOTCHAIN_H

#include <stdint.h>

#define BOOT_MAX_BOOT_ENTRIES 16
#define BOOT_MAX_MODULES 16
#define BOOT_MAX_CMDLINE 1024

/* 启动模式 */
typedef enum {
    BOOT_MODE_BIOS = 0,
    BOOT_MODE_UEFI,
    BOOT_MODE_UEFI_SECURE,
    BOOT_MODE_COREBOOT,
    BOOT_MODE_LINUXBOOT,
} boot_mode_t;

/* 引导加载程序类型 */
typedef enum {
    BOOTLOADER_GRUB = 0,
    BOOTLOADER_SYSLINUX,
    BOOTLOADER_REFIND,
    BOOTLOADER_SYSTEMD_BOOT,
    BOOTLOADER_HEPU_BOOT,
    BOOTLOADER_UBOOT,
} bootloader_type_t;

/* 启动条目 */
typedef struct {
    int used;
    int id;
    char title[128];
    char kernel_path[256];
    char initrd_path[256];
    char cmdline[BOOT_MAX_CMDLINE];
    char root_device[64];
    int default_entry;
    int timeout;
    int boot_count;
    uint64_t last_boot_time;
    int successful;
} boot_entry_t;

/* 启动模块 */
typedef struct {
    int used;
    int id;
    char name[64];
    char path[256];
    uint64_t load_addr;
    uint64_t size;
    int type;  /* 0=initrd,1=module,2=dtb,3=microcode */
} boot_module_t;

/* Multiboot信息 */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    /* V2.40: VBE framebuffer 信息 (Multiboot 1.6+, flags bit 12) */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;  /* 0=索引色, 1=RGB, 2=EGA文本 */
    uint8_t  color_info[6];
} boot_multiboot_info_t;

/* Multiboot头 */
typedef struct {
    uint32_t magic;  /* 0x1BADB002 */
    uint32_t flags;
    uint32_t checksum;
    uint32_t header_addr;
    uint32_t load_addr;
    uint32_t load_end_addr;
    uint32_t bss_end_addr;
    uint32_t entry_addr;
} boot_multiboot_header_t;

/* 内存映射 */
typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;  /* 1=可用,2=保留,3=ACPI,4=NVS */
} memory_map_t;

/* EFI系统表 */
typedef struct {
    uint64_t signature;  /* 'I','B','I',' ','S','Y','S','T' */
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    void *firmware_vendor;
    uint32_t firmware_revision;
    void *stdin;
    void *stdout;
    void *stderr;
    void *runtime_services;
    void *boot_services;
    uint64_t number_of_table_entries;
    void *configuration_table;
} efi_system_table_t;

/* EFI内存描述符 */
typedef struct {
    uint32_t type;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

/* 启动状态 */
typedef struct {
    int initialized;
    boot_mode_t boot_mode;
    bootloader_type_t bootloader;
    char bootloader_name[64];
    char bootloader_version[32];
    /* 启动条目 */
    boot_entry_t entries[BOOT_MAX_BOOT_ENTRIES];
    int entry_count;
    int default_entry;
    int timeout;
    /* 模块 */
    boot_module_t modules[BOOT_MAX_MODULES];
    int module_count;
    /* Multiboot */
    boot_multiboot_info_t multiboot_info;
    int multiboot_valid;
    /* EFI */
    efi_system_table_t *efi_system_table;
    int efi_valid;
    /* 内存映射 */
    memory_map_t memory_map[32];
    int memory_map_count;
    /* 命令行 */
    char cmdline[BOOT_MAX_CMDLINE];
    /* 启动时间 */
    uint64_t boot_start_time;
    uint64_t boot_end_time;
    uint64_t boot_duration_ms;
    /* 启动阶段 */
    int current_stage;
    int total_stages;
    char stage_names[16][64];
    uint64_t stage_times[16];
    /* 安全启动 */
    int secure_boot;
    int secure_boot_enabled;
    int secure_boot_setup_mode;
    /* V6.52: HepuBoot安全/恢复模式 */
    int safe_mode;          /* hepu.safe=1: 安全模式，禁用问题驱动 */
    int recovery_mode;      /* hepu.recovery=1: 恢复模式，最小化启动 */
    /* V7.09: 图形后端选择 */
    int graphics_backend;   /* graphics=auto/virtio/gop/vbe/vga, -1=自动 */
    /* 统计 */
    uint64_t total_boots;
    uint64_t successful_boots;
    uint64_t failed_boots;
} bootchain_t;

/* 初始化启动链 */
void bootchain_init(void);

/* 启动条目 */
int boot_entry_add(const char *title, const char *kernel, const char *initrd, const char *cmdline);
int boot_entry_remove(int entry_id);
int boot_entry_get(int entry_id, boot_entry_t *entry);
int boot_entry_list(boot_entry_t *entries, int max);
int boot_entry_set_default(int entry_id);
int boot_entry_set_timeout(int seconds);

/* 启动模块 */
int boot_module_add(const char *name, const char *path, uint64_t load_addr, int type);
int boot_module_remove(int module_id);
int boot_module_get(int module_id, boot_module_t *module);
int boot_module_list(boot_module_t *modules, int max);

/* Multiboot */
int boot_multiboot_parse(uint32_t magic, boot_multiboot_info_t *info);
int boot_multiboot_get_info(boot_multiboot_info_t *info);
int boot_multiboot_get_memory_map(memory_map_t *map, int max);
int boot_multiboot_get_cmdline(char *cmdline, int max);
/* V6.52: 解析内核命令行参数，设置安全/恢复模式 */
int boot_multiboot_parse_cmdline(void);
int boot_get_safe_mode(void);
int boot_get_recovery_mode(void);
int boot_get_graphics_backend(void);

/* EFI */
int boot_efi_init(efi_system_table_t *st);
int boot_efi_get_info(efi_system_table_t **st);
int boot_efi_get_memory_map(efi_memory_descriptor_t *map, int max, uint64_t *map_size, uint64_t *desc_size);
int boot_efi_get_variable(const char *name, const char *guid, void *data, uint32_t *size);
int boot_efi_set_variable(const char *name, const char *guid, const void *data, uint32_t size);

/* 启动过程 */
int boot_stage_start(const char *name);
int boot_stage_end(void);
int boot_get_stage_info(int *current, int *total, char (*names)[64], uint64_t *times);
int boot_get_boot_time(uint64_t *start, uint64_t *end, uint64_t *duration);

/* 安全启动 */
int boot_secure_boot_status(int *enabled, int *setup_mode);
int boot_secure_boot_enroll_key(const char *key_path);
int boot_secure_boot_verify_image(const char *image_path);

/* ISO生成 */
int boot_iso_create(const char *output_path, const char *kernel_path, const char *initrd_path);
int boot_iso_get_info(const char *iso_path, char *label, uint64_t *size);

/* 统计 */
int boot_get_stats(bootchain_t *stats);

#endif /* BOOTCHAIN_H */
