/* ============================================================================
 * 赫普OS - 正则表达式库
 * V0.9.0: P5-4 第三方库移植
 * 简化版正则表达式引擎，支持常用语法
 * ========================================================================== */

#ifndef _REGEX_H
#define _REGEX_H

#include <stdint.h>
#include <stddef.h>

/* 正则表达式错误码 */
typedef enum {
    REGEX_OK = 0,
    REGEX_ERR_INVALID,
    REGEX_ERR_UNBALANCED_PAREN,
    REGEX_ERR_UNBALANCED_BRACKET,
    REGEX_ERR_TRAILING_BACKSLASH,
    REGEX_ERR_INVALID_ESCAPE,
    REGEX_ERR_INVALID_QUANTIFIER,
    REGEX_ERR_MEMORY
} regex_error_t;

/* 编译后的正则表达式 */
typedef struct {
    const char *pattern;     /* 原始模式 */
    char *compiled;          /* 编译后的内部表示 */
    size_t compiled_len;     /* 编译后长度 */
    regex_error_t error;     /* 编译错误 */
    int error_pos;           /* 错误位置 */
} regex_t;

/* 匹配结果 */
typedef struct {
    int matched;              /* 是否匹配 */
    size_t start;             /* 匹配起始位置 */
    size_t end;               /* 匹配结束位置 */
    size_t match_len;         /* 匹配长度 */
} regex_match_t;

/* 编译正则表达式 */
regex_error_t regex_compile(regex_t *regex, const char *pattern);

/* 释放正则表达式 */
void regex_free(regex_t *regex);

/* 在文本中搜索匹配 */
int regex_search(const regex_t *regex, const char *text, regex_match_t *match);

/* 检查文本是否完全匹配 */
int regex_match(const regex_t *regex, const char *text);

/* 替换所有匹配 */
size_t regex_replace(const regex_t *regex, const char *text,
                     const char *replacement, char *output, size_t output_size);

/* 获取错误描述 */
const char *regex_error_string(regex_error_t error);

/* 便捷函数：一次性搜索 */
int regex_match_simple(const char *pattern, const char *text);

#endif /* _REGEX_H */
