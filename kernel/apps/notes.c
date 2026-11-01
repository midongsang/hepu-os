/* ============================================================================
 * 赫普OS - 记事本应用（从system_tools.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "system_tools.h"
#include "vga.h"
#include "string.h"

static notes_app_t notes;

void notes_init(void)
{
    memset(&notes, 0, sizeof(notes_app_t));
    notes.initialized = 1;
    notes.current_note = -1;
    notes.sort_mode = 0;
    notes.view_mode = 0;

    /* 测试笔记 */
    note_create(" 欢迎 used Hepu notes ", " 这 is Hepu OS 自带 notes app .\n supports 标签 , 颜色 , 置顶 , encryption etc 功能 .");
    note_create(" 购物清单 ", "- 牛奶 \n- 面 packages \n- 鸡蛋 \n- 水果 ");
    note_create(" 会议 records ", "2026 年 8 月 22 日产品会议 \n1. V0.28 version publish \n2. app ecosystem 建设 \n3. 下一步规划 ");
}

int note_create(const char *title, const char *content)
{
    for (int i = 0; i < NOTE_MAX_NOTES; i++) {
        if (!notes.notes[i].used) {
            memset(&notes.notes[i], 0, sizeof(note_t));
            notes.notes[i].used = 1;
            notes.notes[i].id = i;
            if (title) strncpy(notes.notes[i].title, title, 127);
            if (content) strncpy(notes.notes[i].content, content, 4095);
            notes.notes[i].color = i % 8;
            notes.notes[i].created = 1;
            notes.notes[i].modified = 1;
            notes.note_count++;
            return i;
        }
    }
    return -1;
}

int note_delete(int id)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used) return -1;
    notes.notes[id].used = 0;
    notes.note_count--;
    return 0;
}

int note_update(int id, const char *title, const char *content)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used) return -1;
    if (title) strncpy(notes.notes[id].title, title, 127);
    if (content) strncpy(notes.notes[id].content, content, 4095);
    notes.notes[id].modified = 1;
    return 0;
}

int note_get(int id, note_t *note)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used || !note) return -1;
    memcpy(note, &notes.notes[id], sizeof(note_t));
    return 0;
}

int note_list(note_t *notes_out, int max)
{
    if (!notes_out || max <= 0) return 0;
    int count = 0;
    for (int i = NOTE_MAX_NOTES - 1; i >= 0 && count < max; i--) {
        if (notes.notes[i].used && !notes.notes[i].archived)
            memcpy(&notes_out[count++], &notes.notes[i], sizeof(note_t));
    }
    return count;
}

int note_search(const char *query, note_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NOTE_MAX_NOTES && count < max; i++) {
        if (notes.notes[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; notes.notes[i].title[k]; k++) {
                    if (notes.notes[i].title[k] == query[j]) { found = 1; break; }
                }
                for (int k = 0; notes.notes[i].content[k] && !found; k++) {
                    if (notes.notes[i].content[k] == query[j]) { found = 1; break; }
                }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &notes.notes[i], sizeof(note_t));
        }
    }
    return count;
}

int note_toggle_pin(int id)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used) return -1;
    notes.notes[id].pinned = !notes.notes[id].pinned;
    return notes.notes[id].pinned;
}

int note_toggle_archive(int id)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used) return -1;
    notes.notes[id].archived = !notes.notes[id].archived;
    return notes.notes[id].archived;
}

int note_toggle_lock(int id)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used) return -1;
    notes.notes[id].locked = !notes.notes[id].locked;
    return notes.notes[id].locked;
}

int note_set_color(int id, int color)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used) return -1;
    notes.notes[id].color = color;
    return 0;
}

int note_add_tag(int id, const char *tag)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used || !tag) return -1;
    if (notes.notes[id].tag_count >= NOTE_MAX_TAGS) return -1;
    strncpy(notes.notes[id].tags[notes.notes[id].tag_count], tag, 31);
    notes.notes[id].tag_count++;
    return 0;
}

int note_remove_tag(int id, const char *tag)
{
    if (id < 0 || id >= NOTE_MAX_NOTES || !notes.notes[id].used || !tag) return -1;
    for (int i = 0; i < notes.notes[id].tag_count; i++) {
        if (strcmp(notes.notes[id].tags[i], tag) == 0) {
            for (int j = i; j < notes.notes[id].tag_count - 1; j++)
                memcpy(notes.notes[id].tags[j], notes.notes[id].tags[j+1], 32);
            notes.notes[id].tag_count--;
            return 0;
        }
    }
    return -1;
}

int note_list_by_tag(const char *tag, note_t *notes_out, int max)
{
    if (!tag || !notes_out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NOTE_MAX_NOTES && count < max; i++) {
        if (notes.notes[i].used) {
            for (int j = 0; j < notes.notes[i].tag_count; j++) {
                if (strcmp(notes.notes[i].tags[j], tag) == 0) {
                    memcpy(&notes_out[count++], &notes.notes[i], sizeof(note_t));
                    break;
                }
            }
        }
    }
    return count;
}

int notes_get_stats(notes_app_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &notes, sizeof(notes_app_t));
    return 0;
}

/* 统一初始化入口 */
