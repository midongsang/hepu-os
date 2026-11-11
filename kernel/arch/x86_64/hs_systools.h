/* ============================================================================
 * 赫普OS - 系统工具窗口 (V0.55)
 * 设备管理器 / 磁盘管理 / 日志查看器
 * ========================================================================== */
#ifndef HS_SYSTOOLS_H
#define HS_SYSTOOLS_H

#include "hpdesk.h"

/* 窗口创建 */
hs_window_t *hs_devmgr_create(void);
hs_window_t *hs_diskmgr_create(void);
hs_window_t *hs_logview_create(void);

/* 键盘/滚轮/点击 (hpdesk.c 调用; 返回 1=已消费) */
int hs_systools_handle_key(int key);
int hs_systools_handle_wheel(int delta);
int hs_systools_click(int x, int y);

#endif /* HS_SYSTOOLS_H */
