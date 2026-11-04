/* ============================================================================
 * 赫普OS - AI通知分级
 * ========================================================================== */

#include <stdint.h>
#include "ai_notification.h"
#include "vga.h"
#include "string.h"

/* 裸机环境补充：strncat 和 strstr */


static ai_notification_t an;

/* 关键词分类表 */
static const char *important_keywords[] = {
    " 电话 ", " 来电 ", " 通话 ", " SMS ", " verify 码 ", " 密码 ", " payment ", " 转账 ",
    " 订单 ", " 快递 ", " 日程 ", " 提醒 ", " 闹钟 ", " 紧急 ", " 重要 ", " 银行 ",
    "call", "urgent", "important", "verify", "code", "payment", NULL
};

static const char *promotion_keywords[] = {
    " 优惠 ", " 打折 ", " 促销 ", " full 减 ", " s 杀 ", " 抢购 ", " 免费 ", " 赠送 ",
    " 红 packages ", " 优惠券 ", " 限 runtime ", " 特价 ", " 新品 ", " 广告 ", " 推广 ",
    "sale", "discount", "free", "gift", "offer", "promo", NULL
};

static const char *spam_keywords[] = {
    " in 奖 ", " 恭喜 ", " 领取 ", " 点击 ", " 链接 ", " 兼职 ", " 刷单 ", " 贷款 ",
    " 赌博 ", " 色情 ", " 发票 ", " 代开 ", " 退款 ", " 客服 ", " in 奖了 ",
    "win", "free money", "click here", "urgent action", NULL
};

static const char *social_keywords[] = {
    " msg ", " 评论 ", " 点赞 ", " 关注 ", " 粉丝 ", "@", " reply ", " 好友 ",
    "message", "comment", "like", "follow", "mention", "reply", NULL
};

static const char *news_keywords[] = {
    " 新闻 ", " 资讯 ", " header ", " hotspot ", " 快讯 ", " 日报 ", " 周报 ",
    "news", "headline", "breaking", "daily", NULL
};

static int an_match_keyword(const char *text, const char **keywords)
{
    if (!text) return 0;
    for (int i = 0; keywords[i]; i++) {
        if (strstr(text, keywords[i])) return 1;
    }
    return 0;
}

void ai_notification_init(void)
{
    memset(&an, 0, sizeof(ai_notification_t));
    an.initialized = 1;
    an.enabled = 1;
    an.config.enabled = 1;
    an.config.auto_learn = 1;
    an.config.spam_filter = 1;
    an.config.promotion_filter = 1;
    an.config.smart_silence = 1;
    an.config.focus_mode = 0;
    an.config.sleep_mode = 1;
    an.config.quiet_hours_start = 23;
    an.config.quiet_hours_end = 7;
    an.config.min_spam_confidence = 60;

    /* 默认规则 */
    const char *sys_apps[] = {" 电话 ", " SMS ", " system ", " set ", "hepu.phone", "hepu.sms"};
    for (int i = 0; i < 6; i++) {
        ai_notification_rule_add(sys_apps[i], "", AN_CAT_IMPORTANT, AN_PRIORITY_HIGH, AN_ACTION_SHOW);
    }
    /* 推广类应用默认静默 */
    const char *promo_apps[] = {" 购物 ", " 商城 ", " 电商 ", "shop", "mall"};
    for (int i = 0; i < 5; i++) {
        ai_notification_rule_add(promo_apps[i], "", AN_CAT_PROMOTION, AN_PRIORITY_LOW, AN_ACTION_SILENT);
    }

    vga_puts("[AINOTIF] AI notif classify init done \n");
    vga_printf(" spam filter: %s, promo filter: %s, rules: %d \n",
               an.config.spam_filter ? " on " : " close ",
               an.config.promotion_filter ? " on " : " close ",
               an.rule_count);
}

