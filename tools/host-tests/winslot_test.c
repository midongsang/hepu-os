/* ============================================================================
 * 赫普OS 主机端测试 - HepuShell 窗口槽位分配/回收不变量
 *
 * 背景: hepushell.c 旧实现用 window_count 线性追加窗口槽, 关闭只置 visible=0,
 * 不回收槽位也不从 z_order 移除, 反复打开/关闭 32 次(HS_MAX_WINDOWS)后
 * hs_window_create 恒返回 NULL, 系统再也打不开任何窗口。
 *
 * 本测试为自包含纯逻辑回归: 下列 win_create / win_recycle 的核心算法
 * 必须与 kernel/arch/x86_64/hepushell.c 的 hs_window_create / hs_window_recycle
 * 保持逐行一致(图形/标题等无关字段已省略)。
 * 编译: gcc -fsanitize=address,undefined winslot_test.c
 * ========================================================================== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HS_MAX_WINDOWS 32

/* 与 hepushell.h hs_window_t 中参与分配判定的字段对应 */
typedef struct {
    int id;        /* -1=空闲槽; >=0=已占用(即使 visible=0: 最小化/布局隐藏) */
    int visible;
    int focus;
} test_win_t;

typedef struct {
    int window_count;                 /* 高水位: 最大已用槽位下标+1 */
    test_win_t windows[HS_MAX_WINDOWS];
    int focus_window;
    int z_order[HS_MAX_WINDOWS];
    int z_count;
} test_wm_t;

static test_wm_t wm;

static void wm_init(void)
{
    wm.window_count = 0;
    wm.focus_window = -1;
    wm.z_count = 0;
    for (int i = 0; i < HS_MAX_WINDOWS; i++) {
        wm.windows[i].visible = 0;
        wm.windows[i].id = -1;       /* 对应 hs_window_manager_init 的空闲标记 */
        wm.z_order[i] = -1;
    }
}

/* 复刻 hs_window_create 的槽位分配核心(省略图形/标题/动画) */
static test_win_t *win_create(void)
{
    int slot = -1, active = 0;
    for (int i = 0; i < HS_MAX_WINDOWS; i++) {
        if (wm.windows[i].id >= 0) {
            if (wm.windows[i].visible) active++;
        } else if (slot < 0) {
            slot = i;
        }
    }
    if (active >= HS_MAX_WINDOWS || slot < 0) return NULL;

    test_win_t *win = &wm.windows[slot];
    win->id = slot;
    win->visible = 1;

    if (slot + 1 > wm.window_count) wm.window_count = slot + 1;
    wm.z_order[wm.z_count++] = win->id;

    for (int i = 0; i < wm.window_count; i++) wm.windows[i].focus = 0;
    win->focus = 1;
    wm.focus_window = win->id;
    return win;
}

/* 复刻 hs_window_recycle */
static void win_recycle(int id)
{
    if (id < 0 || id >= HS_MAX_WINDOWS) return;
    test_win_t *w = &wm.windows[id];
    if (w->id < 0) return;
    w->visible = 0;
    w->id = -1;
    int new_z = 0;
    for (int i = 0; i < wm.z_count; i++) {
        if (wm.z_order[i] != id) wm.z_order[new_z++] = wm.z_order[i];
    }
    wm.z_count = new_z;
    if (wm.focus_window == id) wm.focus_window = -1;
}

/* 最小化: visible=0 但保留 id(对应 hs_window_minimize), 不回收 */
static void win_minimize(int id)
{
    if (id < 0 || id >= HS_MAX_WINDOWS) return;
    wm.windows[id].visible = 0;
    wm.windows[id].focus = 0;
    if (wm.focus_window == id) wm.focus_window = -1;
}

/* 校验 z_order 不变量: 无重复 id, 每个 id 都指向占用且可见的槽 */
static int z_order_valid(void)
{
    if (wm.z_count < 0 || wm.z_count > HS_MAX_WINDOWS) return 0;
    for (int i = 0; i < wm.z_count; i++) {
        int id = wm.z_order[i];
        if (id < 0 || id >= HS_MAX_WINDOWS) return 0;
        if (wm.windows[id].id != id || !wm.windows[id].visible) return 0;
        for (int j = i + 1; j < wm.z_count; j++)
            if (wm.z_order[j] == id) return 0;   /* 重复 id */
    }
    return 1;
}

