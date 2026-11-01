/* ============================================================================
 * 赫普OS - 正则表达式实现
 * V0.9.0: P5-4 第三方库移植
 * 简化版正则表达式引擎，支持常用语法
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "regex.h"

/* 简单的内存操作函数（避免依赖系统库） */
static void *regex_regex_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static size_t regex_regex_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* 简单的内存分配（避免依赖系统malloc） */
static void *regex_malloc(size_t size)
{
    /* 简化：使用静态缓冲区池 */
    static uint8_t pool[65536];
    static size_t pool_pos = 0;
    if (pool_pos + size > sizeof(pool)) return NULL;
    void *ptr = pool + pool_pos;
    pool_pos += size;
    return ptr;
}

/* 检查字符是否匹配字符类 */
static int char_class_match(char c, const char *class_start, const char *class_end)
{
    int negate = 0;
    const char *p = class_start;

    if (p < class_end && *p == '^') {
        negate = 1;
        p++;
    }

    int matched = 0;
    while (p < class_end) {
        if (p + 2 < class_end && p[1] == '-') {
            /* 范围 */
            if (c >= p[0] && c <= p[2]) {
                matched = 1;
                break;
            }
            p += 3;
        } else {
            if (c == *p) {
                matched = 1;
                break;
            }
            p++;
        }
    }

    return negate ? !matched : matched;
}