int ai_notification_classify(const char *app, const char *title, const char *content,
                             an_category_t *category, an_priority_t *priority,
                             an_action_t *action, float *confidence)
{
    if (!category || !priority || !action) return -1;
    if (!an.enabled) {
        *category = AN_CAT_NORMAL;
        *priority = AN_PRIORITY_NORMAL;
        *action = AN_ACTION_SHOW;
        if (confidence) *confidence = 1.0f;
        return 0;
    }

    /* 合并文本用于匹配 */
    char combined[768];
    combined[0] = 0;
    if (app) strncat(combined, app, 127);
    if (title) { strncat(combined, " ", 1); strncat(combined, title, 255); }
    if (content) { strncat(combined, " ", 1); strncat(combined, content, 383); }

    /* 先匹配规则 */
    for (int i = 0; i < AN_MAX_RULES; i++) {
        if (an.rules[i].used && an.rules[i].enabled) {
            int app_match = (an.rules[i].app[0] == 0) ||
                            (app && strstr(app, an.rules[i].app));
            int kw_match = (an.rules[i].keyword[0] == 0) ||
                           strstr(combined, an.rules[i].keyword);
            if (app_match && kw_match) {
                *category = an.rules[i].category;
                *priority = an.rules[i].priority;
                *action = an.rules[i].action;
                if (confidence) *confidence = 0.95f;
                an.rules[i].hit_count++;
                return 0;
            }
        }
    }

    /* 关键词分类 */
    if (an_match_keyword(combined, spam_keywords)) {
        *category = AN_CAT_SPAM;
        *priority = AN_PRIORITY_SPAM;
        *action = an.config.spam_filter ? AN_ACTION_FILTER : AN_ACTION_COLLAPSE;
        if (confidence) *confidence = 0.85f;
    } else if (an_match_keyword(combined, promotion_keywords)) {
        *category = AN_CAT_PROMOTION;
        *priority = AN_PRIORITY_LOW;
        *action = an.config.promotion_filter ? AN_ACTION_SILENT : AN_ACTION_SHOW;
        if (confidence) *confidence = 0.80f;
    } else if (an_match_keyword(combined, important_keywords)) {
        *category = AN_CAT_IMPORTANT;
        *priority = AN_PRIORITY_HIGH;
        *action = AN_ACTION_SHOW;
        if (confidence) *confidence = 0.88f;
    } else if (an_match_keyword(combined, social_keywords)) {
        *category = AN_CAT_SOCIAL;
        *priority = AN_PRIORITY_NORMAL;
        *action = AN_ACTION_SHOW;
        if (confidence) *confidence = 0.75f;
    } else if (an_match_keyword(combined, news_keywords)) {
        *category = AN_CAT_NEWS;
        *priority = AN_PRIORITY_LOW;
        *action = an.config.smart_silence ? AN_ACTION_SILENT : AN_ACTION_SHOW;
        if (confidence) *confidence = 0.70f;
    } else {
        *category = AN_CAT_NORMAL;
        *priority = AN_PRIORITY_NORMAL;
        *action = AN_ACTION_SHOW;
        if (confidence) *confidence = 0.50f;
    }

    /* 专注模式：非重要全部静默 */
    if (an.config.focus_mode && *category != AN_CAT_IMPORTANT &&
        *category != AN_CAT_SYSTEM) {
        *action = AN_ACTION_SILENT;
        if (*priority == AN_PRIORITY_URGENT) *priority = AN_PRIORITY_NORMAL;
    }

    return 0;
}

int ai_notification_process(const char *app, const char *title, const char *content)
{
    an_category_t cat;
    an_priority_t pri;
    an_action_t act;
    float conf;
    ai_notification_classify(app, title, content, &cat, &pri, &act, &conf);

    /* 记录历史 */
    for (int i = 0; i < AN_MAX_HISTORY; i++) {
        if (!an.history[i].used) {
            memset(&an.history[i], 0, sizeof(an_notification_t));
            an.history[i].used = 1;
            an.history[i].id = i;
            if (app) strncpy(an.history[i].app, app, AN_APP_LEN - 1);
            if (title) strncpy(an.history[i].title, title, 127);
            if (content) strncpy(an.history[i].content, content, AN_TEXT_LEN - 1);
            an.history[i].category = cat;
            an.history[i].priority = pri;
            an.history[i].action = act;
            an.history[i].confidence = conf;
            an.history[i].timestamp = 1;
            an.history_count++;
            break;
        }
    }

    /* 更新统计 */
    an.stats.total_notifications++;
    if (cat == AN_CAT_IMPORTANT) an.stats.total_important++;
    if (cat == AN_CAT_PROMOTION) an.stats.total_promotions++;
    if (cat == AN_CAT_SPAM) an.stats.total_spam++;
    if (act == AN_ACTION_FILTER) an.stats.total_filtered++;
    if (act == AN_ACTION_SILENT || act == AN_ACTION_COLLAPSE) an.stats.total_silenced++;

    return (int)act;
}

