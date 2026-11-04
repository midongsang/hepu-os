/* ============================================================================
 * 赫普OS - 系统新手引导
 * ========================================================================== */

#include <stdint.h>
#include "onboarding.h"
#include "vga.h"
#include "string.h"

static onboarding_t ob;

void onboarding_init(void)
{
    memset(&ob, 0, sizeof(onboarding_t));
    ob.initialized = 1;
    ob.enabled = 1;
    ob.state = OB_STATE_NOT_STARTED;
    ob.config.enabled = 1;
    ob.config.auto_start_on_first_boot = 1;
    ob.config.show_tips = 1;
    ob.config.show_migration_guide = 1;
    ob.config.minimal_mode = 0;
    ob.config.animation_enabled = 1;

    /* 默认引导步骤 */
    const char *steps[][3] = {
        {" 欢迎 used Hepu OS", "HEPU Hyper Evolve OS, own your system , own your data ", " used suggest done 引导 "},
        {" data migrate ", " slave 安卓 /iOS one-tap migrate contacts , photos , app ", " supports no 线 & has 线 migrate "},
        {" 隐私 set ", " Hepu OS default empty perm , device ID randomizer ", " 你 data 只属于你 "},
        {" 手势 ops ", " 底部 滑回 master pages , 左右滑返回 , side 边悬停多任务 ", " 可 at set in 自定义 "},
        {" elastic 桌面 ", " map 标可自由摆放缩放 , 摒弃固定网 bars ", " long 按 enter 编辑 mode "},
        {".hug native app ", " Hepu 专属 app format , 轻量 , security , no 追踪 ", " app store download .hug app "},
        {" creator mode ", " no 需 ROOT, 一键 on system level 修改 , 快照 rollback not 变砖 ", " advanced user 推荐 "},
        {" multi-user private space ", " owner / 访客 / 工作 / private space , app 双开 ", " protected 人隐私 "},
        {"AI 智能体验 ", "AI notif classify , 智能资源 sched , local 推理 ", "AI run at 独立沙盒 "},
        {" start used ", " Hepu OS 准备 ready , 享受自由纯净 move 体验 ", " 随 runtime 可 at set in re- 查看引导 "},
    };
    ob_type_t types[] = {OB_TYPE_WELCOME, OB_TYPE_MIGRATION, OB_TYPE_PRIVACY,
        OB_TYPE_GESTURE, OB_TYPE_DESKTOP, OB_TYPE_HUG_FORMAT,
        OB_TYPE_CREATOR_MODE, OB_TYPE_MULTI_USER, OB_TYPE_AI_FEATURES, OB_TYPE_COMPLETE};

    for (int i = 0; i < 10; i++) {
        onboarding_add_step(types[i], steps[i][0], steps[i][1], steps[i][2]);
    }

    /* 默认使用技巧 */
    const char *tips[][3] = {
        {" 双击亮屏 ", " set in on 双击亮屏 , no 需按电源键 ", "0"},
        {" 三指截屏 ", " 三指下滑快速截屏 ", "0"},
        {" app 双开 ", " set - private space in on app 双开 ", "3"},
        {" background freeze ", " not 常用 app auto freeze , 省电省 memory ", "2"},
        {" 创变 mode ", " set - system in on creator mode ", "4"},
        {" notify 收纳 ", "AI auto 将推广 notify 静默收纳 ", "2"},
        {" private space ", " not 同密码 enter not 同 empty 间 , protected 隐私 ", "3"},
        {" elastic 桌面 ", " long 按 map 标可自由拖动缩放 ", "4"},
        {" 广告 ID reset ", " set - 隐私 in 可一键 reset 广告标识 ", "3"},
        {" lowmem throttle ", " 老 device auto on 节 streams mode , streams 畅 run ", "1"},
    };
    for (int i = 0; i < 10; i++) {
        onboarding_add_tip(tips[i][0], tips[i][1], (tips[i][2][0]-'0'));
    }

    /* 迁移对比 */
    const char *migration[][3] = {
        {" 安卓 : long 按 app - app info ", " Hepu : long 按 app - 详情 , perm 一 entries 了然 ", " base "},
        {"iOS: ctrl in 心 slave 右 角下滑 ", " Hepu : 顶部下滑 open ctrl in 心 ,20 quick toggles ", " base "},
        {" 安卓 : 返回键 / 手势 ", " Hepu : 左右边缘滑动返回 , 底部 滑 master pages ", " 手势 "},
        {" 安卓 : 多任务键 ", " Hepu : 底部 滑悬停 enter 多任务 ", " 手势 "},
        {"iOS：App Store", " Hepu : app store ,.hug native app +APK compat ", " app "},
        {" 安卓 : set - 关于手机 ", " Hepu : set - 关于 , kernel 来源 markers 清晰可见 ", " system "},
    };
    for (int i = 0; i < 6; i++) {
        onboarding_add_migration(migration[i][0], migration[i][1], migration[i][2]);
    }

    vga_puts("[ONBOARD] system onboarding init done \n");
    vga_printf(" onboard steps: %d, tips: %d, migration compare: %d\n",
               ob.step_count, ob.tip_count, ob.migration_count);
}

