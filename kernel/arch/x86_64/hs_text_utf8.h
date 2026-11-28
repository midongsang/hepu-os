#ifndef HS_TEXT_UTF8_H
#define HS_TEXT_UTF8_H

#include <stdint.h>

/* 桌面文本输入的最小 UTF-8 工具。
 * 只负责按码点前进/后退、显示列宽和有界追加，不拥有缓冲区。 */
int hs_utf8_len_at(const char *s, int remain);
int hs_utf8_prev_len(const char *s, int len);
int hs_utf8_next_pos(const char *s, int len, int pos);
int hs_utf8_cols(const char *s, int len);
int hs_utf8_is_wide(uint32_t cp);
int hs_utf8_append(char *buf, int cap, int len, const char *s);
int hs_utf8_backspace(char *buf, int len);

#endif
