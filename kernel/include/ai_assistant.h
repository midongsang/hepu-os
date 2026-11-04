#ifndef AI_ASSISTANT_H
#define AI_ASSISTANT_H

#include <stdint.h>

/* 意图类型 */
typedef enum {
    AINT_UNKNOWN = 0,
    AINT_CALL,           /* 打电话 */
    AINT_SMS,            /* 发短信 */
    AINT_SEARCH,         /* 搜索 */
    AINT_NAVIGATION,     /* 导航 */
    AINT_MUSIC,          /* 播放音乐 */
    AINT_VIDEO,          /* 播放视频 */
    AINT_CAMERA,         /* 拍照 */
    AINT_SETTINGS,       /* 设置 */
    AINT_ALARM,          /* 闹钟 */
    AINT_TIMER,          /* 计时器 */
    AINT_WEATHER,        /* 天气 */
    AINT_NOTE,           /* 记笔记 */
    AINT_REMINDER,       /* 提醒 */
    AINT_OPEN_APP,       /* 打开应用 */
    AINT_SYSTEM_INFO,    /* 系统信息 */
    AINT_POWER,          /* 电源管理 */
    AINT_COUNT
} ai_assist_intent_t;

/* 意图识别结果 */
typedef struct {
    ai_assist_intent_t intent;
    float confidence;
    char entity[64];       /* 关键实体 */
    char raw_text[128];    /* 原始文本 */
} ai_intent_result_t;

/* AI助手配置 */
typedef struct {
    int enabled;
    int voice_enabled;
    char wake_word[32];
    char language[16];
    int response_speed;    /* 0=快, 1=正常, 2=详细 */
} ai_assistant_config_t;

/* AI助手API */
void ai_assistant_init(void);
int ai_assistant_enable(int enable);
int ai_assistant_process_text(const char *text, ai_intent_result_t *result);
int ai_assistant_process_voice(int16_t *audio, int samples, ai_intent_result_t *result);
int ai_assistant_execute_intent(ai_intent_result_t *result);
int ai_assistant_get_response(ai_assist_intent_t intent, char *response, int size);
int ai_assistant_set_wake_word(const char *word);
int ai_assistant_set_language(const char *lang);
int ai_assistant_get_config(ai_assistant_config_t *config);
int ai_assistant_learn(const char *text, ai_assist_intent_t intent);

#endif /* AI_ASSISTANT_H */