int onboarding_start(void)
{
    if (ob.state == OB_STATE_IN_PROGRESS) return 0;
    ob.state = OB_STATE_IN_PROGRESS;
    ob.current_step = 0;
    ob.completed_steps = 0;
    ob.total_starts++;
    return 0;
}

int onboarding_next(void)
{
    if (ob.state != OB_STATE_IN_PROGRESS) return -1;
    if (ob.current_step < ob.step_count - 1) {
        ob.current_step++;
        return 0;
    }
    /* 最后一步，完成引导 */
    ob.state = OB_STATE_COMPLETED;
    ob.total_completions++;
    return 1;
}

int onboarding_prev(void)
{
    if (ob.state != OB_STATE_IN_PROGRESS) return -1;
    if (ob.current_step > 0) {
        ob.current_step--;
        return 0;
    }
    return -1;
}

int onboarding_skip(void)
{
    ob.state = OB_STATE_SKIPPED;
    ob.total_skips++;
    return 0;
}

int onboarding_complete_step(int step_id)
{
    if (step_id < 0 || step_id >= OB_MAX_STEPS || !ob.steps[step_id].used) return -1;
    if (!ob.steps[step_id].completed) {
        ob.steps[step_id].completed = 1;
        ob.completed_steps++;
    }
    return 0;
}

int onboarding_get_current(ob_step_t *step)
{
    if (!step || ob.state != OB_STATE_IN_PROGRESS) return -1;
    if (ob.current_step < 0 || ob.current_step >= ob.step_count) return -1;
    memcpy(step, &ob.steps[ob.current_step], sizeof(ob_step_t));
    return 0;
}

int onboarding_get_progress(int *current, int *total)
{
    if (current) *current = ob.current_step + 1;
    if (total) *total = ob.step_count;
    return 0;
}

int onboarding_get_state(ob_state_t *state)
{
    if (!state) return -1;
    *state = ob.state;
    return 0;
}

int onboarding_reset(void)
{
    ob.state = OB_STATE_NOT_STARTED;
    ob.current_step = 0;
    ob.completed_steps = 0;
    for (int i = 0; i < OB_MAX_STEPS; i++)
        if (ob.steps[i].used) ob.steps[i].completed = 0;
    return 0;
}

int onboarding_add_step(ob_type_t type, const char *title, const char *desc, const char *tip)
{
    for (int i = 0; i < OB_MAX_STEPS; i++) {
        if (!ob.steps[i].used) {
            memset(&ob.steps[i], 0, sizeof(ob_step_t));
            ob.steps[i].used = 1;
            ob.steps[i].id = i;
            ob.steps[i].type = type;
            if (title) strncpy(ob.steps[i].title, title, OB_TITLE_LEN - 1);
            if (desc) strncpy(ob.steps[i].description, desc, OB_TEXT_LEN - 1);
            if (tip) strncpy(ob.steps[i].tip, tip, OB_TEXT_LEN - 1);
            ob.steps[i].required = (type == OB_TYPE_PRIVACY || type == OB_TYPE_WELCOME);
            ob.steps[i].skippable = (type != OB_TYPE_PRIVACY);
            ob.steps[i].duration_ms = 5000;
            ob.step_count++;
            return i;
        }
    }
    return -1;
}

int onboarding_list_steps(ob_step_t *steps, int max)
{
    if (!steps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < OB_MAX_STEPS && count < max; i++) {
        if (ob.steps[i].used) memcpy(&steps[count++], &ob.steps[i], sizeof(ob_step_t));
    }
    return count;
}

