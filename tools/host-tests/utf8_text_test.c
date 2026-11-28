#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL: %s\n", #c); fails++; } } while (0)
#include <string.h>
#include "hs_text_utf8.h"

static void test_len_and_cols(void) {
    const char *s = "a中🙂";
    CHECK(hs_utf8_len_at(s, 20) == 1);
    CHECK(hs_utf8_len_at(s + 1, 18) == 3);
    CHECK(hs_utf8_len_at(s + 4, 15) == 4);
    CHECK(hs_utf8_cols(s, (int)strlen(s)) == 1 + 2 + 2);
    printf("  cols(a中🙂)=%d\n", hs_utf8_cols(s, (int)strlen(s)));
}

static void test_prev(void) {
    const char *s = "a中";
    int len = (int)strlen(s);
    CHECK(hs_utf8_prev_len(s, len) == 3);
    CHECK(hs_utf8_prev_len(s, len - 3) == 1);
    /* 不完整 UTF-8：只退 1 字节，避免死循环。 */
    CHECK(hs_utf8_prev_len("\xe4\xb8", 2) == 1);
    printf("  prev_len handles CJK and broken tails\n");
}

static void test_append_backspace(void) {
    char buf[16] = "a";
    int len = 1;
    len = hs_utf8_append(buf, sizeof(buf), len, "中");
    CHECK(len == 4);
    CHECK(strcmp(buf, "a中") == 0);
    len = hs_utf8_backspace(buf, len);
    CHECK(len == 1);
    CHECK(strcmp(buf, "a") == 0);
    /* 有界：cap=5 放不下第二个汉字，不能写越界。 */
    char small[5] = "a";
    int n = hs_utf8_append(small, sizeof(small), 1, "中中");
    CHECK(n == 4);
    CHECK(strcmp(small, "a中") == 0);
    printf("  append/backspace bounded and codepoint-safe\n");
}

static void test_controls(void) {
    CHECK(hs_utf8_cols("\x01", 1) == 0);
    CHECK(hs_utf8_cols("\xcc\x81", 2) == 0); /* U+0301 组合重音 */
    CHECK(hs_utf8_is_wide(0x4E00) == 1);
    CHECK(hs_utf8_is_wide('A') == 0);
    printf("  controls/combining/wide classification\n");
}

int main(void) {
    test_len_and_cols();
    test_prev();
    test_append_backspace();
    test_controls();
    puts("utf8 text: ok");
    return fails;
}
