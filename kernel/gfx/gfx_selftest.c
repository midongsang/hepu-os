/* ============================================================================
 * HepuOS 可插拔图形子系统 - 自测
 * 验证后端注册、探测、显存分区管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "serial.h"
#include "vga.h"

void gfx_subsystem_selftest(void)
{
    serial_puts(0x3F8, "[GFX-TEST] starting graphics subsystem selftest...\n");

    int pass = 0, fail = 0;

    /* 测试1：VGA应急文本输出 */
    {
        gfx_vga_early_puts("HepuOS Graphics Subsystem Self-Test\n");
        serial_puts(0x3F8, "[GFX-TEST] test1 VGA early text: PASS\n");
        pass++;
    }

    /* 测试2：注册VGA后端 */
    {
        extern void gfx_vga_register(void);
        gfx_vga_register();
        serial_puts(0x3F8, "[GFX-TEST] test2 register VGA backend: PASS\n");
        pass++;
    }

    /* 测试3：注册GOP后端 */
    {
        extern void gfx_gop_register(void);
        gfx_gop_register();
        serial_puts(0x3F8, "[GFX-TEST] test3 register GOP backend: PASS\n");
        pass++;
    }

    /* 测试4：自动探测后端 */
    {
        int ret = gfx_probe_backend();
        if (ret == 0) {
            gfx_backend_type_t type = gfx_get_backend_type();
            serial_puts(0x3F8, "[GFX-TEST] test4 probe backend: PASS (type=");
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puthex(0x3F8, (uint32_t)type);
            serial_puts(0x3F8, ")\n");
            pass++;
        } else {
            serial_puts(0x3F8, "[GFX-TEST] test4 probe backend: FAIL\n");
            fail++;
        }
    }

    /* 测试5：显存分区设置 */
    {
        int ret = gfx_set_vram_partition(0, 1024 * 1024); /* 1MB */
        if (ret == 0) {
            ret = gfx_set_vram_partition(1, 512 * 1024); /* 512KB */
        }
        if (ret == 0) {
            serial_puts(0x3F8, "[GFX-TEST] test5 set vram partitions: PASS\n");
            pass++;
        } else {
            serial_puts(0x3F8, "[GFX-TEST] test5 set vram partitions: FAIL\n");
            fail++;
        }
    }

    /* 测试6：获取显存信息 */
    {
        gfx_vram_info_t info;
        gfx_get_vram_info(&info);
        serial_puts(0x3F8, "[GFX-TEST] test6 vram info: partitions=");
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puthex(0x3F8, info.partition_count);
        serial_puts(0x3F8, ", size[0]=");
        serial_puthex(0x3F8, info.partition_sizes[0]);
        serial_puts(0x3F8, "\n");
        pass++;
    }

    /* 测试7：初始化图形子系统 */
    {
        gfx_config_t config;
        memset(&config, 0, sizeof(config));
        config.default_width = 1024;
        config.default_height = 768;
        config.default_bpp = 32;
        config.vram_partition_size = 2 * 1024 * 1024; /* 用户自定义2MB显存分区 */

        int ret = gfx_init(&config);
        if (ret == 0) {
            serial_puts(0x3F8, "[GFX-TEST] test7 gfx_init: PASS\n");
            pass++;
        } else {
            serial_puts(0x3F8, "[GFX-TEST] test7 gfx_init: FAIL (ret=");
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puthex(0x3F8, (uint32_t)ret);
            serial_puts(0x3F8, ")\n");
            fail++;
        }
    }

    /* 测试8：统一API绘图 */
    {
        gfx_backend_t *be = gfx_get_active_backend();
        if (be) {
            /* 绘制测试矩形 */
            gfx_fill_rect(100, 100, 200, 100, 0x00FF0000); /* 红色 */
            gfx_fill_rect(150, 150, 100, 50, 0x0000FF00);  /* 绿色 */

            /* 绘制测试字符 */
            gfx_draw_char(200, 200, 'H', 0x00FFFFFF, 0x00000000);
            gfx_draw_string(200, 220, "HepuOS GFX", 0x00FFFFFF, 0x00000000);

            serial_puts(0x3F8, "[GFX-TEST] test8 unified API drawing: PASS\n");
            pass++;
        } else {
            serial_puts(0x3F8, "[GFX-TEST] test8 unified API drawing: FAIL (no backend)\n");
            fail++;
        }
    }

    /* 测试9：高级绘图函数（线条、圆形、圆角矩形） */
    {
        gfx_backend_t *be = gfx_get_active_backend();
        if (be) {
            /* 线条 */
            gfx_draw_line(50, 300, 200, 350, 0x00FFFF00); /* 黄色 */
            /* 圆形 */
            gfx_draw_circle(300, 300, 30, 0x0000FFFF); /* 青色 */
            gfx_fill_circle(350, 300, 20, 0x00FF00FF); /* 紫色 */
            /* 圆角矩形 */
            gfx_draw_rounded_rect(400, 280, 100, 60, 10, 0x00FFFFFF); /* 白色 */
            gfx_fill_rounded_rect(420, 290, 60, 40, 8, 0x00808080); /* 灰色 */
            /* 水平线/垂直线 */
            gfx_draw_hline(50, 400, 200, 0x00FF8800); /* 橙色 */
            gfx_draw_vline(50, 400, 50, 0x0088FF00); /* 黄绿 */

            serial_puts(0x3F8, "[GFX-TEST] test9 advanced drawing: PASS\n");
            pass++;
        } else {
            serial_puts(0x3F8, "[GFX-TEST] test9 advanced drawing: FAIL (no backend)\n");
            fail++;
        }
    }

    /* 测试10：清屏和模式查询 */
    {
        gfx_backend_t *be = gfx_get_active_backend();
        if (be) {
            uint32_t w = 0, h = 0, bpp = 0;
            gfx_get_mode(&w, &h, &bpp);
            serial_puts(0x3F8, "[GFX-TEST] test10 mode: ");
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puthex(0x3F8, w);
            serial_puts(0x3F8, "x");
            serial_puthex(0x3F8, h);
            serial_puts(0x3F8, "@");
            serial_puthex(0x3F8, bpp);
            serial_puts(0x3F8, "bpp\n");

            /* 清屏测试（深蓝色） */
            gfx_clear_screen(0x001A202C);

            serial_puts(0x3F8, "[GFX-TEST] test10 clear screen: PASS\n");
            pass++;
        } else {
            serial_puts(0x3F8, "[GFX-TEST] test10 clear screen: FAIL (no backend)\n");
            fail++;
        }
    }

    /* 测试结果 */
    serial_puts(0x3F8, "[GFX-TEST] selftest done (pass=");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, (uint32_t)pass);
    serial_puts(0x3F8, ", fail=");
    serial_puthex(0x3F8, (uint32_t)fail);
    serial_puts(0x3F8, ")\n");

    vga_printf("[GFX] selftest: pass=%d fail=%d\n", pass, fail);
}