int onboarding_get_step(int id, ob_step_t *step)
{
    if (id < 0 || id >= OB_MAX_STEPS || !ob.steps[id].used || !step) return -1;
    memcpy(step, &ob.steps[id], sizeof(ob_step_t));
    return 0;
}

int onboarding_add_tip(const char *title, const char *content, int category)
{
    for (int i = 0; i < OB_MAX_TIPS; i++) {
        if (!ob.tips[i].used) {
            memset(&ob.tips[i], 0, sizeof(ob_tip_t));
            ob.tips[i].used = 1;
            ob.tips[i].id = i;
            if (title) strncpy(ob.tips[i].title, title, OB_TITLE_LEN - 1);
            if (content) strncpy(ob.tips[i].content, content, OB_TEXT_LEN - 1);
            ob.tips[i].category_id = category;
            const char *cats[] = {" base ", " 进 orders ", " 效率 ", " 隐私 ", " 自定义 "};
            if (category >= 0 && category < 5)
                strncpy(ob.tips[i].category, cats[category], 31);
            ob.tip_count++;
            return i;
        }
    }
    return -1;
}

int onboarding_get_random_tip(ob_tip_t *tip)
{
    if (!tip || ob.tip_count == 0) return -1;
    /* 简单伪随机：用tip_count做索引变化 */
    int idx = ((uint32_t)(ob.total_tips_shown + 7) % (uint32_t)ob.tip_count);
    int count = 0;
    for (int i = 0; i < OB_MAX_TIPS; i++) {
        if (ob.tips[i].used) {
            if (count == idx) {
                memcpy(tip, &ob.tips[i], sizeof(ob_tip_t));
                ob.tips[i].shown_count++;
                ob.total_tips_shown++;
                return 0;
            }
            count++;
        }
    }
    return -1;
}

int onboarding_get_tips_by_category(int category, ob_tip_t *tips, int max)
{
    if (!tips || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < OB_MAX_TIPS && count < max; i++) {
        if (ob.tips[i].used && ob.tips[i].category_id == category)
            memcpy(&tips[count++], &ob.tips[i], sizeof(ob_tip_t));
    }
    return count;
}

int onboarding_list_tips(ob_tip_t *tips, int max)
{
    if (!tips || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < OB_MAX_TIPS && count < max; i++) {
        if (ob.tips[i].used) memcpy(&tips[count++], &ob.tips[i], sizeof(ob_tip_t));
    }
    return count;
}

int onboarding_vote_tip(int tip_id, int useful)
{
    if (tip_id < 0 || tip_id >= OB_MAX_TIPS || !ob.tips[tip_id].used) return -1;
    if (useful) ob.tips[tip_id].useful_votes++;
    return 0;
}

int onboarding_add_migration(const char *android_way, const char *hepu_way, const char *category)
{
    for (int i = 0; i < 32; i++) {
        if (!ob.migration[i].used) {
            memset(&ob.migration[i], 0, sizeof(ob_migration_item_t));
            ob.migration[i].used = 1;
            ob.migration[i].id = i;
            if (android_way) strncpy(ob.migration[i].android_way, android_way, OB_TEXT_LEN - 1);
            if (hepu_way) strncpy(ob.migration[i].hepu_way, hepu_way, OB_TEXT_LEN - 1);
            if (category) strncpy(ob.migration[i].category, category, 31);
            ob.migration_count++;
            return i;
        }
    }
    return -1;
}

int onboarding_list_migration(ob_migration_item_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 32 && count < max; i++) {
        if (ob.migration[i].used) memcpy(&items[count++], &ob.migration[i], sizeof(ob_migration_item_t));
    }
    return count;
}

int onboarding_set_config(const ob_config_t *config)
{
    if (!config) return -1;
    memcpy(&ob.config, config, sizeof(ob_config_t));
    return 0;
}

int onboarding_get_config(ob_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &ob.config, sizeof(ob_config_t));
    return 0;
}

int onboarding_set_enabled(int enable)
{
    ob.enabled = enable ? 1 : 0;
    return 0;
}

int onboarding_get_stats(onboarding_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ob, sizeof(onboarding_t));
    return 0;
}
