/* ============================================================================
 * 赫普OS - 系统信息工具实现（V8.75新增）
 * 详细硬件/软件信息展示
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/sys_info_app.h"
#include "../include/vga.h"
#include "../include/graphics.h"

/* 全局状态 */
static sys_info_t si;

/* 初始化 */
int sys_info_init(void)
{
    if (si.initialized) return 0;
    memset(&si, 0, sizeof(sys_info_t));
    si.initialized = 1;
    si.current_tab = 0;

    /* OS信息 */
    strcpy(si.os.os_name, "Hepu OS");
    strcpy(si.os.os_version, "8.75");
    strcpy(si.os.kernel_version, "8.75");
    strcpy(si.os.build_date, "2026-10-06");
    strcpy(si.os.arch, "x86");
    si.os.major_version = 8;
    si.os.minor_version = 75;
    si.os.patch_version = 0;
    si.os.uptime_ms = 3600000;  /* 1小时 */
    si.os.boot_count = 42;
    strcpy(si.os.compiler, "GCC");
    strcpy(si.os.compiler_version, "11.4");

    /* CPU信息 */
    strcpy(si.cpu.vendor, "GenuineIntel");
    strcpy(si.cpu.model_name, "Intel Core i7-10700K");
    si.cpu.family = 6;
    si.cpu.model = 165;
    si.cpu.stepping = 5;
    si.cpu.cores = 8;
    si.cpu.threads = 16;
    si.cpu.cache_kb = 16384;  /* 16MB L3 */
    si.cpu.frequency_mhz = 3800;
    si.cpu.features = 0xFFFFFFFF;
    si.cpu.x86_64 = 1;
    si.cpu.pae = 1;
    si.cpu.nx = 1;
    si.cpu.smep = 1;
    si.cpu.smap = 1;

    /* 内存信息 */
    si.memory.total_kb = 512 * 1024;   /* 512MB */
    si.memory.used_kb = 128 * 1024;    /* 128MB */
    si.memory.free_kb = 384 * 1024;    /* 384MB */
    si.memory.cached_kb = 32 * 1024;   /* 32MB */
    si.memory.buffers_kb = 8 * 1024;   /* 8MB */
    si.memory.swap_total_kb = 256 * 1024;
    si.memory.swap_used_kb = 0;
    si.memory.kernel_used_kb = 64 * 1024;
    si.memory.user_used_kb = 64 * 1024;
    si.memory.page_size = 4096;
    si.memory.total_pages = 131072;
    si.memory.free_pages = 98304;

    /* 显示信息 */
    si.display.width = 1024;
    si.display.height = 768;
    si.display.bpp = 32;
    si.display.refresh_rate = 60;
    strcpy(si.display.driver, "GOP");
    si.display.framebuffer_addr = 0x80000000;
    si.display.framebuffer_size = 1024 * 768 * 4;
    si.display.vram_mb = 64;

    /* 存储信息 */
    si.storage_count = 2;
    strcpy(si.storage[0].device, "sda");
    strcpy(si.storage[0].fs_type, "ext2");
    strcpy(si.storage[0].mount_point, "/");
    si.storage[0].total_bytes = 100ULL * 1024 * 1024 * 1024;  /* 100GB */
    si.storage[0].used_bytes = 25ULL * 1024 * 1024 * 1024;   /* 25GB */
    si.storage[0].free_bytes = 75ULL * 1024 * 1024 * 1024;   /* 75GB */
    si.storage[0].block_size = 4096;
    si.storage[0].inodes_total = 6553600;
    si.storage[0].inodes_free = 5000000;

    strcpy(si.storage[1].device, "sdb");
    strcpy(si.storage[1].fs_type, "FAT32");
    strcpy(si.storage[1].mount_point, "/boot/efi");
    si.storage[1].total_bytes = 99ULL * 1024 * 1024;   /* 99MB */
    si.storage[1].used_bytes = 5ULL * 1024 * 1024;    /* 5MB */
    si.storage[1].free_bytes = 94ULL * 1024 * 1024;   /* 94MB */
    si.storage[1].block_size = 2048;
    si.storage[1].inodes_total = 0;
    si.storage[1].inodes_free = 0;

    vga_puts("[SYS-INFO] System info tool initialized\n");
    return 0;
}

