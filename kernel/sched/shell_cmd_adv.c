/* ============================================================================
 * 赫普OS - Shell：高级命令（手势/电源/网络等大处理块）
 * 从 shell_execute.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：cmd_gesture/cmd_pm/cmd_tcp 等大命令处理块
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "process.h"
#include "shell.h"
#include "serial.h"
#include "pit.h"
#include "hepu.h"
#include "i18n.h"
#include "vfs.h"
#include "ext2.h"
#include "ide_ext2.h"
#include "wm.h"
#include "ide.h"
#include "usb.h"
#include "appreg.h"
#include "touch.h"
#include "touch_hw.h"
#include "power.h"
/* V4.36-E81: 手势事件回调——UI 层事件驱动挂钩 (事件即时打印, 非轮询计数) */
static void hepu_ui_gesture_cb(gesture_event_t *ge)
{
    if (!ge) return;
    switch (ge->type) {
        case GESTURE_TAP:         vga_printf("[UI] TAP @(%d,%d)\n", ge->x, ge->y); break;
        case GESTURE_DOUBLE_TAP:  vga_printf("[UI] DOUBLE_TAP @(%d,%d)\n", ge->x, ge->y); break;
        case GESTURE_LONG_PRESS:  vga_printf("[UI] LONG_PRESS @(%d,%d)\n", ge->x, ge->y); break;
        case GESTURE_SWIPE_LEFT:  vga_printf("[UI] SWIPE_LEFT  dx=%d dy=%d\n", ge->dx, ge->dy); break;
        case GESTURE_SWIPE_RIGHT: vga_printf("[UI] SWIPE_RIGHT dx=%d dy=%d\n", ge->dx, ge->dy); break;
        case GESTURE_SWIPE_UP:    vga_printf("[UI] SWIPE_UP    dx=%d dy=%d\n", ge->dx, ge->dy); break;
        case GESTURE_SWIPE_DOWN:  vga_printf("[UI] SWIPE_DOWN  dx=%d dy=%d\n", ge->dx, ge->dy); break;
        case GESTURE_PINCH:       vga_printf("[UI] PINCH @(%d,%d) (fingers closer)\n", ge->x, ge->y); break;
        case GESTURE_ZOOM:        vga_printf("[UI] ZOOM  @(%d,%d) (fingers apart)\n", ge->x, ge->y); break;
        default:                  vga_printf("[UI] gesture type=%d\n", (int)ge->type); break;
    }
}

/* V4.39-E84: 手势类型名 (队列 drain 显示用) */
static const char *gesture_type_name(int t)
{
    switch (t) {
        case GESTURE_TAP:         return "TAP";
        case GESTURE_DOUBLE_TAP:  return "DOUBLE_TAP";
        case GESTURE_LONG_PRESS:  return "LONG_PRESS";
        case GESTURE_SWIPE_LEFT:  return "SWIPE_LEFT";
        case GESTURE_SWIPE_RIGHT: return "SWIPE_RIGHT";
        case GESTURE_SWIPE_UP:    return "SWIPE_UP";
        case GESTURE_SWIPE_DOWN:  return "SWIPE_DOWN";
        case GESTURE_PINCH:       return "PINCH";
        case GESTURE_ZOOM:        return "ZOOM";
        default:                  return "UNKNOWN";
    }
}

/* 命令处理函数 */


