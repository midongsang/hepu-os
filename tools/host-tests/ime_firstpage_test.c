#include <stdio.h>
#include <string.h>
#include "ime.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s\n", #c); fails++; } } while (0)

/* ime_core 的频率持久化桩：主机测试不碰真实磁盘。 */
int ide_ext2_read_file(const char *dirpath, const char *name, char *buf, int max) {
    (void)dirpath; (void)name; (void)buf; (void)max; return -1;
}
int ide_ext2_writefile(const char *name, const char *data) {
    (void)name; (void)data; return 0;
}
int ide_ext2_mkdir(const char *path) { (void)path; return 0; }
int ide_ext2_sync(void) { return 0; }

static void type_pinyin(const char *py)
{
    for (int i = 0; py[i]; i++) ime_input(py[i]);
}

static int first_page_has(const char *want)
{
    char cand[IME_CHAR_MAX];
    int page_size = ime_page_size();
    for (int i = 0; i < page_size; i++) {
        if (ime_candidate_get(i, cand, sizeof(cand)) != 0) break;
        if (strcmp(cand, want) == 0) return 1;
    }
    return 0;
}

static int load_rows(const char *path, char rows[][128], int max_rows)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("FAIL: cannot open %s\n", path);
        return -1;
    }
    int n = 0;
    char line[256];
    while (n < max_rows && fgets(line, sizeof(line), f)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        char *value = tab + 1;
        char *nl = strchr(value, '\n');
        if (nl) *nl = 0;
        char *cr = strchr(value, '\r');
        if (cr) *cr = 0;
        snprintf(rows[n], 128, "%s\t%s", line, value);
        n++;
    }
    fclose(f);
    return n;
}

int main(void)
{
    char rows[600][128];
    int n = load_rows("tools/ime-data/common-500.tsv", rows, 500);
    if (n != 500) {
        printf("FAIL: common-500 rows=%d (want 500)\n", n);
        return 1;
    }

    int char_hits = 0;
    int miss_chars = 0;
    for (int r = 0; r < n; r++) {
        char *tab = strchr(rows[r], '\t');
        *tab = 0;
        const char *py = rows[r];
        const char *hz = tab + 1;
        ime_reset();
        type_pinyin(py);
        if (first_page_has(hz)) char_hits++;
        else {
            if (miss_chars < 8) printf("  miss char %s (%s)\n", hz, py);
            miss_chars++;
        }
    }
    printf("  common-500 first page: %d/500 (%.1f%%), miss=%d\n",
           char_hits, char_hits * 100.0 / n, miss_chars);
    CHECK(char_hits >= 470);

    int pn = load_rows("tools/ime-data/common-100-phrases.tsv", rows, 100);
    if (pn != 100) {
        printf("FAIL: common-100-phrases rows=%d (want 100)\n", pn);
        return 1;
    }
    int phrase_hits = 0;
    for (int r = 0; r < pn; r++) {
        char *tab = strchr(rows[r], '\t');
        *tab = 0;
        const char *py = rows[r];
        const char *hz = tab + 1;
        ime_reset();
        type_pinyin(py);
        if (first_page_has(hz)) phrase_hits++;
    }
    printf("  common-100 phrases first page: %d/100\n", phrase_hits);
    CHECK(phrase_hits == 100);

    printf("ime firstpage: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