int ai_notification_on_click(int notification_id)
{
    if (notification_id < 0 || notification_id >= AN_MAX_HISTORY ||
        !an.history[notification_id].used) return -1;
    an.history[notification_id].user_clicked = 1;
    an.stats.total_user_clicks++;
    /* 学习：用户点击的通知可能更重要 */
    if (an.config.auto_learn && an.history[notification_id].category == AN_CAT_PROMOTION) {
        /* 用户点击了推广，降低过滤强度 */
    }
    return 0;
}

int ai_notification_on_dismiss(int notification_id)
{
    if (notification_id < 0 || notification_id >= AN_MAX_HISTORY ||
        !an.history[notification_id].used) return -1;
    an.history[notification_id].user_dismissed = 1;
    an.stats.total_user_dismisses++;
    return 0;
}

int ai_notification_rule_add(const char *app, const char *keyword,
                             an_category_t category, an_priority_t priority,
                             an_action_t action)
{
    for (int i = 0; i < AN_MAX_RULES; i++) {
        if (!an.rules[i].used) {
            memset(&an.rules[i], 0, sizeof(an_rule_t));
            an.rules[i].used = 1;
            an.rules[i].id = i;
            if (app) strncpy(an.rules[i].app, app, AN_APP_LEN - 1);
            if (keyword) strncpy(an.rules[i].keyword, keyword, 63);
            an.rules[i].category = category;
            an.rules[i].priority = priority;
            an.rules[i].action = action;
            an.rules[i].enabled = 1;
            an.rule_count++;
            return i;
        }
    }
    return -1;
}

int ai_notification_rule_remove(int rule_id)
{
    if (rule_id < 0 || rule_id >= AN_MAX_RULES || !an.rules[rule_id].used) return -1;
    an.rules[rule_id].used = 0;
    an.rule_count--;
    return 0;
}

int ai_notification_rule_list(an_rule_t *rules, int max)
{
    if (!rules || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AN_MAX_RULES && count < max; i++) {
        if (an.rules[i].used) memcpy(&rules[count++], &an.rules[i], sizeof(an_rule_t));
    }
    return count;
}

int ai_notification_rule_enable(int rule_id, int enable)
{
    if (rule_id < 0 || rule_id >= AN_MAX_RULES || !an.rules[rule_id].used) return -1;
    an.rules[rule_id].enabled = enable ? 1 : 0;
    return 0;
}

int ai_notification_app_config_set(const char *app, const an_app_config_t *config)
{
    if (!app || !config) return -1;
    for (int i = 0; i < 64; i++) {
        if (an.app_configs[i].used && strcmp(an.app_configs[i].app, app) == 0) {
            memcpy(&an.app_configs[i], config, sizeof(an_app_config_t));
            return 0;
        }
    }
    for (int i = 0; i < 64; i++) {
        if (!an.app_configs[i].used) {
            memcpy(&an.app_configs[i], config, sizeof(an_app_config_t));
            an.app_configs[i].used = 1;
            strncpy(an.app_configs[i].app, app, AN_APP_LEN - 1);
            an.app_config_count++;
            return 0;
        }
    }
    return -1;
}

