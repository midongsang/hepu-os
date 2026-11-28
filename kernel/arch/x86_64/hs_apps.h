#ifndef HS_APPS_H
#define HS_APPS_H

#include "hpdesk.h"

/* 应用窗口创建 */
hs_window_t *hs_terminal_create(void);
void hs_terminal_run_line(const char *line);
hs_window_t *hs_files_create(void);
hs_window_t *hs_taskmgr_create(void);
hs_window_t *hs_editor_create(void);
hs_window_t *hs_editor_open_file(const char *path);   /* 用编辑器打开指定路径 */

/* 编辑器: 快捷键请求 (返回 1=由编辑器消费) */
int hs_apps_editor_save(void);
int hs_apps_editor_close(void);

/* 键盘/滚轮转发 (hpdesk.c 调用; 返回 1=已消费) */
int hs_apps_handle_key(int key);
void hs_apps_handle_wheel(int delta);

/* 文本输入目标契约 (M1 IME) */
int hs_apps_text_focus(void);
int hs_apps_text_ime_allowed(void);
int hs_apps_text_insert_utf8(const char *s);
int hs_apps_text_backspace_utf8(void);

#endif
