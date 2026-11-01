/* ============================================================================
 * 赫普OS - 中文拼音输入法核心 (V3.25, V3.27 候选翻页)
 * 前缀匹配 ime_table, 候选缓存 UTF-8 拆字, 0 键翻页 (每页 IME_PAGE_SIZE 个)
 * ========================================================================== */

#include "ime.h"
#include <string.h>
#include "ide_ext2.h"          /* V3.42: 频率持久化落盘 */

/* ime_data.c 提供的数据表 */
typedef struct {
    const char *py;
    const char *hz;
} ime_entry_t;
extern const ime_entry_t ime_table[];
extern const int ime_table_entries;   /* ime_data.c 导出的真实条目数 */
extern const ime_entry_t ime_phrase_table[];   /* V3.28: 常用词组表 */
extern const int ime_phrase_entries;
extern const ime_entry_t ime_short_table[];    /* V3.69: 简拼表 */
extern const int ime_short_entries;

static char ime_py[IME_PINYIN_MAX + 1];       /* 拼音缓冲 */
static int  ime_py_len = 0;
static char ime_cands[IME_CAND_MAX][IME_CHAR_MAX];  /* 候选字(UTF-8) */
static int  ime_cand_cnt = 0;
static int  ime_page = 0;                     /* V3.27: 当前候选页(0 起) */

/* V3.39: 候选词使用频率表 (会话内记忆, 高频候选排序靠前) */
#define IME_FREQ_MAX 12
static char ime_freq_words[IME_FREQ_MAX][IME_CHAR_MAX];
static uint16_t ime_freq_vals[IME_FREQ_MAX];
static int  ime_freq_n = 0;

/* V3.42: 频率持久化标志 */
static int ime_freq_loaded = 0;