int ai_notification_app_config_get(const char *app, an_app_config_t *config)
{
    if (!app || !config) return -1;
    for (int i = 0; i < 64; i++) {
        if (an.app_configs[i].used && strcmp(an.app_configs[i].app, app) == 0) {
            memcpy(config, &an.app_configs[i], sizeof(an_app_config_t));
            return 0;
        }
    }
    return -1;
}

int ai_notification_app_allow(const char *app, int allow)
{
    an_app_config_t cfg;
    if (ai_notification_app_config_get(app, &cfg) != 0) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.allowed = allow;
        cfg.show_on_lock_screen = 1;
        cfg.badge_enabled = 1;
        return ai_notification_app_config_set(app, &cfg);
    }
    cfg.allowed = allow;
    return ai_notification_app_config_set(app, &cfg);
}

int ai_notification_app_silence(const char *app, int silent)
{
    an_app_config_t cfg;
    if (ai_notification_app_config_get(app, &cfg) != 0) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.allowed = 1;
        cfg.silent_mode = silent;
        return ai_notification_app_config_set(app, &cfg);
    }
    cfg.silent_mode = silent;
    return ai_notification_app_config_set(app, &cfg);
}

int ai_notification_set_config(const an_config_t *config)
{
    if (!config) return -1;
    memcpy(&an.config, config, sizeof(an_config_t));
    return 0;
}

int ai_notification_get_config(an_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &an.config, sizeof(an_config_t));
    return 0;
}

int ai_notification_set_enabled(int enable)
{
    an.enabled = enable ? 1 : 0;
    return 0;
}

int ai_notification_set_focus_mode(int enable)
{
    an.config.focus_mode = enable ? 1 : 0;
    return 0;
}

int ai_notification_set_sleep_mode(int enable)
{
    an.config.sleep_mode = enable ? 1 : 0;
    return 0;
}

int ai_notification_get_history(an_notification_t *notifs, int max)
{
    if (!notifs || max <= 0) return 0;
    int count = 0;
    for (int i = AN_MAX_HISTORY - 1; i >= 0 && count < max; i--) {
        if (an.history[i].used) memcpy(&notifs[count++], &an.history[i], sizeof(an_notification_t));
    }
    return count;
}

int ai_notification_get_by_category(an_category_t cat, an_notification_t *notifs, int max)
{
    if (!notifs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AN_MAX_HISTORY && count < max; i++) {
        if (an.history[i].used && an.history[i].category == cat)
            memcpy(&notifs[count++], &an.history[i], sizeof(an_notification_t));
    }
    return count;
}

int ai_notification_get_stats(an_stats_t *stats)
{
    if (!stats) return -1;
    /* 估算准确率 */
    if (an.stats.total_notifications > 0) {
        uint32_t correct = an.stats.total_user_clicks + an.stats.total_filtered;
        an.stats.accuracy_estimate = (float)((uint32_t)correct * 100u / (uint32_t)an.stats.total_notifications);
    }
    memcpy(stats, &an.stats, sizeof(an_stats_t));
    return 0;
}

int ai_notification_get_pending_count(int *count)
{
    if (!count) return -1;
    int c = 0;
    for (int i = 0; i < AN_MAX_HISTORY; i++) {
        if (an.history[i].used && !an.history[i].user_clicked && !an.history[i].user_dismissed)
            c++;
    }
    *count = c;
    return 0;
}

int ai_notification_learn_feedback(int notification_id, int is_important)
{
    if (notification_id < 0 || notification_id >= AN_MAX_HISTORY ||
        !an.history[notification_id].used) return -1;
    if (is_important && an.history[notification_id].category != AN_CAT_IMPORTANT) {
        /* 用户标记为重要，添加规则 */
        ai_notification_rule_add(an.history[notification_id].app, "",
                    AN_CAT_IMPORTANT, AN_PRIORITY_HIGH, AN_ACTION_SHOW);
    }
    return 0;
}

int ai_notification_reset_learning(void)
{
    /* 清除自动添加的规则（保留系统规则，前11条） */
    for (int i = 11; i < AN_MAX_RULES; i++) {
        if (an.rules[i].used) {
            an.rules[i].used = 0;
            an.rule_count--;
        }
    }
    return 0;
}
