#ifndef MULTILINGUAL_IME_H
#define MULTILINGUAL_IME_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 多语言输入法
 * 拼音/手写/语音/多语言/词库联想/云同步
 * ========================================================================== */

#define IME_MAX_LANGUAGES 8
#define IME_MAX_CANDIDATES 16
#define IME_MAX_DICTIONARY 4096
#define IME_MAX_USER_WORDS 256
#define MLI_NAME_LEN 128
#define IME_MAX_INPUT 64

/* 输入法语言 */
typedef enum {
    IME_LANG_ZH_PINYIN = 0,  /* 中文拼音 */
    IME_LANG_ZH_WUBI,         /* 中文五笔 */
    IME_LANG_ZH_HANDWRITE,    /* 中文手写 */
    IME_LANG_ZH_VOICE,        /* 中文语音 */
    IME_LANG_EN,              /* 英语 */
    IME_LANG_JA,              /* 日语 */
    IME_LANG_KO,              /* 韩语 */
    IME_LANG_FR,              /* 法语 */
    IME_LANG_DE,              /* 德语 */
    IME_LANG_ES,              /* 西班牙语 */
    IME_LANG_IT,              /* 意大利语 */
    IME_LANG_PT,              /* 葡萄牙语 */
    IME_LANG_RU,              /* 俄语 */
    IME_LANG_AR,              /* 阿拉伯语 */
    IME_LANG_TH,              /* 泰语 */
    IME_LANG_VI,              /* 越南语 */
    IME_LANG_ID,              /* 印尼语 */
    IME_LANG_MS,              /* 马来语 */
    IME_LANG_HI,              /* 印地语 */
    IME_LANG_TR,              /* 土耳其语 */
    IME_LANG_EMOJI,           /* 表情符号 */
} mli_language_t;

/* 输入法模式 */
typedef enum {
    IME_MODE_TEXT = 0,
    IME_MODE_NUMBER,
    IME_MODE_PHONE,
    IME_MODE_EMAIL,
    IME_MODE_URL,
    IME_MODE_PASSWORD,
    IME_MODE_SEARCH,
} mli_mode_t;

/* 键盘布局 */
typedef enum {
    IME_LAYOUT_QWERTY = 0,
    IME_LAYOUT_QWERTZ,
    IME_LAYOUT_AZERTY,
    IME_LAYOUT_DVORAK,
    IME_LAYOUT_T9,
    IME_LAYOUT_5X4,       /* 五笔 */
    IME_LAYOUT_12KEY,     /* 手机12键 */
} mli_layout_t;

/* 候选词 */
typedef struct {
    char text[64];
    int frequency;
    int is_user_word;
    int is_emoji;
} mli_candidate_t;

/* 词典条目 */
typedef struct {
    int used;
    char pinyin[32];
    char word[64];
    int frequency;
    int is_user;
} mli_dict_entry_t;

/* 用户词 */
typedef struct {
    int used;
    char word[64];
    char pinyin[32];
    int frequency;
    uint64_t last_used;
} mli_user_word_t;

/* 输入法状态 */
typedef struct {
    int used;
    mli_language_t language;
    mli_mode_t mode;
    mli_layout_t layout;
    char composing[IME_MAX_INPUT];
    int composing_len;
    mli_candidate_t candidates[IME_MAX_CANDIDATES];
    int candidate_count;
    int selected_candidate;
    int shift_state;
    int caps_lock;
    int emoji_mode;
    int handwriting_enabled;
    int voice_enabled;
    int cloud_sync;
    int auto_correct;
    int show_candidates;
} mli_state_t;

/* 语言配置 */
typedef struct {
    mli_language_t id;
    char name[MLI_NAME_LEN];
    char native_name[MLI_NAME_LEN];
    mli_layout_t default_layout;
    int enabled;
    int dictionary_loaded;
    int word_count;
} mli_language_config_t;

/* 手写识别 */
typedef struct {
    int enabled;
    int stroke_count;
    float strokes[128][2];  /* 笔画点 */
    int point_count;
    char recognized[64];
    float confidence;
} mli_handwriting_t;