void cmd_gesture(char *args)
{
        /* V4.36-E81: 手势事件驱动回调——UI 层挂钩管道 */
        extern int input_set_gesture_callback(gesture_callback_t);
        extern int input_enable_gestures(int);
        extern int touch_sim_inject(int, int, int);
        extern int touch_sim_inject_slot(int, int, int, int);
        if (args && args[0] == 'o' && args[1] == 'n') {
            input_enable_gestures(1);
            input_set_gesture_callback(hepu_ui_gesture_cb);
            vga_puts("[GESTURE] callback registered (event-driven UI hook)\n");
            vga_puts("  -> now run touchtest double/pinch/swipe to see events\n");
        } else if (args && args[0] == 'o' && args[1] == 'f' && args[2] == 'f') {
            input_set_gesture_callback(NULL);
            vga_puts("[GESTURE] callback unregistered\n");
        } else if (args && args[0] == 'u' && args[1] == 'i') {
            /* V4.40-E85: gesture ui <tap|dbl|long|left|right|up|down|pinch|zoom>
             * —— 文本模式验证 wm 手势处理逻辑 (wm_handle_gesture) */
            extern void wm_handle_gesture(gesture_event_t *);
            char *a2 = args + 2;
            while (*a2 == ' ') a2++;
            gesture_event_t ge;
            memset(&ge, 0, sizeof(ge));
            if (a2[0] == 't' && a2[1] == 'a') ge.type = GESTURE_TAP;
            else if (a2[0] == 'd') ge.type = GESTURE_DOUBLE_TAP;
            else if (a2[0] == 'l') ge.type = GESTURE_LONG_PRESS;
            else if (a2[0] == 'l' && a2[1] == 'e') ge.type = GESTURE_SWIPE_LEFT;
            else if (a2[0] == 'r') ge.type = GESTURE_SWIPE_RIGHT;
            else if (a2[0] == 'u') ge.type = GESTURE_SWIPE_UP;
            else if (a2[0] == 'd' && a2[1] == 'o') ge.type = GESTURE_SWIPE_DOWN;
            else if (a2[0] == 'p') ge.type = GESTURE_PINCH;
            else if (a2[0] == 'z') ge.type = GESTURE_ZOOM;
            else { vga_puts("usage: gesture ui <tap|dbl|long|left|right|up|down|pinch|zoom>\n"); return; }
            ge.x = 160; ge.y = 120; ge.dx = 300; ge.dy = 0;
            vga_puts("[GESTURE-UI] feeding event to wm_handle_gesture...\n");
            wm_handle_gesture(&ge);
        } else if (args && args[0] == 'q') {
            /* V4.39-E84: gesture q [drain|clear] —— 事件队列 (UI 轮询源) */
            extern int input_gesture_poll(gesture_event_t *);
            extern void input_gesture_queue_clear(void);
            extern void input_gesture_queue_stats(int *, int *);
            extern const char *gesture_type_name(int);
            char *a2 = args + 1;
            while (*a2 == ' ') a2++;
            if (*a2 == 'd') {
                gesture_event_t ge;
                int n = 0;
                while (input_gesture_poll(&ge)) {
                    vga_printf("  [Q] %s @(%d,%d) dx=%d dy=%d\n",
                               gesture_type_name(ge.type), ge.x, ge.y, ge.dx, ge.dy);
                    n++;
                }
                vga_printf("  drained %d events\n", n);
            } else if (*a2 == 'c') {
                input_gesture_queue_clear();
                vga_puts("[Q] queue cleared\n");
            } else {
                int pending = 0, dropped = 0;
                input_gesture_queue_stats(&pending, &dropped);
                vga_printf(" gesture queue: pending=%d dropped=%d (cap 16)\n", pending, dropped);
            }
        } else if (args && args[0] == 't' && args[1] == 'h') {
            /* V4.38-E83: gesture threshold [lp] [dt] [sw] */
            extern int input_gesture_config(int, int, int);
            extern void input_gesture_cfg_get(int *, int *, int *);
            int lp = 0, dt = 0, sw = 0;
            if (args[1] == 'h' && args[2] != 0) {
                /* V4.38-E83: args="threshold 80 25 70" —— 跳过 "threshold" 词到首个数字 */
                char *p2 = args;
                while (*p2 && *p2 != ' ') p2++;
                while (*p2 == ' ') p2++;
                if (*p2) {
                    int n = 0;
                    char *q = p2;
                    while (*q && *q != ' ') { n = n * 10 + (*q - '0'); q++; }
                    lp = n;
                    while (*q == ' ') q++;
                    n = 0;
                    if (*q) { while (*q && *q != ' ') { n = n * 10 + (*q - '0'); q++; } dt = n; }
                    while (*q == ' ') q++;
                    n = 0;
                    if (*q) { while (*q && *q != ' ') { n = n * 10 + (*q - '0'); q++; } sw = n; }
                    input_gesture_config(lp, dt, sw);
                }
            }
            input_gesture_cfg_get(&lp, &dt, &sw);
            vga_printf(" gesture cfg: long_press=%d ticks, dbl_window=%d ticks, swipe_min=%d\n",
                       lp, dt, sw);
        } else if (args && args[0] == 't') {
            input_enable_gestures(1);
            input_set_gesture_callback(hepu_ui_gesture_cb);
            vga_puts("[GESTURE] self-test: injecting tap/swipe/pinch...\n");
            touch_sim_inject(160, 120, 1);
            touch_sim_inject(160, 120, 0);
            touch_sim_inject(100, 100, 1);
            touch_sim_inject(500, 100, 2);
            touch_sim_inject(500, 100, 0);
            touch_sim_inject_slot(0, 200, 200, 1);
            touch_sim_inject_slot(1, 300, 200, 1);
            touch_sim_inject_slot(1, 400, 200, 1);
            touch_sim_inject_slot(0, -1, 0, 0);
            touch_sim_inject_slot(1, -1, 0, 0);
            vga_puts("[GESTURE] injected 3 gestures (tap/swipe/pinch)\n");
        } else {
            vga_puts("usage: gesture on|off|test\n");
}
}
void cmd_pm(char *args)
{
        /* V2.60: 电源管理——状态/电池/休眠唤醒/省电超时 */
        extern void serial_puts(unsigned short, const char *);
        serial_puts(0x3F8, "[PM] cmd: ");
        extern void pm_init(void);
        extern int pm_set_state(int);
        extern int pm_get_state(void);
        extern void pm_wakeup(void);
        extern void pm_sleep(int);
        extern int pm_get_battery(void *);
        extern void pm_update_battery(void *);
        extern void pm_set_sleep_timeout(int);
        extern int pm_get_sleep_timeout(void);
        pm_init();
        if (args && args[0] == 's' && args[1] == 'l') {
            pm_sleep(3);   /* PM_STATE_SUSPEND */
            serial_puts(0x3F8, "sleep\n");
            vga_puts(" suspend requested\n");
        } else if (args && args[0] == 'w') {
            pm_wakeup();
            serial_puts(0x3F8, "wake\n");
            vga_puts(" wakeup requested\n");
        } else if (args && args[0] == 'i') {
            pm_set_state(1);   /* PM_STATE_IDLE */
            vga_puts(" idle requested\n");
        } else if (args && args[0] == 't') {
            /* pm tmo <sec> */
            int t = 0;
            if (args[3] >= '0' && args[3] <= '9') { t = args[3] - '0'; }
            if (args[4] >= '0' && args[4] <= '9') { t = t * 10 + args[4] - '0'; }
            if (t > 0) { pm_set_sleep_timeout(t); }
            serial_puts(0x3F8, "tmo=");
            /* 简易十进制输出 */
            {
                char nb[8]; int ni=0, nv=t;
                if (nv==0) nb[ni++]='0';
                while (nv>0 && ni<7) { nb[ni++]='0'+(nv%10); nv/=10; }
                while (ni>0) { serial_puts(0x3F8, (char[2]){nb[--ni], '\0'}); }
                serial_puts(0x3F8, "\n");
            }
            vga_printf(" sleep timeout: %d s\n", pm_get_sleep_timeout());
        } else if (args && args[0] == 'b') {
            /* pm batt <pct>: 模拟电池电量 (V2.54 修复: 完整多位数解析) */
            int pct = 0;
            const char *q = args + 5;   /* "batt 12" -> args[5]='1' */
            while (*q >= '0' && *q <= '9') {
                pct = pct * 10 + (*q - '0');
                q++;
            }
            if (pct == 0) pct = 85;     /* 无数字则默认 85% */
            extern void pm_batt_set(int);
            pm_batt_set(pct);
            vga_printf(" battery set to %d%%\n", pct);
        } else if (args && args[0] == 'l') {
            /* V4.49-E94: pm level <0|1|2> —— 省电等级 (性能/平衡/省电) */
            extern int pm_set_level(int);
            extern int pm_get_level(void);
            int lvl = -1;
            const char *q = args + 6;   /* "level 2" -> args[6]='2' */
            while (*q >= '0' && *q <= '9') { lvl = *q - '0'; q++; }
            if (lvl >= 0 && lvl <= 2) {
                pm_set_level(lvl);
            } else {
                vga_printf(" power level: %d\n", pm_get_level());
                vga_puts(" usage: pm level <0|1|2>  (0=perf 1=balanced 2=saver)\n");
            }
        } else if (args && args[0] == 's') {
            /* pm save [on|off]: 省电模式 (V3.79) */
            extern void pm_set_low_power(int);
            extern int pm_is_low_power(void);
            int on = -1;
            if (args[5] == 'o' && args[6] == 'n') on = 1;
            else if (args[5] == 'o' && args[6] == 'f' && args[7] == 'f') on = 0;
            if (on >= 0) pm_set_low_power(on);
            vga_printf(" low power mode: %s\n", pm_is_low_power() ? "ON" : "OFF");
        } else {
            int st = pm_get_state();
            const char *sn = (st == 0) ? "ON" : (st == 1) ? "IDLE" :
                             (st == 2) ? "STANDBY" : (st == 3) ? "SUSPEND" :
                             (st == 4) ? "HIBERNATE" : "OFF";
            /* V2.54: 显示电池电量 (验证 pm_tick 放电) */
            extern int pm_get_battery(void *);
            typedef struct { int present, charging, capacity, voltage, current,
                             temperature, design_capacity, full_capacity, remaining_capacity; } pmb;
            pmb b; pm_get_battery(&b);
            vga_printf(" PM state: %s | battery %d%% %s | tmo %d s\n",
                       sn, b.capacity, b.charging ? "CHG" : "DIS",
                       pm_get_sleep_timeout());
            extern int pm_is_low_power(void);
            vga_printf(" low power: %s\n", pm_is_low_power() ? "ON" : "OFF");
            /* V4.04-E49: 深度睡眠统计 */
            extern void pm_get_wake_stats(void *);
            typedef struct { uint32_t sleep_count, wake_count, total_sleep_ticks,
                             last_sleep_ticks; int last_wake_reason; } pws;
            pws ws; pm_get_wake_stats(&ws);
            vga_printf(" sleeps=%u wakes=%u total=%u.%02us last=%u.%02us\n",
                       ws.sleep_count, ws.wake_count,
                       ws.total_sleep_ticks / 100, ws.total_sleep_ticks % 100,
                       ws.last_sleep_ticks / 100, ws.last_sleep_ticks % 100);
            /* V4.49-E94: 省电等级 + 电池续航估算 */
            extern int pm_get_level(void);
            extern int pm_battery_minutes(void);
            int lvl = pm_get_level();
            int mins = pm_battery_minutes();
            vga_printf(" level: %d (%s) | battery est: %d min\n",
                       lvl, lvl == 0 ? "perf" : lvl == 1 ? "balanced" : "saver",
                       mins);
            vga_puts(" usage: pm | pm sleep | pm wake | pm idle | pm tmo <sec> | pm batt <pct> | pm save <on|off> | pm level <0|1|2>\n");
        }
}
void cmd_tcp(char *args)
{
        /* V4.50-E95: TCP 状态机演示——connect/listen/accept/send/recv/close/list */
        extern int tcp_connect(unsigned int, unsigned short, unsigned short);
        extern int tcp_listen(unsigned short, int);
        extern int tcp_accept(int, unsigned int *, unsigned short *);
        extern int tcp_send(int, const unsigned char *, unsigned int);
        extern int tcp_receive(int, unsigned char *, unsigned int);
        extern int tcp_close(int);
        extern int tcp_get_state(int, int *);
        extern const char *hepu_tcp_state_text(int);
        extern int tcp_get_connection_info(int, void *);
        extern void tcp_tick(void);
        typedef struct { int state; unsigned int local_ip; unsigned short local_port;
                         unsigned int remote_ip; unsigned short remote_port;
                         unsigned int seq_num, ack_num; unsigned short window;
                         int retries; unsigned int last_activity;
                         unsigned char *send_buffer; unsigned int send_buffer_size, send_buffer_len;
                         unsigned char *recv_buffer; unsigned int recv_buffer_size, recv_buffer_len;
                         int in_use; } tcpci;
        if (args && args[0] == 'c' && args[1] == 'o') {
            /* tcp connect <a> <b> <c> <d> <port> (空格分隔, 避开 sendkey 不能发 .) */
            const char *q = args + 8;
            int nums[5] = {0, 0, 0, 0, 0};
            int ni = 0; int v = 0; int indigit = 0;
            for (const char *p = q; *p && ni < 5; p++) {
                if (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); indigit = 1; }
                else if (*p == ' ' || *p == '.' || *p == '\r' || *p == '\n') {
                    if (indigit) { nums[ni++] = v; v = 0; indigit = 0; }
                }
            }
            if (indigit && ni < 5) nums[ni++] = v;
            unsigned int ip = ((unsigned int)nums[0] << 24) | ((unsigned int)nums[1] << 16) |
                              ((unsigned int)nums[2] << 8) | (unsigned int)nums[3];
            int port = nums[4] ? nums[4] : 80;
            int fd = tcp_connect(ip, (unsigned short)port, 0);
            if (fd >= 0) vga_printf(" fd=%d (SYN_SENT)\n", fd);
        } else if (args && args[0] == 'l') {
            /* tcp listen <port> */
            int port = 0; const char *r = args + 11;
            while (*r >= '0' && *r <= '9') { port = port * 10 + (*r - '0'); r++; }
            int fd = tcp_listen((unsigned short)port, 4);
            if (fd >= 0) vga_printf(" fd=%d (LISTEN)\n", fd);
        } else if (args && args[0] == 'a') {
            /* tcp accept <listen_fd> */
            int lfd = 0; const char *r = args + 7;
            while (*r >= '0' && *r <= '9') { lfd = lfd * 10 + (*r - '0'); r++; }
            unsigned int cip = 0; unsigned short cport = 0;
            int fd = tcp_accept(lfd, &cip, &cport);
            if (fd >= 0) vga_printf(" fd=%d (SYN_RECEIVED) client=%d.%d.%d.%d:%d\n",
                                    fd, (cip >> 24) & 0xFF, (cip >> 16) & 0xFF,
                                    (cip >> 8) & 0xFF, cip & 0xFF, cport);
        } else if (args && args[0] == 's' && args[1] == 'e') {
            /* tcp send <fd> <text...> */
            int fd = 0; const char *r = args + 9;
            while (*r >= '0' && *r <= '9') { fd = fd * 10 + (*r - '0'); r++; }
            while (*r == ' ') r++;
            int n = 0; while (r[n] && r[n] != '\r' && n < 200) n++;
            int ret = tcp_send(fd, (const unsigned char *)r, (unsigned int)n);
            vga_printf(" send -> %d bytes\n", ret);
        } else if (args && args[0] == 'r') {
            /* tcp recv <fd> */
            int fd = 0; const char *r = args + 9;
            while (*r >= '0' && *r <= '9') { fd = fd * 10 + (*r - '0'); r++; }
            unsigned char buf[128];
            int n = tcp_receive(fd, buf, 120);
            if (n > 0) { buf[n] = 0; vga_printf(" recv %d bytes: %s\n", n, (char *)buf); }
            else vga_puts(" recv: no data\n");
        } else if (args && args[0] == 'c' && args[1] == 'l') {
            /* tcp close <fd> */
            int fd = 0; const char *r = args + 10;
            while (*r >= '0' && *r <= '9') { fd = fd * 10 + (*r - '0'); r++; }
            tcp_close(fd);
            vga_printf(" close(fd=%d)\n", fd);
        } else if (args && args[0] == 't') {
            /* tcp tick: 手动推进状态机 */
            tcp_tick();
            vga_puts(" tick ok\n");
        } else {
            /* tcp: 列出全部连接 */
            int found = 0;
            for (int i = 0; i < 32; i++) {
                tcpci ci;
                if (tcp_get_connection_info(i, &ci) != 0) continue;
                if (!ci.in_use) continue;
                found++;
                vga_printf(" fd=%d %s %d.%d.%d.%d:%d seq=%u ack=%u win=%u unacked=%u\n",
                           i, hepu_tcp_state_text(ci.state),
                           (ci.remote_ip >> 24) & 0xFF, (ci.remote_ip >> 16) & 0xFF,
                           (ci.remote_ip >> 8) & 0xFF, ci.remote_ip & 0xFF,
                           ci.remote_port, ci.seq_num, ci.ack_num,
                           ci.window, ci.seq_num - ci.ack_num);
            }
            if (!found) vga_puts(" no tcp connections\n");
            vga_puts(" usage: tcp connect <ip> <port> | tcp listen <port> | tcp accept <fd> | tcp send <fd> <text> | tcp recv <fd> | tcp close <fd> | tcp tick\n");
        }
}
void cmd_touchtest(char *args)
{
        /* V2.50: 触屏驱动自检——注入模拟触点, 验证 input 链路 */
        extern int touch_sim_inject(int, int, int);
        extern int touch_sim_inject_slot(int, int, int, int);
        extern touch_hw_state_t *touch_hw_get_state(void);
        extern int input_enable_gestures(int);
        input_enable_gestures(1);
        if (args && args[0] == 'p') {
            /* touchtest pinch: 双指分离 -> ZOOM */
            input_enable_gestures(1);
            vga_puts("PINCH selftest\n");
            touch_sim_inject_slot(0, 200, 200, 1);
            touch_sim_inject_slot(1, 300, 200, 1);
            vga_puts(" two fingers down (200,200)+(300,200)\n");
            touch_sim_inject_slot(1, 400, 200, 1);   /* 第二指外移 */
            touch_sim_inject_slot(0, -1, 0, 0);
            touch_sim_inject_slot(1, -1, 0, 0);
            vga_puts(" fingers up -> ZOOM (distance grew)\n");
        } else if (args && args[0] == 'l') {
            /* touchtest long: 按下 500ms+ -> LONG_PRESS */
            input_enable_gestures(1);
            vga_puts("LONG-PRESS selftest\n");
            touch_sim_inject(240, 160, 1);
            extern uint32_t pit_get_ticks(void);
            uint32_t t0 = pit_get_ticks();
            while ((pit_get_ticks() - t0) < 60) { }   /* 按住 ~600ms */
            touch_sim_inject(240, 160, 0);
            vga_puts(" hold 600ms then up -> LONG_PRESS\n");
        } else if (args && args[0] == 'd') {
            /* touchtest double: 两次快速 tap -> DOUBLE_TAP */
            input_enable_gestures(1);
            vga_puts("DOUBLE-TAP selftest\n");
            touch_sim_inject(160, 120, 1);
            touch_sim_inject(160, 120, 0);
            touch_sim_inject(160, 120, 1);
            touch_sim_inject(160, 120, 0);
            vga_puts(" two taps -> DOUBLE_TAP\n");
        } else if (args && args[0] == 's' && args[1] == 'w') {
            vga_puts("SWIPE selftest\n");
            touch_sim_inject(100, 100, 1);
            touch_sim_inject(500, 100, 2);  /* 保持按下移动 */
            touch_sim_inject(500, 100, 0);
            vga_puts(" swipe (100,100)->(500,100) -> SWIPE_RIGHT\n");
        } else if (args && args[0] == 's' && args[1] == 't') {
            /* touchtest stats: 手势识别计数 */
            extern uint32_t input_gesture_count(int);
            extern int input_enable_gestures(int);
            input_enable_gestures(1);
            const char *gn[10] = {"", "TAP", "LONG_PRESS", "SWIPE_LEFT",
                                  "SWIPE_RIGHT", "SWIPE_UP", "SWIPE_DOWN",
                                  "PINCH", "ZOOM", "DOUBLE_TAP"};
            vga_puts("GESTURE stats (since boot):\n");
            for (int i = 1; i <= 9; i++) {
                uint32_t c = input_gesture_count(i);
                if (c > 0) vga_printf("  %-11s %u\n", gn[i], c);
            }
        } else {
            vga_puts("TOUCH selftest\n");
            touch_sim_inject(320, 100, 1);
            vga_puts(" tap down at (320,100)\n");
            touch_sim_inject(320, 100, 0);
            vga_puts(" tap up -> TAP\n");
            for (int i = 0; i < 8; i++)
                touch_sim_inject(300 - i * 25, 200, 1);
            touch_sim_inject(0, 200, 0);
            vga_puts(" swipe injected -> SWIPE_LEFT\n");
            touch_hw_state_t *st = touch_hw_get_state();
            vga_printf(" touch dev=%d if=%d pts=%d\n",
                       st->dev_id, (int)st->cfg.if_type, st->points);
        }
}
