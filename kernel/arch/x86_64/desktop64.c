/* ============================================================================
 * 赫普OS 64位 - 图形桌面运行器（Hepu Desktop 专用）
 * V0.13.3: 移除经典桌面（desktop64_draw 等），统一由 Hepu Desktop 承担全部桌面渲染
 * 参考现代系统（Windows DWM / Wayland compositor）：单一合成器 + 分层渲染
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void gfx64_init_from_bootloader(void);
extern void gfx64_get_info(int *width, int *height, int *bpp);

/* Hepu Desktop 接口 */
extern int hpdesk_init(int width, int height);
extern void hpdesk_render(void);
extern void hpdesk_handle_key(int key);
extern void hpdesk_update_mouse(int x, int y);
extern void hpdesk_mark_dirty(void);
extern void hpdesk_claim_owner(void);
extern void hpdesk_deskstate_drain(void);
extern void hpdesk_mouse_down(int x, int y);
extern void hpdesk_mouse_up(int x, int y);
extern void hpdesk_handle_click(int x, int y);
extern void hpdesk_handle_right_click(int x, int y);

/* 输入 */
extern int keyboard64_haschar(void);
extern int keyboard64_getchar(void);
extern void mouse64_update(int screen_width, int screen_height);
extern void mouse64_get_pos(int *x, int *y);
extern int mouse64_get_left(void);
extern int mouse64_get_right(void);

extern void shell64_run(void);
extern int g_pending_app;

/* V0.13.5: 统一输入抽象层 (USB/PS2 归一化) */
#include "hsinput.h"

/* USB HID 鼠标 */
extern int usb64_init(void);
extern void usb64_poll(void);
extern int usb64_mouse_available(void);
extern void usb64_mouse_get(int *x, int *y, int *left, int *right);
/* USB HID 键盘 */
extern int usb64_kbd_available(void);
extern int usb64_kbd_haschar(void);
extern int usb64_kbd_getchar(void);

static int desktop_initialized = 0;
static int screen_width = 0;
static int screen_height = 0;
int hepu_nographics = 0;  /* hepu.nographics: 默认图形模式，要文本改1 */
/* P3: hepu_dev_mode 全局变量已移除，改为 per-process 会话级（hepu_is_dev） */

/* 图形模式初始化（读取bootloader传递的VBE信息） */
int desktop64_init(void)
{
    if (hepu_nographics) {
        vga_puts("[DESKTOP64] nographics=1, skipping GUI\n");
        return -1;
    }
    int bpp;
    gfx64_init_from_bootloader();
    gfx64_get_info(&screen_width, &screen_height, &bpp);

    if (screen_width == 0 || screen_height == 0) {
        vga_puts("[DESKTOP64] No VBE mode, using text mode\n");
        return -1;
    }

    vga_puts("[DESKTOP64] Desktop initialized: ");
    char buf[16];
    int n = screen_width, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j++] = 'x';
    n = screen_height; i = 0;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts("\n");

    desktop_initialized = 1;
    return 0;
}

/* 桌面主循环：Hepu Desktop 事件循环（鼠标/键盘/渲染/点击） */
/* V0.59.25 主循环分段计时(见 [PROF] 的残差拆解) */
uint64_t g_cyc_net, g_cyc_dhcp, g_cyc_input, g_cyc_render_call, g_cyc_loop, g_loop_iters;
static inline uint64_t desktop64_rdtsc(void)
{
    uint32_t a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    return ((uint64_t)d << 32) | a;
}