static int active_count(void)
{
    int n = 0;
    for (int i = 0; i < HS_MAX_WINDOWS; i++)
        if (wm.windows[i].id >= 0 && wm.windows[i].visible) n++;
    return n;
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); return 1; } \
} while (0)

int main(void)
{
    /* T1: 反复 创建->关闭 远超 32 次, 始终可开窗, 不泄漏槽位 */
    wm_init();
    for (int round = 0; round < 200; round++) {
        test_win_t *w = win_create();
        CHECK(w != NULL, "T1 反复开关过程中创建窗口失败(槽位泄漏复现)");
        CHECK(z_order_valid(), "T1 z_order 不变量被破坏");
        win_recycle(w->id);
    }
    CHECK(wm.z_count == 0, "T1 结束后 z_order 应清空");
    CHECK(active_count() == 0, "T1 结束后应无活跃窗口");
    for (int i = 0; i < HS_MAX_WINDOWS; i++)
        CHECK(wm.windows[i].id == -1, "T1 所有槽位应已回收为空闲");

    /* T2: 真正占满 32 个时第 33 个应失败; 关闭一个后可再创建 */
    wm_init();
    for (int i = 0; i < HS_MAX_WINDOWS; i++)
        CHECK(win_create() != NULL, "T2 占满过程中创建失败");
    CHECK(win_create() == NULL, "T2 活跃窗口满时应返回 NULL");
    CHECK(active_count() == HS_MAX_WINDOWS, "T2 活跃窗口数应为 32");
    win_recycle(7);
    CHECK(active_count() == HS_MAX_WINDOWS - 1, "T2 回收后活跃数应为 31");
    test_win_t *reused = win_create();
    CHECK(reused != NULL, "T2 回收一个槽位后应能再创建");
    CHECK(reused->id == 7, "T2 应复用被释放的 7 号槽位");
    CHECK(z_order_valid(), "T2 z_order 不变量被破坏");

    /* T3: 最小化(visible=0 保留 id)不应被当作空闲槽, 且不进入活跃计数 */
    wm_init();
    test_win_t *a = win_create();
    test_win_t *b = win_create();
    int id_a = a->id;
    win_minimize(id_a);
    CHECK(wm.windows[id_a].id == id_a, "T3 最小化后槽位仍应被占用(id 保留)");
    CHECK(wm.windows[id_a].visible == 0, "T3 最小化后 visible 应为 0");
    /* 此时空闲槽不应包含 id_a: 下一个新窗口不能复用 id_a */
    test_win_t *c = win_create();
    CHECK(c->id != id_a, "T3 最小化窗口的槽位不应被新窗口抢占");
    CHECK(active_count() == 2, "T3 活跃窗口应为 b 与 c(最小化的 a 不计)");
    /* 恢复最小化窗口 */
    wm.windows[id_a].visible = 1;
    CHECK(active_count() == 3, "T3 恢复后活跃窗口应为 3");
    CHECK(z_order_valid(), "T3 z_order 不变量被破坏");

    /* T4: 交错创建/关闭, 活跃数始终 <=32, 长期运行无泄漏 */
    wm_init();
    for (int step = 0; step < 500; step++) {
        if (win_create() == NULL) { CHECK(active_count() == HS_MAX_WINDOWS,
            "T4 创建失败只应发生在真正占满时"); }
        CHECK(active_count() <= HS_MAX_WINDOWS, "T4 活跃窗口数超过上限");
        CHECK(z_order_valid(), "T4 z_order 不变量被破坏");
        if (active_count() > 0 && (step % 2 == 0)) {
            int id = wm.z_order[0];   /* 关闭最底层窗口 */
            win_recycle(id);
        }
    }

    printf("ALL PASS: 窗口槽位分配/回收不变量全部成立 (T1-T4)\n");
    return 0;
}
