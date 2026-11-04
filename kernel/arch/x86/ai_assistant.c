/* ============================================================================
 * 赫普OS - AI助手框架
 * 自然语言意图识别 + 任务执行
 * ========================================================================== */

#include <stdint.h>
#include "ai_assistant.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

#define MAX_KEYWORDS 16
#define MAX_LEARNED 32

typedef struct {
    ai_assist_intent_t intent;
    const char *keywords[MAX_KEYWORDS];
    const char *response;
} intent_rule_t;

typedef struct {
    char text[64];
    ai_assist_intent_t intent;
} learned_rule_t;

static intent_rule_t rules[] = {
    {AINT_CALL,        {"打电话", "呼叫", "call", "dial", "拨"}, "好的，正在为您拨打电话"},
    {AINT_SMS,         {"发短信", "消息", "sms", "message", "发送"}, "好的，正在发送短信"},
    {AINT_SEARCH,      {"搜索", "查找", "search", "find", "百度", "谷歌"}, "正在为您搜索"},
    {AINT_NAVIGATION,  {"导航", "路线", "navigation", "地图", "去哪"}, "正在规划导航路线"},
    {AINT_MUSIC,       {"音乐", "播放", "music", "歌", "唱"}, "正在为您播放音乐"},
    {AINT_VIDEO,       {"视频", "电影", "video", "movie", "看"}, "正在为您播放视频"},
    {AINT_CAMERA,      {"拍照", "相机", "camera", "拍一张", "自拍"}, "正在打开相机"},
    {AINT_SETTINGS,    {"设置", "settings", "配置", "调整", "修改"}, "正在打开设置"},
    {AINT_ALARM,       {"闹钟", "alarm", "叫醒", "定时"}, "好的，已设置闹钟"},
    {AINT_TIMER,       {"计时", "timer", "倒计时", "秒表"}, "计时器已启动"},
    {AINT_WEATHER,     {"天气", "weather", "温度", "下雨", "冷不冷"}, "正在查询天气"},
    {AINT_NOTE,        {"笔记", "note", "记一下", "记录", "备忘"}, "已为您记录"},
    {AINT_REMINDER,    {"提醒", "remind", "待办", "别忘了"}, "已设置提醒"},
    {AINT_OPEN_APP,    {"打开", "open", "启动", "运行", "app"}, "正在打开应用"},
    {AINT_SYSTEM_INFO, {"系统信息", "关于", "版本", "内存", "存储"}, "正在显示系统信息"},
    {AINT_POWER,       {"关机", "重启", "power", "省电", "休眠"}, "正在执行电源操作"},
};

#define RULE_COUNT (sizeof(rules) / sizeof(rules[0]))

static learned_rule_t learned[MAX_LEARNED];
static int learned_count = 0;
static ai_assistant_config_t config;

void ai_assistant_init(void)
{
    memset(&config, 0, sizeof(config));
    config.enabled = 1;
    config.voice_enabled = 1;
    strncpy(config.wake_word, "小赫", 31); config.wake_word[31] = 0;
    strncpy(config.language, "zh-CN", 15); config.language[15] = 0;
    config.response_speed = 1;
    memset(learned, 0, sizeof(learned));
    vga_puts("[AI-ASSIST] AI assistant framework init(wakeword: Xiao Hepu)\n");
}

int ai_assistant_enable(int enable)
{
    config.enabled = enable;
    return 0;
}

static int match_keyword(const char *text, const char *keyword)
{
    return strstr(text, keyword) != NULL;
}

