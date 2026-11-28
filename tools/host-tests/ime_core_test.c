#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s\n", #c); fails++; } } while (0)
#include <string.h>
#include "ime.h"

/* ime_core 的频率持久化桩：主机测试不碰真实磁盘。 */
int ide_ext2_read_file(const char *dirpath, const char *name, char *buf, int max) {
    (void)dirpath; (void)name; (void)buf; (void)max; return -1;
}
int ide_ext2_writefile(const char *name, const char *data) {
    (void)name; (void)data; return 0;
}
int ide_ext2_mkdir(const char *path) { (void)path; return 0; }
int ide_ext2_sync(void) { return 0; }

static void type(const char *s) {
    for (int i = 0; s[i]; i++) {
        int r = ime_input(s[i]);
        CHECK(r == 1);
    }
}

static void test_nihao(void) {
    ime_reset();
    type("nihao");
    CHECK(ime_pinyin_len() == 5);
    CHECK(ime_candidate_count() > 0);
    char c[IME_CHAR_MAX];
    CHECK(ime_candidate_get(0, c, sizeof(c)) == 0);
    CHECK(strcmp(c, "你好") == 0);
    char out[IME_CHAR_MAX];
    CHECK(ime_commit_first(out, sizeof(out)) > 0);
    CHECK(strcmp(out, "你好") == 0);
    ime_reset();   /* 上屏后由 UI 层复位；核心 ime_commit_first 保持无副作用 */
    CHECK(ime_pinyin_len() == 0);
    printf("  nihao -> %s\n", out);
}

static void test_zhongguo(void) {
    ime_reset();
    type("zhongguo");
    CHECK(ime_pinyin_len() == 8);
    char c[IME_CHAR_MAX];
    CHECK(ime_candidate_get(0, c, sizeof(c)) == 0);
    CHECK(strcmp(c, "中国") == 0);
    printf("  zhongguo -> %s\n", c);
}

static void test_backspace(void) {
    ime_reset();
    type("ni");
    CHECK(ime_input(8) == 1);
    CHECK(ime_pinyin_len() == 1);
    CHECK(strcmp(ime_pinyin(), "n") == 0);
    printf("  backspace keeps one pinyin letter\n");
}

int main(void) {
    test_nihao();
    test_zhongguo();
    test_backspace();
    puts("ime core: ok");
    return fails;
}
