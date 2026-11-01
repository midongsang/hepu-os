/* ============================================================================
 * 赫普OS - 多语言输入法
 * ========================================================================== */

#include <stdint.h>
#include "multilingual_ime.h"
#include "vga.h"
#include "string.h"

multilingual_mli_t ime;

void multilingual_ime_init(void)
{
    memset(&ime, 0, sizeof(multilingual_mli_t));
    ime.initialized = 1;
    ime.enabled = 1;
    ime.config.primary_language = IME_LANG_ZH_PINYIN;
    ime.config.secondary_language = IME_LANG_EN;
    ime.config.default_layout = IME_LAYOUT_QWERTY;
    ime.config.auto_capitalize = 1;
    ime.config.auto_correct = 1;
    ime.config.double_space_period = 1;
    ime.config.show_candidates = 1;
    ime.config.emoji_access = 1;
    ime.config.glide_typing = 1;
    ime.config.cloud_sync = 1;
    ime.config.keyboard_height = 100;
    ime.config.theme = 2;  /* 跟随系统 */
    ime.state.language = IME_LANG_ZH_PINYIN;
    ime.state.layout = IME_LAYOUT_QWERTY;

    /* 注册支持的语言 */
    const char *names[] = {" Pinyin "," in 文五笔 "," in 文手写 "," in 文 voice ","English",
        " 日本語 ","한국어","Français","Deutsch","Español","Italiano","Português",
        "Русский","العربية","ไทย","Tiếng Việt","Bahasa Indonesia","Bahasa Melayu",
        "हिन्दी","Türkçe","Emoji"};
    for (int i = 0; i < 21 && i < IME_MAX_LANGUAGES; i++) {
        ime.languages[i].id = (mli_language_t)i;
        strncpy(ime.languages[i].name, names[i], 127);
        strncpy(ime.languages[i].native_name, names[i], 127);
        ime.languages[i].enabled = 1;
        ime.languages[i].dictionary_loaded = 1;
        ime.language_count++;
    }

    /* 加载基础词典 */
    const char *dict[][2] = {
        {"ni"," 你 "},{"nihao"," 你好 "},{"wo"," 我 "},{"women"," 我们 "},
        {"ta"," 他 "},{"tamen"," 他们 "},{"shi"," is "},{"bu"," not "},
        {"yi"," 一 "},{"er"," 二 "},{"san"," 三 "},{"si"," 四 "},
        {"wu"," 五 "},{"liu"," 六 "},{"qi"," 七 "},{"ba"," 八 "},
        {"jiu"," 九 "},{"shi"," 十 "},{"bai"," 百 "},{"qian"," 千 "},
        {"wan"," 万 "},{"ren"," 人 "},{"da"," 大 "},{"xiao"," 小 "},
        {"hao"," 好 "},{"huai"," bad "},{"duo"," 多 "},{"shao"," 少 "},
        {"lai"," 来 "},{"qu"," 去 "},{"shang"," "},{"xia"," 下 "},
        {"zuo"," 左 "},{"you"," 右 "},{"qian"," prev "},{"hou"," next "},
        {"zhong"," in "},{"guo"," 国 "},{"zhongguo"," in 国 "},{"hepu"," Hepu "},
    };
    for (int i = 0; i < 40 && i < IME_MAX_DICTIONARY; i++) {
        ime.dictionary[i].used = 1;
        strncpy(ime.dictionary[i].pinyin, dict[i][0], 31);
        strncpy(ime.dictionary[i].word, dict[i][1], 63);
        ime.dictionary[i].frequency = 100 - i;
        ime.dict_count++;
    }

    vga_puts("[IME] multilingual IME init done \n");
    vga_printf(" lang: %d, dict: %d words, master lang: Pinyin \n",
               ime.language_count, ime.dict_count);
}

int mli_add_language(mli_language_t lang)
{
    if (lang < 0 || lang >= IME_MAX_LANGUAGES) return -1;
    for (int i = 0; i < IME_MAX_LANGUAGES; i++) {
        if (!ime.languages[i].enabled && ime.languages[i].id == lang) {
            ime.languages[i].enabled = 1;
            return 0;
        }
    }
    return -1;
}

int mli_remove_language(mli_language_t lang)
{
    for (int i = 0; i < IME_MAX_LANGUAGES; i++) {
        if (ime.languages[i].id == lang && ime.languages[i].enabled) {
            ime.languages[i].enabled = 0;
            return 0;
        }
    }
    return -1;
}

int mli_set_language(mli_language_t lang)
{
    ime.state.language = lang;
    return 0;
}

int mli_get_language(mli_language_t *lang)
{
    if (!lang) return -1;
    *lang = ime.state.language;
    return 0;
}

int mli_switch_language(void)
{
    if (ime.state.language == ime.config.primary_language)
        ime.state.language = ime.config.secondary_language;
    else
        ime.state.language = ime.config.primary_language;
    return 0;
}

int mli_list_languages(mli_language_config_t *langs, int max)
{
    if (!langs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < IME_MAX_LANGUAGES && count < max; i++) {
        if (ime.languages[i].enabled)
            memcpy(&langs[count++], &ime.languages[i], sizeof(mli_language_config_t));
    }
    return count;
}

int mli_get_language_config(mli_language_t lang, mli_language_config_t *config)
{
    if (!config) return -1;
    for (int i = 0; i < IME_MAX_LANGUAGES; i++) {
        if (ime.languages[i].id == lang) {
            memcpy(config, &ime.languages[i], sizeof(mli_language_config_t));
            return 0;
        }
    }
    return -1;
}