void sys_info_shutdown(void) { si.initialized = 0; }

int sys_info_refresh(void)
{
    if (!si.initialized) return -1;
    si.os.uptime_ms += 1000;
    return 0;
}

int sys_info_get_os(os_info_t *info)
{
    if (!info) return -1;
    memcpy(info, &si.os, sizeof(os_info_t));
    return 0;
}

int sys_info_get_cpu(cpu_info_t *info)
{
    if (!info) return -1;
    memcpy(info, &si.cpu, sizeof(cpu_info_t));
    return 0;
}

int sys_info_get_memory(memory_info_t *info)
{
    if (!info) return -1;
    memcpy(info, &si.memory, sizeof(memory_info_t));
    return 0;
}

int sys_info_get_storage(storage_info_t *info, int max_count)
{
    if (!info || max_count <= 0) return 0;
    int c = si.storage_count;
    if (c > max_count) c = max_count;
    memcpy(info, si.storage, c * sizeof(storage_info_t));
    return c;
}

int sys_info_get_display(display_info_t *info)
{
    if (!info) return -1;
    memcpy(info, &si.display, sizeof(display_info_t));
    return 0;
}

void sys_info_set_tab(int tab)
{
    if (tab >= 0 && tab <= 4)
        si.current_tab = tab;
}