int ai_assistant_process_text(const char *text, ai_intent_result_t *result)
{
    if (!text || !result || !config.enabled) return -1;

    memset(result, 0, sizeof(ai_intent_result_t));
    strncpy(result->raw_text, text, 127);

    /* 先检查学习的规则 */
    for (int i = 0; i < learned_count; i++) {
        if (strcmp(text, learned[i].text) == 0) {
            result->intent = learned[i].intent;
            result->confidence = 1.0f;
            return 0;
        }
    }

    /* 关键词匹配 */
    int best_match = -1;
    int best_score = 0;

    for (int i = 0; i < (int)RULE_COUNT; i++) {
        int score = 0;
        for (int j = 0; j < MAX_KEYWORDS && rules[i].keywords[j]; j++) {
            if (match_keyword(text, rules[i].keywords[j])) {
                score++;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_match = i;
        }
    }

    if (best_match >= 0 && best_score > 0) {
        result->intent = rules[best_match].intent;
        result->confidence = (float)best_score / 3.0f;
        if (result->confidence > 1.0f) result->confidence = 1.0f;
        /* 提取实体（简化：取第一个数字或名称） */
        const char *p = text;
        int ei = 0;
        while (*p && ei < 63) {
            if ((*p >= '0' && *p <= '9') || ((uint8_t)*p >= 0x80)) {
                result->entity[ei++] = *p;
            }
            p++;
        }
        result->entity[ei] = '\0';
        return 0;
    }

    result->intent = AINT_UNKNOWN;
    result->confidence = 0.0f;
    return 0;
}

int ai_assistant_process_voice(int16_t *audio, int samples, ai_intent_result_t *result)
{
    (void)audio; (void)samples;
    /* 简化：语音识别由ai.c模块处理，这里只做意图匹配 */
    if (!config.voice_enabled) return -1;
    /* 实际应调用ai_speech_recognize()获取文本，再调用process_text */
    result->intent = AINT_UNKNOWN;
    result->confidence = 0.0f;
    return 0;
}

int ai_assistant_execute_intent(ai_intent_result_t *result)
{
    if (!result) return -1;
    switch (result->intent) {
        case AINT_CALL:
            vga_printf("[AI] exec: make call %s\n", result->entity);
            break;
        case AINT_SMS:
            vga_printf("[AI] exec: send SMS \n");
            break;
        case AINT_SEARCH:
            vga_printf("[AI] exec: search %s\n", result->entity);
            break;
        case AINT_NAVIGATION:
            vga_printf("[AI] exec: navigate to %s\n", result->entity);
            break;
        case AINT_MUSIC:
            vga_printf("[AI] exec: play music \n");
            break;
        case AINT_OPEN_APP:
            vga_printf("[AI] exec: open app %s\n", result->entity);
            break;
        case AINT_SETTINGS:
            vga_printf("[AI] exec: open settings \n");
            break;
        case AINT_WEATHER:
            vga_printf("[AI] exec: check weather \n");
            break;
        case AINT_NOTE:
            vga_printf("[AI] exec: take note \n");
            break;
        case AINT_ALARM:
        case AINT_TIMER:
            vga_printf("[AI] exec: set alarm / timer \n");
            break;
        case AINT_POWER:
            vga_printf("[AI] exec: power mgmt \n");
            break;
        case AINT_SYSTEM_INFO:
            vga_printf("[AI] exec: show sysinfo \n");
            break;
        default:
            vga_printf("[AI] unrecognized intent , suggest: try saying \" make call \",\" play music \",\" today's weather \" etc \n");
            return -1;
    }
    return 0;
}

int ai_assistant_get_response(ai_assist_intent_t intent, char *response, int size)
{
    if (!response || size <= 0) return -1;
    for (int i = 0; i < (int)RULE_COUNT; i++) {
        if (rules[i].intent == intent) {
            strncpy(response, rules[i].response, size - 1);
            response[size - 1] = '\0';
            return 0;
        }
    }
    strncpy(response, "抱歉，我没有理解您的意思", size - 1);
    response[size - 1] = '\0';
    return -1;
}

int ai_assistant_set_wake_word(const char *word)
{
    if (!word || strlen(word) >= 32) return -1;
    strncpy(config.wake_word, word, 31); config.wake_word[31] = 0;
    return 0;
}

int ai_assistant_set_language(const char *lang)
{
    if (!lang || strlen(lang) >= 16) return -1;
    strncpy(config.language, lang, 15); config.language[15] = 0;
    return 0;
}

int ai_assistant_get_config(ai_assistant_config_t *cfg)
{
    if (!cfg) return -1;
    memcpy(cfg, &config, sizeof(config));
    return 0;
}

int ai_assistant_learn(const char *text, ai_assist_intent_t intent)
{
    if (!text || learned_count >= MAX_LEARNED) return -1;
    strncpy(learned[learned_count].text, text, 63);
    learned[learned_count].intent = intent;
    learned_count++;
    return 0;
}