int mli_input_key(int keycode, int modifiers)
{
    (void)modifiers;
    if (keycode >= 'a' && keycode <= 'z' && ime.state.composing_len < IME_MAX_INPUT - 1) {
        ime.state.composing[ime.state.composing_len++] = (char)keycode;
        ime.state.composing[ime.state.composing_len] = 0;
        ime.total_keystrokes++;
        /* 拼音模式下更新候选词 */
        if (ime.state.language == IME_LANG_ZH_PINYIN)
            mli_pinyin_get_candidates(ime.state.composing, ime.state.candidates, IME_MAX_CANDIDATES);
        return 0;
    }
    return -1;
}

int mli_backspace(void)
{
    if (ime.state.composing_len > 0) {
        ime.state.composing_len--;
        ime.state.composing[ime.state.composing_len] = 0;
        return 0;
    }
    return -1;
}

int mli_commit(void)
{
    if (ime.state.composing_len > 0) {
        ime.state.composing_len = 0;
        ime.state.composing[0] = 0;
        ime.state.candidate_count = 0;
        ime.total_words++;
        return 0;
    }
    return -1;
}

int mli_clear_composing(void)
{
    ime.state.composing_len = 0;
    ime.state.composing[0] = 0;
    ime.state.candidate_count = 0;
    return 0;
}

int mli_select_candidate(int index)
{
    if (index < 0 || index >= ime.state.candidate_count) return -1;
    ime.state.selected_candidate = index;
    ime.total_words++;
    return 0;
}

int mli_get_composing(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int len = 0;
    while (ime.state.composing[len] && len < size - 1) { buf[len] = ime.state.composing[len]; len++; }
    buf[len] = 0;
    return len;
}

int mli_get_candidates(mli_candidate_t *cands, int max)
{
    if (!cands || max <= 0) return 0;
    int count = ime.state.candidate_count < max ? ime.state.candidate_count : max;
    for (int i = 0; i < count; i++) memcpy(&cands[i], &ime.state.candidates[i], sizeof(mli_candidate_t));
    return count;
}

int mli_get_selected_candidate(int *index)
{
    if (!index) return -1;
    *index = ime.state.selected_candidate;
    return 0;
}

int mli_set_mode(mli_mode_t mode)
{
    ime.state.mode = mode;
    return 0;
}

int mli_get_mode(mli_mode_t *mode)
{
    if (!mode) return -1;
    *mode = ime.state.mode;
    return 0;
}

int mli_set_layout(mli_layout_t layout)
{
    ime.state.layout = layout;
    return 0;
}

int mli_get_layout(mli_layout_t *layout)
{
    if (!layout) return -1;
    *layout = ime.state.layout;
    return 0;
}

int mli_toggle_shift(void)
{
    ime.state.shift_state = !ime.state.shift_state;
    return ime.state.shift_state;
}

int mli_toggle_caps(void)
{
    ime.state.caps_lock = !ime.state.caps_lock;
    return ime.state.caps_lock;
}

int mli_toggle_emoji(void)
{
    ime.state.emoji_mode = !ime.state.emoji_mode;
    return ime.state.emoji_mode;
}

int mli_pinyin_input(const char *pinyin)
{
    if (!pinyin) return -1;
    int len = 0;
    while (pinyin[len] && len < IME_MAX_INPUT - 1) {
        ime.state.composing[len] = pinyin[len];
        len++;
    }
    ime.state.composing[len] = 0;
    ime.state.composing_len = len;
    return mli_pinyin_get_candidates(pinyin, ime.state.candidates, IME_MAX_CANDIDATES);
}

int mli_pinyin_get_candidates(const char *pinyin, mli_candidate_t *cands, int max)
{
    if (!pinyin || !cands || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < IME_MAX_DICTIONARY && count < max; i++) {
        if (ime.dictionary[i].used &&
            strncmp(ime.dictionary[i].pinyin, pinyin, 32) == 0) {
            memset(&cands[count], 0, sizeof(mli_candidate_t));
            strncpy(cands[count].text, ime.dictionary[i].word, 63);
            cands[count].frequency = ime.dictionary[i].frequency;
            count++;
        }
    }
    /* 模糊匹配 */
    if (count == 0) {
        for (int i = 0; i < IME_MAX_DICTIONARY && count < max; i++) {
            if (ime.dictionary[i].used &&
                strncmp(ime.dictionary[i].pinyin, pinyin, strlen(pinyin)) == 0) {
                memset(&cands[count], 0, sizeof(mli_candidate_t));
                strncpy(cands[count].text, ime.dictionary[i].word, 63);
                cands[count].frequency = ime.dictionary[i].frequency;
                count++;
            }
        }
    }
    ime.state.candidate_count = count;
    return count;
}

int mli_pinyin_commit(int candidate_index)
{
    if (candidate_index < 0 || candidate_index >= ime.state.candidate_count) return -1;
    ime.total_words++;
    mli_clear_composing();
    return 0;
}

int mli_set_config(const mli_config_t *config)
{
    if (!config) return -1;
    memcpy(&ime.config, config, sizeof(mli_config_t));
    return 0;
}

int mli_get_config(mli_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &ime.config, sizeof(mli_config_t));
    return 0;
}

int mli_set_auto_correct(int enable)
{
    ime.config.auto_correct = enable ? 1 : 0;
    return 0;
}

int mli_set_cloud_sync(int enable)
{
    ime.config.cloud_sync = enable ? 1 : 0;
    return 0;
}

int mli_get_stats(multilingual_mli_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ime, sizeof(multilingual_mli_t));
    return 0;
}