/* V3.42: 从 /data/ime/freq.txt 加载频率表 (每行 "词 频率") */
static void ime_freq_load(void)
{
    if (ime_freq_loaded) return;
    ime_freq_loaded = 1;
    char buf[1024];
    int n = ide_ext2_read_file("/data/ime", "freq.txt", buf, 1023);
    if (n <= 0) return;
    buf[n] = 0;
    char *line = buf;
    ime_freq_n = 0;
    while (line && ime_freq_n < IME_FREQ_MAX) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *sp = strchr(line, ' ');
        if (sp && line[0]) {
            *sp = 0;
            int v = 0;
            char *p = sp + 1;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (v > 0) {
                strncpy(ime_freq_words[ime_freq_n], line, IME_CHAR_MAX - 1);
                ime_freq_words[ime_freq_n][IME_CHAR_MAX - 1] = 0;
                ime_freq_vals[ime_freq_n] = (uint16_t)(v > 65535 ? 65535 : v);
                ime_freq_n++;
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
}

/* V3.42: 频率表落盘到 /data/ime/freq.txt */
static void ime_freq_save(void)
{
    char out[1024];
    int op = 0;
    for (int i = 0; i < ime_freq_n && op < 900; i++) {
        int j = 0;
        while (ime_freq_words[i][j] && op < 1008) out[op++] = ime_freq_words[i][j++];
        if (op < 1010) out[op++] = ' ';
        char num[8]; int nn = 0;
        unsigned int v = ime_freq_vals[i];
        do { num[nn++] = '0' + v % 10; v /= 10; } while (v && nn < 7);
        while (nn > 0 && op < 1010) out[op++] = num[--nn];
        if (op < 1010) out[op++] = '\n';
    }
    if (op > 0) {
        out[op] = 0;
        ide_ext2_mkdir("data/ime");
        ide_ext2_writefile("data/ime/freq.txt", out);
        ide_ext2_sync();
    }
}

/* V3.39: 查询候选词频率 (未记录返回 0) */
static int ime_freq_of(const char *w)
{
    for (int i = 0; i < ime_freq_n; i++) {
        if (strcmp(ime_freq_words[i], w) == 0) return ime_freq_vals[i];
    }
    return 0;
}

/* V3.39: 记录候选词使用 (已存在 +1; 新词替换频率最低项) */
void ime_bump(const char *w)
{
    if (!w || !*w) return;
    ime_freq_load();
    for (int i = 0; i < ime_freq_n; i++) {
        if (strcmp(ime_freq_words[i], w) == 0) {
            ime_freq_vals[i]++;
            ime_freq_save();   /* V3.48: 已存在词频率增长也落盘 */
            return;
        }
    }
    if (ime_freq_n < IME_FREQ_MAX) {
        int n = ime_freq_n++;
        strncpy(ime_freq_words[n], w, IME_CHAR_MAX - 1);
        ime_freq_words[n][IME_CHAR_MAX - 1] = 0;
        ime_freq_vals[n] = 1;
    } else {
        int min = 0;
        for (int i = 1; i < IME_FREQ_MAX; i++)
            if (ime_freq_vals[i] < ime_freq_vals[min]) min = i;
        strncpy(ime_freq_words[min], w, IME_CHAR_MAX - 1);
        ime_freq_words[min][IME_CHAR_MAX - 1] = 0;
        ime_freq_vals[min] = 1;
    }
    ime_freq_save();
}

/* 从 "字1 字2 字3" 中切出第 n 个 UTF-8 字 */
static int ime_split_word(const char *hz, int n, char *out, int cap)
{
    const char *p = hz;
    int idx = 0;
    while (*p) {
        /* 跳过空格 */
        while (*p == ' ') p++;
        if (!*p) break;
        const char *start = p;
        /* 跳过 1 个 UTF-8 字符(中文 3 字节, 全角 3 字节) */
        p++;
        while ((*p & 0xC0) == 0x80) p++;
        if (idx == n) {
            int len = (int)(p - start);
            if (len >= cap) len = cap - 1;
            memcpy(out, start, (size_t)len);
            out[len] = 0;
            return 0;
        }
        idx++;
    }
    return -1;
}

/* 重建候选: 前缀匹配 ime_table, 拆字填充 (最多 IME_CAND_MAX) */
static void ime_rebuild_cands(void)
{
    ime_cand_cnt = 0;
    ime_page = 0;                 /* V3.27: 拼音变化回到第一页 */
    if (ime_py_len == 0) return;
    size_t plen = (size_t)ime_py_len;
    /* V3.28: 第一遍扫词组表(整词候选, 排单字前) */
    for (int i = 0; i < ime_phrase_entries && ime_cand_cnt < IME_CAND_MAX; i++) {
        const ime_entry_t *e = &ime_phrase_table[i];
        if (strncmp(e->py, ime_py, plen) != 0) continue;
        /* 词组整体作为一个候选(不拆字) */
        size_t wl = 0; while (e->hz[wl]) wl++;
        if (wl >= IME_CHAR_MAX) wl = IME_CHAR_MAX - 1;
        int dup = 0;
        for (int j = 0; j < ime_cand_cnt; j++) {
            if (strncmp(ime_cands[j], e->hz, wl) == 0 && ime_cands[j][wl] == 0) { dup = 1; break; }
        }
        if (!dup) {
            memcpy(ime_cands[ime_cand_cnt], e->hz, wl);
            ime_cands[ime_cand_cnt][wl] = 0;
            ime_cand_cnt++;
        }
    }
    /* V3.69: 第二遍扫简拼表 (输入 >=2 字母, 首字母缩写命中; 与全拼词组互补去重) */
    if (plen >= 2) {
        for (int i = 0; i < ime_short_entries && ime_cand_cnt < IME_CAND_MAX; i++) {
            const ime_entry_t *e = &ime_short_table[i];
            if (strncmp(e->py, ime_py, plen) != 0) continue;
            size_t wl = 0; while (e->hz[wl]) wl++;
            if (wl >= IME_CHAR_MAX) wl = IME_CHAR_MAX - 1;
            int dup = 0;
            for (int j = 0; j < ime_cand_cnt; j++) {
                if (strncmp(ime_cands[j], e->hz, wl) == 0 && ime_cands[j][wl] == 0) { dup = 1; break; }
            }
            if (!dup) {
                memcpy(ime_cands[ime_cand_cnt], e->hz, wl);
                ime_cands[ime_cand_cnt][wl] = 0;
                ime_cand_cnt++;
            }
        }
    }
    /* 第三遍扫单字表 */
    for (int i = 0; i < ime_table_entries; i++) {
        const ime_entry_t *e = &ime_table[i];
        if (strncmp(e->py, ime_py, plen) != 0) continue;
        /* 逐字拆出加入候选 */
        int wi = 0;
        while (ime_cand_cnt < IME_CAND_MAX) {
            char w[IME_CHAR_MAX];
            if (ime_split_word(e->hz, wi, w, IME_CHAR_MAX) != 0) break;
            /* 去重 */
            int dup = 0;
            for (int j = 0; j < ime_cand_cnt; j++) {
                if (strcmp(ime_cands[j], w) == 0) { dup = 1; break; }
            }
            if (!dup) {
                memcpy(ime_cands[ime_cand_cnt], w, sizeof(w));
                ime_cand_cnt++;
            }
            wi++;
        }
        if (ime_cand_cnt >= IME_CAND_MAX) break;
    }
    /* V3.39: 候选按历史频率降序稳定排序 (高频候选前移, 同频保持原序) */
    for (int i = 1; i < ime_cand_cnt; i++) {
        char tmp[IME_CHAR_MAX];
        memcpy(tmp, ime_cands[i], sizeof(tmp));
        int v = ime_freq_of(tmp);
        int j = i - 1;
        while (j >= 0 && ime_freq_of(ime_cands[j]) < v) {
            memcpy(ime_cands[j + 1], ime_cands[j], sizeof(ime_cands[j + 1]));
            j--;
        }
        memcpy(ime_cands[j + 1], tmp, sizeof(ime_cands[j + 1]));
    }
}

int ime_input(int key)
{
    ime_freq_load();   /* V3.42: 懒加载频率 */
    if (key == 0x08) {                    /* 退格删拼音 */
        if (ime_py_len > 0) {
            ime_py[--ime_py_len] = 0;
            ime_rebuild_cands();
            return 1;
        }
        return 0;                         /* 拼音空时退格交给上层 */
    }
    if (key >= 'a' && key <= 'z') {       /* 拼音字母 */
        if (ime_py_len < IME_PINYIN_MAX) {
            ime_py[ime_py_len++] = (char)key;
            ime_py[ime_py_len] = 0;
            ime_rebuild_cands();
        }
        return 1;
    }
    return 0;                             /* 数字/空格/其他交给上层 */
}

void ime_reset(void)
{
    ime_py_len = 0;
    ime_py[0] = 0;
    ime_cand_cnt = 0;
    ime_page = 0;                 /* V3.27: 复位回到第一页 */
}

const char *ime_pinyin(void)
{
    return ime_py;
}

int ime_pinyin_len(void)
{
    return ime_py_len;
}

int ime_candidate_count(void)
{
    return ime_cand_cnt;
}

/* V3.27: 取当前页第 idx 个候选 (idx 为页内偏移 0..IME_PAGE_SIZE-1) */
int ime_candidate_get(int idx, char *out, int cap)
{
    if (!out || cap <= 0) return -1;
    int gi = ime_page * IME_PAGE_SIZE + idx;
    if (idx < 0 || idx >= IME_PAGE_SIZE || gi >= ime_cand_cnt) return -1;
    int len = (int)strlen(ime_cands[gi]);
    if (len >= cap) len = cap - 1;
    memcpy(out, ime_cands[gi], (size_t)len);
    out[len] = 0;
    return 0;
}

/* V3.27: 翻页支持 */
int ime_page_index(void)
{
    return ime_page;
}

int ime_page_count(void)
{
    return (ime_cand_cnt + IME_PAGE_SIZE - 1) / IME_PAGE_SIZE;
}

int ime_page_next(void)
{
    int pc = ime_page_count();
    if (pc <= 1) return ime_page;
    ime_page = (ime_page + 1) % pc;
    return ime_page;
}

/* V4.28-E73: 按拼音串直接查询候选(供 shell readline TAB 内嵌 IME 集成)
 * 返回候选总数; 之后用 ime_candidate_get(idx, out, cap) 取候选 */
int ime_query_py(const char *py)
{
    size_t n = 0;
    while (py[n] && n < IME_PINYIN_MAX) n++;
    for (size_t i = 0; i < n; i++) ime_py[i] = py[i];
    ime_py[n] = 0;
    ime_py_len = (int)n;
    ime_rebuild_cands();
    return ime_cand_cnt;
}

int ime_commit_first(char *out, int cap)
{
    if (ime_cand_cnt == 0 || !out || cap <= 0) return 0;
    int len = (int)strlen(ime_cands[0]);
    if (len >= cap) len = cap - 1;
    memcpy(out, ime_cands[0], (size_t)len);
    out[len] = 0;
    return len;
}