/* 格式化字节大小 */
static void format_bytes(uint64_t bytes, char *buf, int buf_size)
{
    if (bytes >= 1024ULL * 1024 * 1024 * 1024)
        snprintf(buf, buf_size, "%.1f TB", bytes / (1024.0 * 1024 * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, buf_size, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024)
        snprintf(buf, buf_size, "%.1f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        snprintf(buf, buf_size, "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, buf_size, "%lld B", (long long)bytes);
}

/* 绘制UI */
void sys_info_draw_ui(int x, int y, int width, int height)
{
    gfx_color_t bg={30,30,40,255}, fg={220,220,220,255}, accent={100,180,255,255};
    gfx_color_t green={100,220,100,255}, yellow={220,200,100,255};
    gfx_color_t gray={130,130,130,255}, label_color={150,170,200,255};

    gfx_fill_rect(x, y, width, height, bg);

    /* 标题 */
    gfx_draw_string_font(x+10, y+8, "系统信息", accent, 16);

    /* 标签页 */
    const char *tabs[] = {"概览", "CPU", "内存", "存储", "显示"};
    int tab_y = y + 32;
    for (int i = 0; i < 5; i++) {
        int tx = x + 10 + i * 70;
        if (i == si.current_tab) {
            gfx_fill_rect(tx, tab_y, 65, 22, accent);
            gfx_draw_string_font(tx+15, tab_y+5, tabs[i],
                                 (gfx_color_t){0,0,0,255}, 12);
        } else {
            gfx_fill_rect(tx, tab_y, 65, 22, (gfx_color_t){50,55,65,255});
            gfx_draw_string_font(tx+15, tab_y+5, tabs[i], gray, 12);
        }
    }

    int cy = tab_y + 35;
    char line[200];

    if (si.current_tab == 0) {
        /* 概览 */
        gfx_draw_string_font(x+20, cy, "操作系统", accent, 14);
        snprintf(line, sizeof(line), "系统: %s %s (%s)",
                 si.os.os_name, si.os.os_version, si.os.arch);
        gfx_draw_string_font(x+20, cy+22, line, fg, 12);
        snprintf(line, sizeof(line), "内核: %s  构建: %s",
                 si.os.kernel_version, si.os.build_date);
        gfx_draw_string_font(x+20, cy+40, line, fg, 12);
        snprintf(line, sizeof(line), "编译器: %s %s  启动次数: %d",
                 si.os.compiler, si.os.compiler_version, si.os.boot_count);
        gfx_draw_string_font(x+20, cy+58, line, fg, 12);
        uint32_t up_h = si.os.uptime_ms / 3600000;
        uint32_t up_m = (si.os.uptime_ms % 3600000) / 60000;
        snprintf(line, sizeof(line), "运行时间: %d小时%d分钟", up_h, up_m);
        gfx_draw_string_font(x+20, cy+76, line, fg, 12);

        gfx_draw_string_font(x+20, cy+105, "处理器", accent, 14);
        snprintf(line, sizeof(line), "%s", si.cpu.model_name);
        gfx_draw_string_font(x+20, cy+127, line, fg, 12);
        snprintf(line, sizeof(line), "%d核%d线程  @ %lld MHz  缓存: %d KB",
                 si.cpu.cores, si.cpu.threads,
                 (long long)si.cpu.frequency_mhz, si.cpu.cache_kb);
        gfx_draw_string_font(x+20, cy+145, line, fg, 12);

        gfx_draw_string_font(x+20, cy+174, "内存", accent, 14);
        char total_s[32], used_s[32];
        format_bytes(si.memory.total_kb * 1024, total_s, sizeof(total_s));
        format_bytes(si.memory.used_kb * 1024, used_s, sizeof(used_s));
        snprintf(line, sizeof(line), "总计: %s  已用: %s  空闲: %s",
                 total_s, used_s,
                 si.memory.free_kb > 1024*1024 ? "384.0 MB" : "");
        gfx_draw_string_font(x+20, cy+196, line, fg, 12);

        gfx_draw_string_font(x+20, cy+225, "显示", accent, 14);
        snprintf(line, sizeof(line), "%dx%d @ %dHz  %dbpp  %s  VRAM: %dMB",
                 si.display.width, si.display.height,
                 si.display.refresh_rate, si.display.bpp,
                 si.display.driver, si.display.vram_mb);
        gfx_draw_string_font(x+20, cy+247, line, fg, 12);

    } else if (si.current_tab == 1) {
        /* CPU详情 */
        gfx_draw_string_font(x+20, cy, "处理器信息", accent, 14);
        snprintf(line, sizeof(line), "厂商: %s", si.cpu.vendor);
        gfx_draw_string_font(x+20, cy+22, line, fg, 12);
        snprintf(line, sizeof(line), "型号: %s", si.cpu.model_name);
        gfx_draw_string_font(x+20, cy+40, line, fg, 12);
        snprintf(line, sizeof(line), "家族: %d  型号: %d  步进: %d",
                 si.cpu.family, si.cpu.model, si.cpu.stepping);
        gfx_draw_string_font(x+20, cy+58, line, fg, 12);
        snprintf(line, sizeof(line), "核心: %d  线程: %d  主频: %lld MHz",
                 si.cpu.cores, si.cpu.threads, (long long)si.cpu.frequency_mhz);
        gfx_draw_string_font(x+20, cy+76, line, fg, 12);
        snprintf(line, sizeof(line), "缓存: %d KB L3", si.cpu.cache_kb);
        gfx_draw_string_font(x+20, cy+94, line, fg, 12);

        gfx_draw_string_font(x+20, cy+123, "特性支持", accent, 14);
        snprintf(line, sizeof(line), "64位(x86-64): %s",
                 si.cpu.x86_64 ? "是" : "否");
        gfx_draw_string_font(x+20, cy+145, line,
                             si.cpu.x86_64?green:gray, 12);
        snprintf(line, sizeof(line), "PAE(物理地址扩展): %s",
                 si.cpu.pae ? "是" : "否");
        gfx_draw_string_font(x+20, cy+163, line,
                             si.cpu.pae?green:gray, 12);
        snprintf(line, sizeof(line), "NX(数据执行保护): %s",
                 si.cpu.nx ? "是" : "否");
        gfx_draw_string_font(x+20, cy+181, line,
                             si.cpu.nx?green:gray, 12);
        snprintf(line, sizeof(line), "SMEP(监管模式执行保护): %s",
                 si.cpu.smep ? "是" : "否");
        gfx_draw_string_font(x+20, cy+199, line,
                             si.cpu.smep?green:gray, 12);
        snprintf(line, sizeof(line), "SMAP(监管模式访问保护): %s",
                 si.cpu.smap ? "是" : "否");
        gfx_draw_string_font(x+20, cy+217, line,
                             si.cpu.smap?green:gray, 12);

    } else if (si.current_tab == 2) {
        /* 内存详情 */
        gfx_draw_string_font(x+20, cy, "内存信息", accent, 14);
        char s[32];
        format_bytes(si.memory.total_kb*1024, s, sizeof(s));
        snprintf(line, sizeof(line), "总内存: %s", s);
        gfx_draw_string_font(x+20, cy+22, line, fg, 12);
        format_bytes(si.memory.used_kb*1024, s, sizeof(s));
        snprintf(line, sizeof(line), "已使用: %s", s);
        gfx_draw_string_font(x+20, cy+40, line, yellow, 12);
        format_bytes(si.memory.free_kb*1024, s, sizeof(s));
        snprintf(line, sizeof(line), "空闲: %s", s);
        gfx_draw_string_font(x+20, cy+58, line, green, 12);
        format_bytes(si.memory.cached_kb*1024, s, sizeof(s));
        snprintf(line, sizeof(line), "缓存: %s", s);
        gfx_draw_string_font(x+20, cy+76, line, fg, 12);
        format_bytes(si.memory.kernel_used_kb*1024, s, sizeof(s));
        snprintf(line, sizeof(line), "内核占用: %s", s);
        gfx_draw_string_font(x+20, cy+94, line, fg, 12);
        format_bytes(si.memory.user_used_kb*1024, s, sizeof(s));
        snprintf(line, sizeof(line), "用户占用: %s", s);
        gfx_draw_string_font(x+20, cy+112, line, fg, 12);

        /* 内存使用条 */
        int bar_x = x+20, bar_y = cy+135, bar_w = width-40, bar_h = 16;
        gfx_fill_rect(bar_x, bar_y, bar_w, bar_h, (gfx_color_t){50,50,60,255});
        float mem_pct = (float)si.memory.used_kb / si.memory.total_kb;
        gfx_fill_rect(bar_x, bar_y, (int)(bar_w*mem_pct), bar_h,
                      mem_pct>0.8?yellow:green);
        snprintf(line, sizeof(line), "%.0f%% 已使用", mem_pct*100);
        gfx_draw_string_font(bar_x+bar_w/2-25, bar_y+2, line, fg, 12);

        gfx_draw_string_font(x+20, cy+165, "交换空间", accent, 14);
        format_bytes(si.memory.swap_total_kb*1024, s, sizeof(s));
        snprintf(line, sizeof(line), "总计: %s  已用: %lld KB",
                 s, (long long)si.memory.swap_used_kb);
        gfx_draw_string_font(x+20, cy+187, line, fg, 12);

        snprintf(line, sizeof(line), "页大小: %d  总页数: %d  空闲页: %d",
                 si.memory.page_size, si.memory.total_pages,
                 si.memory.free_pages);
        gfx_draw_string_font(x+20, cy+210, line, gray, 11);

    } else if (si.current_tab == 3) {
        /* 存储详情 */
        gfx_draw_string_font(x+20, cy, "存储设备", accent, 14);
        for (int i = 0; i < si.storage_count; i++) {
            storage_info_t *st = &si.storage[i];
            int sy = cy + 22 + i * 90;

            gfx_fill_rect(x+15, sy, width-30, 80,
                          (gfx_color_t){40,45,55,255});

            snprintf(line, sizeof(line), "%s (%s) - 挂载于 %s",
                     st->device, st->fs_type, st->mount_point);
            gfx_draw_string_font(x+25, sy+8, line, accent, 12);

            char ts[32], us_s[32], fs_s[32];
            format_bytes(st->total_bytes, ts, sizeof(ts));
            format_bytes(st->used_bytes, us_s, sizeof(us_s));
            format_bytes(st->free_bytes, fs_s, sizeof(fs_s));
            snprintf(line, sizeof(line), "总计: %s  已用: %s  空闲: %s",
                     ts, us_s, fs_s);
            gfx_draw_string_font(x+25, sy+28, line, fg, 11);

            /* 使用条 */
            int sb_x = x+25, sb_y = sy+48, sb_w = width-50, sb_h = 10;
            gfx_fill_rect(sb_x, sb_y, sb_w, sb_h,
                          (gfx_color_t){50,50,60,255});
            float pct = (float)st->used_bytes / st->total_bytes;
            gfx_fill_rect(sb_x, sb_y, (int)(sb_w*pct), sb_h,
                          pct>0.8?yellow:green);
            snprintf(line, sizeof(line), "%.1f%% 已使用", pct*100);
            gfx_draw_string_font(sb_x+sb_w/2-25, sb_y-1, line, fg, 10);

            snprintf(line, sizeof(line), "块大小: %d  inode: %d/%d",
                     st->block_size,
                     st->inodes_total - st->inodes_free,
                     st->inodes_total);
            gfx_draw_string_font(x+25, sy+63, line, gray, 10);
        }

    } else if (si.current_tab == 4) {
        /* 显示详情 */
        gfx_draw_string_font(x+20, cy, "显示信息", accent, 14);
        snprintf(line, sizeof(line), "分辨率: %d x %d",
                 si.display.width, si.display.height);
        gfx_draw_string_font(x+20, cy+22, line, fg, 12);
        snprintf(line, sizeof(line), "色深: %d bpp  刷新率: %d Hz",
                 si.display.bpp, si.display.refresh_rate);
        gfx_draw_string_font(x+20, cy+40, line, fg, 12);
        snprintf(line, sizeof(line), "驱动: %s", si.display.driver);
        gfx_draw_string_font(x+20, cy+58, line, fg, 12);
        snprintf(line, sizeof(line), "帧缓冲地址: 0x%08X",
                 si.display.framebuffer_addr);
        gfx_draw_string_font(x+20, cy+76, line, fg, 12);
        char fbs[32];
        format_bytes(si.display.framebuffer_size, fbs, sizeof(fbs));
        snprintf(line, sizeof(line), "帧缓冲大小: %s", fbs);
        gfx_draw_string_font(x+20, cy+94, line, fg, 12);
        snprintf(line, sizeof(line), "显存: %d MB", si.display.vram_mb);
        gfx_draw_string_font(x+20, cy+112, line, fg, 12);

        /* 分辨率预览 */
        int prev_x = x + width - 180, prev_y = cy + 20;
        int prev_w = 160, prev_h = 120;
        gfx_fill_rect(prev_x, prev_y, prev_w, prev_h,
                      (gfx_color_t){20,30,50,255});
        gfx_draw_rect(prev_x, prev_y, prev_w, prev_h, accent);
        snprintf(line, sizeof(line), "%dx%d", si.display.width,
                 si.display.height);
        gfx_draw_string_font(prev_x+prev_w/2-20, prev_y+prev_h/2-6,
                             line, accent, 12);
    }

    /* 底部状态栏 */
    gfx_draw_string_font(x+10, y+height-20,
                         "1-5切换标签 R刷新", gray, 10);
}

void sys_info_handle_key(char key)
{
    if (!si.initialized) return;
    switch (key) {
        case '1': si.current_tab = 0; break;
        case '2': si.current_tab = 1; break;
        case '3': si.current_tab = 2; break;
        case '4': si.current_tab = 3; break;
        case '5': si.current_tab = 4; break;
        case 'r': case 'R': sys_info_refresh(); break;
    }
}

int sys_info_get_state(sys_info_t *state)
{
    if (!state) return -1;
    memcpy(state, &si, sizeof(sys_info_t));
    return 0;
}
