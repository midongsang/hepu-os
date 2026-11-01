/* ============================================================================
 * 赫普OS - 系统信息工具（V8.75新增）
 * 详细硬件/软件信息展示
 * ========================================================================== */

#ifndef _SYS_INFO_APP_H
#define _SYS_INFO_APP_H

#include <stdint.h>

/* CPU信息 */
typedef struct {
    char     vendor[16];      /* CPU厂商 */
    char     model_name[64];  /* CPU型号 */
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t cores;
    uint32_t threads;
    uint32_t cache_kb;        /* 缓存大小 */
    uint64_t frequency_mhz;   /* 主频 */
    uint32_t features;        /* 特性标志位 */
    int      x86_64;          /* 是否支持64位 */
    int      pae;             /* 是否支持PAE */
    int      nx;              /* 是否支持NX位 */
    int      smep;            /* 是否支持SMEP */
    int      smap;            /* 是否支持SMAP */
} cpu_info_t;

/* 内存信息 */
typedef struct {
    uint64_t total_kb;
    uint64_t used_kb;
    uint64_t free_kb;
    uint64_t cached_kb;
    uint64_t buffers_kb;
    uint64_t swap_total_kb;
    uint64_t swap_used_kb;
    uint64_t kernel_used_kb;
    uint64_t user_used_kb;
    uint32_t page_size;
    uint32_t total_pages;
    uint32_t free_pages;
} memory_info_t;

/* 存储信息 */
typedef struct {
    char     device[32];
    char     fs_type[16];
    char     mount_point[64];
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint32_t block_size;
    uint32_t inodes_total;
    uint32_t inodes_free;
} storage_info_t;

/* 操作系统信息 */
typedef struct {
    char     os_name[32];
    char     os_version[32];
    char     kernel_version[32];
    char     build_date[32];
    char     arch[16];
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t patch_version;
    uint64_t uptime_ms;
    uint32_t boot_count;
    char     compiler[32];
    char     compiler_version[16];
} os_info_t;

/* 显示信息 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t refresh_rate;
    char     driver[32];
    uint32_t framebuffer_addr;
    uint64_t framebuffer_size;
    uint32_t vram_mb;
} display_info_t;

/* 系统信息汇总 */
typedef struct {
    os_info_t      os;
    cpu_info_t     cpu;
    memory_info_t  memory;
    display_info_t display;
    storage_info_t storage[4];
    int            storage_count;
    int            initialized;
    int            current_tab;  /* 0=概览, 1=CPU, 2=内存, 3=存储, 4=显示 */
} sys_info_t;

/* 初始化系统信息 */
int sys_info_init(void);

/* 关闭 */
void sys_info_shutdown(void);

/* 刷新信息 */
int sys_info_refresh(void);

/* 获取OS信息 */
int sys_info_get_os(os_info_t *info);

/* 获取CPU信息 */
int sys_info_get_cpu(cpu_info_t *info);

/* 获取内存信息 */
int sys_info_get_memory(memory_info_t *info);

/* 获取存储信息 */
int sys_info_get_storage(storage_info_t *info, int max_count);

/* 获取显示信息 */
int sys_info_get_display(display_info_t *info);

/* 切换标签页 */
void sys_info_set_tab(int tab);

/* 绘制UI */
void sys_info_draw_ui(int x, int y, int width, int height);

/* 处理键盘 */
void sys_info_handle_key(char key);

/* 获取状态 */
int sys_info_get_state(sys_info_t *state);

#endif /* _SYS_INFO_APP_H */
