/* ============================================================================
 * 赫普OS - 高级输入法功能（从multilingual_ime.c拆分）
 * 手写识别 / 语音输入 / 词典管理 / 用户词
 * ========================================================================== */

#include <stdint.h>
#include "multilingual_ime.h"
#include "vga.h"
#include "string.h"

extern multilingual_mli_t ime;

int mli_handwriting_start(void)
{
    memset(&ime.handwriting, 0, sizeof(mli_handwriting_t));
    ime.handwriting.enabled = 1;
    return 0;
}

int mli_handwriting_add_point(float x, float y)
{
    if (ime.handwriting.point_count < 128) {
        ime.handwriting.strokes[ime.handwriting.point_count][0] = x;
        ime.handwriting.strokes[ime.handwriting.point_count][1] = y;
        ime.handwriting.point_count++;
        return 0;
    }
    return -1;
}

int mli_handwriting_end_stroke(void)
{
    ime.handwriting.stroke_count++;
    return 0;
}

int mli_handwriting_recognize(char *result, int max)
{
    if (!result || max <= 0) return -1;
    /* 简化识别 */
    strncpy(result, " 字 ", max - 1);
    ime.handwriting.confidence = 0.85f;
    return 1;
}

int mli_handwriting_clear(void)
{
    memset(&ime.handwriting, 0, sizeof(mli_handwriting_t));
    return 0;
}

int mli_handwriting_get_state(mli_handwriting_t *state)
{
    if (!state) return -1;
    memcpy(state, &ime.handwriting, sizeof(mli_handwriting_t));
    return 0;
}

int mli_voice_start(void)
{
    ime.voice.enabled = 1;
    ime.voice.recording = 1;
    return 0;
}

int mli_voice_stop(void)
{
    ime.voice.recording = 0;
    return 0;
}

int mli_voice_get_partial(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int len = 0;
    while (ime.voice.partial_result[len] && len < size - 1) { buf[len] = ime.voice.partial_result[len]; len++; }
    buf[len] = 0;
    return len;
}

int mli_voice_get_final(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int len = 0;
    while (ime.voice.final_result[len] && len < size - 1) { buf[len] = ime.voice.final_result[len]; len++; }
    buf[len] = 0;
    return len;
}

int mli_voice_get_state(mli_voice_t *state)
{
    if (!state) return -1;
    memcpy(state, &ime.voice, sizeof(mli_voice_t));
    return 0;
}

int mli_dict_add(const char *pinyin, const char *word, int frequency)
{
    for (int i = 0; i < IME_MAX_DICTIONARY; i++) {
        if (!ime.dictionary[i].used) {
            memset(&ime.dictionary[i], 0, sizeof(mli_dict_entry_t));
            ime.dictionary[i].used = 1;
            if (pinyin) strncpy(ime.dictionary[i].pinyin, pinyin, 31);
            if (word) strncpy(ime.dictionary[i].word, word, 63);
            ime.dictionary[i].frequency = frequency;
            ime.dict_count++;
            return 0;
        }
    }
    return -1;
}

int mli_dict_lookup(const char *pinyin, mli_dict_entry_t *entries, int max)
{
    if (!pinyin || !entries || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < IME_MAX_DICTIONARY && count < max; i++) {
        if (ime.dictionary[i].used &&
            strncmp(ime.dictionary[i].pinyin, pinyin, 32) == 0) {
            memcpy(&entries[count++], &ime.dictionary[i], sizeof(mli_dict_entry_t));
        }
    }
    return count;
}

int mli_user_word_add(const char *word, const char *pinyin)
{
    for (int i = 0; i < IME_MAX_USER_WORDS; i++) {
        if (!ime.user_words[i].used) {
            memset(&ime.user_words[i], 0, sizeof(mli_user_word_t));
            ime.user_words[i].used = 1;
            if (word) strncpy(ime.user_words[i].word, word, 63);
            if (pinyin) strncpy(ime.user_words[i].pinyin, pinyin, 31);
            ime.user_words[i].frequency = 1;
            ime.user_word_count++;
            return 0;
        }
    }
    return -1;
}

int mli_user_word_remove(const char *word)
{
    for (int i = 0; i < IME_MAX_USER_WORDS; i++) {
        if (ime.user_words[i].used && strcmp(ime.user_words[i].word, word) == 0) {
            ime.user_words[i].used = 0;
            ime.user_word_count--;
            return 0;
        }
    }
    return -1;
}

int mli_user_word_list(mli_user_word_t *words, int max)
{
    if (!words || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < IME_MAX_USER_WORDS && count < max; i++) {
        if (ime.user_words[i].used) memcpy(&words[count++], &ime.user_words[i], sizeof(mli_user_word_t));
    }
    return count;
}

