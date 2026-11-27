/* 电池图标状态映射的主机端单测。
 *
 * 为什么需要它：QEMU 夹具只能造出 ABSENT / DECLARED 两种状态——没有 AML 执行器
 * 就进不了 VALID（M2 的事）。如果只靠截屏验收，VALID 的填充分支永远是"没人跑过"
 * 的死代码。状态→图标的映射是纯逻辑，放在这里覆盖全部四种状态。
 *
 * 构建: gcc -I kernel/arch/x86_64 tools/host-tests/battery_ui_test.c
 */
#include <stdio.h>
#include "hpdesk.h"

static int fails;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("  FAIL: %s\n", what);
        fails++;
    }
}

int main(void)
{
    /* 1) 状态 → 外观：只看电池状态，不看台式/笔记本 */
    check(hs_battery_ui_mode(HS_BATTERY_ABSENT) == 0, "ABSENT 不画图标");
    check(hs_battery_ui_mode(HS_BATTERY_DECLARED) == 1, "DECLARED 只画轮廓 + 两道短线");
    check(hs_battery_ui_mode(HS_BATTERY_UNKNOWN) == 1, "UNKNOWN 只画轮廓 + 两道短线");
    check(hs_battery_ui_mode(HS_BATTERY_VALID) == 2, "VALID 才画填充");
    check(hs_battery_ui_mode(99) == 1, "非法状态退回未知形态，不得当成 VALID");

    /* 2) 填充宽度：0% 不填空，100% 填满，非零至少 1px */
    check(hs_battery_fill_px(32, 0) == 0, "0% 不填空");
    check(hs_battery_fill_px(32, 100) == 32, "100% 填满内区");
    check(hs_battery_fill_px(32, 50) == 16, "50% 填一半");
    check(hs_battery_fill_px(18, 1) == 1, "1% 至少 1px，不显示成空电池");
    check(hs_battery_fill_px(18, 150) == 18, ">100% 钳到满");
    check(hs_battery_fill_px(18, -5) == 0, "<0% 钳到空");
    check(hs_battery_fill_px(0, 60) == 0, "内区宽度为 0 时不画");

    /* 3) 单调性：电量越高填充只增不减 */
    int prev = -1, mono = 1;
    for (int p = 0; p <= 100; p++) {
        int w = hs_battery_fill_px(24, p);
        if (w < prev || w > 24) mono = 0;
        prev = w;
    }
    check(mono, "填充宽度随电量单调不减且不超过内区");

    if (fails) {
        printf("FAIL %d 项\n", fails);
        return 1;
    }
    printf("PASS 全部断言\n");
    return 0;
}