/* 检查转义字符类 */
static int escape_match(char c, char esc)
{
    switch (esc) {
        case 'd': return (c >= '0' && c <= '9');
        case 'D': return !(c >= '0' && c <= '9');
        case 'w': return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_');
        case 'W': return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_');
        case 's': return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
        case 'S': return !(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
        default: return (c == esc);
    }
}

/* 递归匹配函数（简化版NFA） */
static int match_here(const char *pattern, const char *text,
                      const char **end_ptr);

/* 匹配星号/加号/问号量词 */
static int match_quantifier(char c, int is_char_class,
                            const char *class_start, const char *class_end,
                            char esc, int is_escape,
                            const char *pattern, const char *text,
                            const char **end_ptr,
                            int min_count, int max_count)
{
    int count = 0;
    const char *p = text;

    /* 贪婪匹配 */
    while (max_count < 0 || count < max_count) {
        if (*p == '\0') break;
        int matched = 0;
        if (is_char_class) {
            matched = char_class_match(*p, class_start, class_end);
        } else if (is_escape) {
            matched = escape_match(*p, esc);
        } else if (c == '.') {
            matched = (*p != '\n');
        } else {
            matched = (*p == c);
        }
        if (!matched) break;
        p++;
        count++;
    }

    /* 回溯：尝试不同的匹配长度 */
    while (count >= min_count) {
        if (match_here(pattern, text + count, end_ptr)) {
            return 1;
        }
        count--;
    }
    return 0;
}

/* 递归匹配函数 */
static int match_here(const char *pattern, const char *text, const char **end_ptr)
{
    if (*pattern == '\0') {
        if (end_ptr) *end_ptr = text;
        return 1;
    }

    if (*pattern == '$' && pattern[1] == '\0') {
        if (*text == '\0') {
            if (end_ptr) *end_ptr = text;
            return 1;
        }
        return 0;
    }

    /* 分组 */
    if (*pattern == '(') {
        const char *group_end = pattern + 1;
        int depth = 1;
        while (*group_end && depth > 0) {
            if (*group_end == '(') depth++;
            else if (*group_end == ')') depth--;
            if (depth > 0) group_end++;
        }
        if (*group_end != ')') return 0;

        /* 检查是否有 | （或操作） */
        const char *alt = pattern + 1;
        int alt_depth = 0;
        while (alt < group_end) {
            if (*alt == '(') alt_depth++;
            else if (*alt == ')') alt_depth--;
            else if (*alt == '|' && alt_depth == 0) {
                /* 尝试第一个分支 */
                size_t branch_len = alt - (pattern + 1);
                char *branch = (char*)regex_malloc(branch_len + 1);
                if (!branch) return 0;
                regex_memcpy(branch, pattern + 1, branch_len);
                branch[branch_len] = '\0';

                /* 构建剩余模式 */
                const char *rest = group_end + 1;
                size_t rest_len = regex_strlen(rest);
                char *combined = (char*)regex_malloc(branch_len + rest_len + 1);
                if (!combined) return 0;
                regex_memcpy(combined, branch, branch_len);
                regex_memcpy(combined + branch_len, rest, rest_len + 1);

                if (match_here(combined, text, end_ptr)) return 1;

                /* 尝试第二个分支 */
                size_t branch2_len = group_end - (alt + 1);
                char *branch2 = (char*)regex_malloc(branch2_len + 1);
                if (!branch2) return 0;
                regex_memcpy(branch2, alt + 1, branch2_len);
                branch2[branch2_len] = '\0';

                char *combined2 = (char*)regex_malloc(branch2_len + rest_len + 1);
                if (!combined2) return 0;
                regex_memcpy(combined2, branch2, branch2_len);
                regex_memcpy(combined2 + branch2_len, rest, rest_len + 1);

                return match_here(combined2, text, end_ptr);
            }
            alt++;
        }

        /* 没有 |，直接匹配分组内容 */
        size_t group_len = group_end - (pattern + 1);
        char *group = (char*)regex_malloc(group_len + 1);
        if (!group) return 0;
        regex_memcpy(group, pattern + 1, group_len);
        group[group_len] = '\0';

        const char *rest = group_end + 1;
        size_t rest_len = regex_strlen(rest);
        char *combined = (char*)regex_malloc(group_len + rest_len + 1);
        if (!combined) return 0;
        regex_memcpy(combined, group, group_len);
        regex_memcpy(combined + group_len, rest, rest_len + 1);

        return match_here(combined, text, end_ptr);
    }

    /* 字符类 */
    if (*pattern == '[') {
        const char *class_end = pattern + 1;
        while (*class_end && *class_end != ']') class_end++;
        if (*class_end != ']') return 0;

        char next = class_end[1];
        if (next == '*') {
            return match_quantifier(0, 1, pattern + 1, class_end, 0, 0,
                                    class_end + 2, text, end_ptr, 0, -1);
        } else if (next == '+') {
            return match_quantifier(0, 1, pattern + 1, class_end, 0, 0,
                                    class_end + 2, text, end_ptr, 1, -1);
        } else if (next == '?') {
            return match_quantifier(0, 1, pattern + 1, class_end, 0, 0,
                                    class_end + 2, text, end_ptr, 0, 1);
        } else {
            if (*text == '\0') return 0;
            if (!char_class_match(*text, pattern + 1, class_end)) return 0;
            return match_here(class_end + 1, text + 1, end_ptr);
        }
    }

    /* 转义字符 */
    if (*pattern == '\\' && pattern[1]) {
        char esc = pattern[1];
        char next = pattern[2];
        if (next == '*') {
            return match_quantifier(0, 0, NULL, NULL, esc, 1,
                                    pattern + 3, text, end_ptr, 0, -1);
        } else if (next == '+') {
            return match_quantifier(0, 0, NULL, NULL, esc, 1,
                                    pattern + 3, text, end_ptr, 1, -1);
        } else if (next == '?') {
            return match_quantifier(0, 0, NULL, NULL, esc, 1,
                                    pattern + 3, text, end_ptr, 0, 1);
        } else {
            if (*text == '\0') return 0;
            if (!escape_match(*text, esc)) return 0;
            return match_here(pattern + 2, text + 1, end_ptr);
        }
    }

    /* 普通字符或点 */
    char c = *pattern;
    char next = pattern[1];
    if (next == '*') {
        return match_quantifier(c, 0, NULL, NULL, 0, 0,
                                pattern + 2, text, end_ptr, 0, -1);
    } else if (next == '+') {
        return match_quantifier(c, 0, NULL, NULL, 0, 0,
                                pattern + 2, text, end_ptr, 1, -1);
    } else if (next == '?') {
        return match_quantifier(c, 0, NULL, NULL, 0, 0,
                                pattern + 2, text, end_ptr, 0, 1);
    } else {
        if (*text == '\0') return 0;
        if (c != '.' && c != *text) return 0;
        return match_here(pattern + 1, text + 1, end_ptr);
    }
}

/* ============================================================================
 * 公共API
 * ========================================================================== */

regex_error_t regex_compile(regex_t *regex, const char *pattern)
{
    if (!regex || !pattern) return REGEX_ERR_INVALID;

    regex->pattern = pattern;
    regex->compiled = NULL;
    regex->compiled_len = 0;
    regex->error = REGEX_OK;
    regex->error_pos = -1;

    /* 简化：直接使用原始模式（不做预编译优化） */
    regex->compiled = (char*)pattern;
    regex->compiled_len = regex_strlen(pattern);

    /* 基本语法检查 */
    int paren_depth = 0;
    int bracket_depth = 0;
    for (size_t i = 0; pattern[i]; i++) {
        if (pattern[i] == '\\' && pattern[i+1]) {
            i++;
            continue;
        }
        if (pattern[i] == '(') paren_depth++;
        else if (pattern[i] == ')') paren_depth--;
        else if (pattern[i] == '[') bracket_depth++;
        else if (pattern[i] == ']') bracket_depth--;

        if (paren_depth < 0) {
            regex->error = REGEX_ERR_UNBALANCED_PAREN;
            regex->error_pos = i;
            return regex->error;
        }
    }
    if (paren_depth != 0) {
        regex->error = REGEX_ERR_UNBALANCED_PAREN;
        return regex->error;
    }
    if (bracket_depth != 0) {
        regex->error = REGEX_ERR_UNBALANCED_BRACKET;
        return regex->error;
    }

    return REGEX_OK;
}

void regex_free(regex_t *regex)
{
    if (regex) {
        regex->compiled = NULL;
    }
}

int regex_search(const regex_t *regex, const char *text, regex_match_t *match)
{
    if (!regex || !text) return 0;

    const char *pattern = regex->compiled ? regex->compiled : regex->pattern;
    int anchored = (pattern[0] == '^');

    if (anchored) {
        const char *end_ptr = NULL;
        if (match_here(pattern + 1, text, &end_ptr)) {
            if (match) {
                match->matched = 1;
                match->start = 0;
                match->end = end_ptr - text;
                match->match_len = match->end - match->start;
            }
            return 1;
        }
        return 0;
    }

    for (size_t i = 0; text[i]; i++) {
        const char *end_ptr = NULL;
        if (match_here(pattern, text + i, &end_ptr)) {
            if (match) {
                match->matched = 1;
                match->start = i;
                match->end = end_ptr - text;
                match->match_len = match->end - match->start;
            }
            return 1;
        }
    }
    return 0;
}

int regex_match(const regex_t *regex, const char *text)
{
    if (!regex || !text) return 0;

    const char *pattern = regex->compiled ? regex->compiled : regex->pattern;
    const char *end_ptr = NULL;

    if (pattern[0] == '^') {
        return match_here(pattern + 1, text, &end_ptr) && *end_ptr == '\0';
    }

    return match_here(pattern, text, &end_ptr) && *end_ptr == '\0';
}

size_t regex_replace(const regex_t *regex, const char *text,
                     const char *replacement, char *output, size_t output_size)
{
    if (!regex || !text || !replacement || !output || output_size == 0) return 0;

    size_t out_pos = 0;
    size_t text_pos = 0;
    regex_match_t match;

    while (text[text_pos]) {
        if (regex_search(regex, text + text_pos, &match) && match.matched) {
            /* 复制匹配前的文本 */
            size_t copy_len = match.start;
            if (out_pos + copy_len >= output_size) break;
            regex_memcpy(output + out_pos, text + text_pos, copy_len);
            out_pos += copy_len;

            /* 复制替换文本 */
            size_t repl_len = regex_strlen(replacement);
            if (out_pos + repl_len >= output_size) break;
            regex_memcpy(output + out_pos, replacement, repl_len);
            out_pos += repl_len;

            text_pos += match.end;
        } else {
            if (out_pos + 1 >= output_size) break;
            output[out_pos++] = text[text_pos++];
        }
    }

    if (out_pos < output_size) output[out_pos] = '\0';
    return out_pos;
}

const char *regex_error_string(regex_error_t error)
{
    switch (error) {
        case REGEX_OK: return "No error";
        case REGEX_ERR_INVALID: return "Invalid argument";
        case REGEX_ERR_UNBALANCED_PAREN: return "Unbalanced parentheses";
        case REGEX_ERR_UNBALANCED_BRACKET: return "Unbalanced brackets";
        case REGEX_ERR_TRAILING_BACKSLASH: return "Trailing backslash";
        case REGEX_ERR_INVALID_ESCAPE: return "Invalid escape sequence";
        case REGEX_ERR_INVALID_QUANTIFIER: return "Invalid quantifier";
        case REGEX_ERR_MEMORY: return "Out of memory";
        default: return "Unknown error";
    }
}

int regex_match_simple(const char *pattern, const char *text)
{
    regex_t regex;
    if (regex_compile(&regex, pattern) != REGEX_OK) return 0;
    int result = regex_match(&regex, text);
    regex_free(&regex);
    return result;
}