void desktop64_run(void)
{
    /* 进主循环前把 owner 身份钉住: 此后只有这个上下文能直接改桌面状态 */
    hpdesk_claim_owner();
    if (!desktop_initialized) {
        if (desktop64_init() != 0) {
            vga_puts("[DESKTOP64] Failed to initialize desktop\n");
            return;
        }
    }

    /* Hepu Desktop：启动即初始化 */
    static int hpdesk_started = 0;
    if (!hpdesk_started) {
        hpdesk_init(screen_width, screen_height);
        hpdesk_started = 1;
        vga_puts("[DESKTOP64] Hepu Desktop started as default desktop\n");
    }

#ifdef HEPU_ATA_HILBA_TEST
    /* OPEN-01 取证：在主盘(slave)上做高 LBA 写读回环，通过即关闭该条。 */
    {
        extern int ide64_selftest_high_lba(int drive, uint64_t n_lba, const uint32_t *lbas);
        static const uint32_t lbas[] = { 0u, 0x1000u, 0x20000u, 0x3FFFFu, 0x7F000u };
        ide64_selftest_high_lba(1, (uint64_t)(sizeof(lbas) / sizeof(lbas[0])), lbas);
    }
#endif

#ifdef HEPU_FS_STRESS
    /* SEC-18 大负载写入演练（默认不编译） */
    {
        extern void fs64_stress_run(int iters);
        fs64_stress_run(8);
    }
#endif

#ifdef HEPU_SEC19_PROBE
    /* SEC-19 端到端探测：真正跑一次 /bin/ps.elf —— 它的栈用量超过
     * 4KB，是 SEC-19 的原始复现程序。跑到 = 跨栈页正常；崩 = 会打 #PF
     * 现场。探测完照常进桌面主循环。 */
    {
        extern int user_process_exec(const char *path);
        extern void vga_put_hex(uint64_t);
        vga_puts("[SEC19] probe exec /bin/ps.elf\n");
        int pr = user_process_exec("/bin/ps.elf");
        vga_puts("[SEC19] probe done ret=");
        vga_put_hex((uint64_t)pr);
        vga_puts("\n");
    }
#endif









    /* USB 主机控制器初始化（UHCI + HID 鼠标/键盘） */
    if (usb64_init() == 0) {
        vga_puts("[DESKTOP64] USB HID subsystem ready\n");
    }

    vga_puts("[DESKTOP64] Desktop running with mouse support\n");

    /* V0.13.5: 统一输入层接管全部输入分发 (键盘/鼠标/滚轮, USB优先PS/2兜底) */
    hs_input_init(screen_width, screen_height);

    /* V0.13.8 P5.1: 丢弃 PS/2 键盘初始化噪声(误触 F2-F8 关面板), 主循环前清空 */
    {
        extern void keyboard64_flush(void);
        keyboard64_flush();
        /* USB 键盘队列同样清空(枚举报告噪声) */
        extern void usb64_kbd_flush(void);
        usb64_kbd_flush();
    }

    /* V0.55 P3-GUI: 首次启动 → 桌面内弹出图形化首启向导
     * (图形化不可用时由 kernel_main 走 oobe64_run 文本兜底) */
    {
        extern int auth64_is_configured(void);
        extern void hs_oobe_begin(void);
        extern int hpdesk_skip_oobe(void);
        /* V0.55.2: config.hsqml 的 UI.skip_oobe=1 时跳过欢迎引导 */
        if (!auth64_is_configured() && !hpdesk_skip_oobe()) hs_oobe_begin();
    }

    /* 事件循环
     * V0.56.1 性能修复：渲染限 60fps 并与输入轮询解耦。
     * 旧实现每轮都整屏合成 + gfx64_present() 全屏拷贝(1080p≈8MB)且无帧率上限，
     * 实际跑到数百 fps，把内存带宽/CPU 吃满，输入轮询被饿死 → 实机卡顿。 */
    uint64_t last_render_ms = 0;
    while (1) {
        extern uint64_t pit64_get_ms(void);
        uint64_t now = pit64_get_ms();
        /* V0.59.25: 主循环分段计时 —— [PROF] 里那个 33937/帧的"真残差"一直是个黑盒,
         * 这里把它按轮拆成 网络/DHCP/输入/渲染 四段, 外加整轮总时长。
         * 目的不是精确归因, 而是先排除"某一轮 poll 偶发阻塞"这种可能。 */
        uint64_t __lp0 = desktop64_rdtsc();

        /* V0.13.9: 网络轮询 (网卡收发) */
        extern void net64_poll(void);
        { uint64_t __t = desktop64_rdtsc(); net64_poll(); g_cyc_net += desktop64_rdtsc() - __t; }

        /* V0.13.65: 自动 DHCP 联网 (启动后自动获取 IP, 不依赖 shell 命令) */
        extern int dhcp64_poll(void);
        { uint64_t __t = desktop64_rdtsc(); dhcp64_poll(); g_cyc_dhcp += desktop64_rdtsc() - __t; }

        /* V0.13.9: 天气真实数据拉取 (60 帧≈8s 后一次, 阻塞 ~1-3s)
         * V0.56.1: 原来只试一次, 网络(DHCP/DNS)没就绪就永久空白。改为失败重试,
         * 最多 6 次, 每次间隔 60 帧; 成功后不再请求。 */
        static int wx_tick = 0;
        static int wx_tries = 0;
        /* V0.58.3: 6 次 × 每次最多阻塞数秒 = 启动期累计卡顿近 20 秒。
         * 降到 3 次、间隔 300 帧, 累计阻塞控制在 10 秒内且分散。 */
        /* V0.59.15: 启动期不再自动拉天气。同步 HTTP 会把主循环堵住(每次最多 3 秒,
         * 还带重试), 表现为桌面启动一段时间内时钟与画面一起冻住。
         * 现在推迟到启动 60 秒后只试一次: 首屏完全不受影响, 顶栏天气卡片在这之前
         * 显示 "--" 占位(本来就有占位逻辑)。 */
        if (wx_tries < 1) {
            /* 主循环一轮约 1ms, 60000 轮 ≈ 60 秒墙钟(按轮数算, 不是帧数) */
            if (++wx_tick >= 60000) {
                wx_tick = 0;
                extern int hpdesk_fetch_weather(void);
                if (hpdesk_fetch_weather()) wx_tries = 6;
                else wx_tries++;
            }
        }

        { uint64_t __t = desktop64_rdtsc(); hs_input_poll(); g_cyc_input += desktop64_rdtsc() - __t; }

        /* SB-08 验收: BSP 侧持续浮点负载 —— 与 AP 上 kstat 的探针并发,
         * 两核各自的中断要在各自的 FXSAVE 槽里保存/恢复, 这里的结果是判据。
         * 验收结束后可以删掉(每 20 帧一次, 开销微秒级)。 */
        {
            static int sbt = 0, sbc = 0;
            static uint32_t sb_ok = 0, sb_fail = 0;
            extern int sb08_fpu_probe(void);
            extern void vga_puts(const char *);
            extern void vga_put_hex(uint64_t);
            if (++sbt >= 5) {
                sbt = 0;
                if (sb08_fpu_probe()) sb_ok++; else sb_fail++;
                if (++sbc >= 200) {
                    sbc = 0;
                    vga_puts("[SB08] bsp load ok=");
                    vga_put_hex(sb_ok);
                    vga_puts(" fail=");
                    vga_put_hex(sb_fail);
                    vga_puts("\n");
                }
            }
        }

        /* V0.13.51: 内存压力周期检查 (每~100帧: 低内存时回收块缓存) */
        static int mp_tick = 0;
        if (++mp_tick >= 100) {
            mp_tick = 0;
            extern void mem_pressure_tick(void);
            mem_pressure_tick();
        }

        /* V0.57.0: 数据面板节拍 —— 每 ~500ms 请求一次重绘。
         * 按需合成只在"脏"时重绘, 而 CPU/内存占用、进程数这类数值会自己变,
         * 没有任何交互去标脏 → 面板上的百分比会一直冻在打开那一刻的数字,
         * 看起来就像"占用不显示/不刷新"。这里给一个低频心跳, 代价是每秒两次重绘。 */
        {
            static int data_tick = 0;
            if (++data_tick >= 500) {   /* 主循环一轮约 1ms(PIT 1kHz), 500 轮≈0.5s */
                data_tick = 0;
                hpdesk_mark_dirty();
            }
        }

        /* 渲染：仅每 ~16ms(≈60fps) 一次，输入/网络仍每轮处理保证低延迟 */
        if (now - last_render_ms >= 16) {
            last_render_ms = now;
            uint64_t __t = desktop64_rdtsc();
            hpdesk_deskstate_drain();   /* §5-2: 先消费跨核标脏请求, 再渲染 */
            hpdesk_render();
            g_cyc_render_call += desktop64_rdtsc() - __t;
        }

        /* V0.13.7 P4: Hepu Desktop Dock/启动器图标点击的应用启动请求 */
        /* WM-03: 原子取走并清零。原来是「读 != 0 -> 处理 -> 写 0」三步，
         * 若在这个窗口里另一线程又写入一个新请求，会被后面的 =0 抹掉
         * （典型场景: 一个 tick 内连点两个图标，第二个请求凭空消失）。 */
        {
            int pend = __atomic_exchange_n(&g_pending_app, 0, __ATOMIC_ACQ_REL);
            if (pend != 0) {
                hpdesk_handle_pending_app();
                vga_puts("[DESKTOP64] Pending app request handled by Hepu Desktop\n");
            }
        }

        g_cyc_loop += desktop64_rdtsc() - __lp0;
        g_loop_iters++;
        /* 让出 CPU 等下一次中断(PIT 1kHz 兜底), 避免忙等 100% 占用。
         * V0.58.2: 显式开中断再 hlt —— 若之前走过 cli 路径, 单纯 hlt 会永久挂起。
         * 注意这是防御性加固: 实测主循环仍会在十余轮后停止推进(见 OPEN_BUGS
         * SEC-24), 与 cli 无关, 另案排查。 */
        asm volatile("sti; hlt");
    }
}