/* 语音输入 */
typedef struct {
    int enabled;
    int recording;
    char partial_result[256];
    char final_result[1024];
    float confidence;
    int language;
    uint64_t record_tmli_ms;
} mli_voice_t;

/* 输入法配置 */
typedef struct {
    int enabled;
    mli_language_t primary_language;
    mli_language_t secondary_language;
    mli_layout_t default_layout;
    int auto_capitalize;
    int auto_correct;
    int double_space_period;
    int key_vibration;
    int key_sound;
    int show_candidates;
    int emoji_access;
    int glide_typing;
    int cloud_sync;
    int personalization;
    int keyboard_height;  /* 百分比 */
    int theme;  /* 0=light,1=dark,2=system */
} mli_config_t;

/* 输入法管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    mli_config_t config;
    mli_state_t state;
    /* 语言 */
    mli_language_config_t languages[IME_MAX_LANGUAGES];
    int language_count;
    /* 词典 */
    mli_dict_entry_t dictionary[IME_MAX_DICTIONARY];
    int dict_count;
    /* 用户词 */
    mli_user_word_t user_words[IME_MAX_USER_WORDS];
    int user_word_count;
    /* 手写 */
    mli_handwriting_t handwriting;
    /* 语音 */
    mli_voice_t voice;
    /* 统计 */
    uint64_t total_keystrokes;
    uint64_t total_words;
    uint64_t total_corrections;
    uint64_t total_emoji;
} multilingual_mli_t;

/* 初始化 */
void multilingual_ime_init(void);

/* 语言管理 */
int mli_add_language(mli_language_t lang);
int mli_remove_language(mli_language_t lang);
int mli_set_language(mli_language_t lang);
int mli_get_language(mli_language_t *lang);
int mli_switch_language(void);
int mli_list_languages(mli_language_config_t *langs, int max);
int mli_get_language_config(mli_language_t lang, mli_language_config_t *config);

/* 输入处理 */
int mli_input_key(int keycode, int modifiers);
int mli_backspace(void);
int mli_commit(void);
int mli_clear_composing(void);
int mli_select_candidate(int index);
int mli_get_composing(char *buf, int size);
int mli_get_candidates(mli_candidate_t *cands, int max);
int mli_get_selected_candidate(int *index);

/* 模式/布局 */
int mli_set_mode(mli_mode_t mode);
int mli_get_mode(mli_mode_t *mode);
int mli_set_layout(mli_layout_t layout);
int mli_get_layout(mli_layout_t *layout);
int mli_toggle_shift(void);
int mli_toggle_caps(void);
int mli_toggle_emoji(void);

/* 拼音处理 */
int mli_pinyin_input(const char *pinyin);
int mli_pinyin_get_candidates(const char *pinyin, mli_candidate_t *cands, int max);
int mli_pinyin_commit(int candidate_index);

/* 手写 */
int mli_handwriting_start(void);
int mli_handwriting_add_point(float x, float y);
int mli_handwriting_end_stroke(void);
int mli_handwriting_recognize(char *result, int max);
int mli_handwriting_clear(void);
int mli_handwriting_get_state(mli_handwriting_t *state);

/* 语音 */
int mli_voice_start(void);
int mli_voice_stop(void);
int mli_voice_get_partial(char *buf, int size);
int mli_voice_get_final(char *buf, int size);
int mli_voice_get_state(mli_voice_t *state);

/* 词典/用户词 */
int mli_dict_add(const char *pinyin, const char *word, int frequency);
int mli_dict_lookup(const char *pinyin, mli_dict_entry_t *entries, int max);
int mli_user_word_add(const char *word, const char *pinyin);
int mli_user_word_remove(const char *word);
int mli_user_word_list(mli_user_word_t *words, int max);

/* 配置 */
int mli_set_config(const mli_config_t *config);
int mli_get_config(mli_config_t *config);
int mli_set_auto_correct(int enable);
int mli_set_cloud_sync(int enable);

/* 统计 */
int mli_get_stats(multilingual_mli_t *stats);

#endif /* MULTILINGUAL_IME_H */
