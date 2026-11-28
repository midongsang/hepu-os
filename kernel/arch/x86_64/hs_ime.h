#ifndef HS_IME_H
#define HS_IME_H

/* 桌面输入法适配层：EN/ZH 双模 + 候选条。
 * 核心拼音逻辑复用 kernel/shared/ime/ime.c，不在这里重复一份词库。 */

#define HS_IME_MODE_EN 0
#define HS_IME_MODE_ZH 1

void hs_ime_init(void);
int  hs_ime_mode(void);
void hs_ime_set_mode(int mode);
void hs_ime_toggle(void);
void hs_ime_cancel(void);
int  hs_ime_composing(void);

/* 在 hsinput 的快捷键/Esc 分支之前调用；返回 1=已消费。 */
int  hs_ime_handle_key(int key, int mod);

/* 在桌面帧末尾、present 之前绘制候选条。 */
void hs_ime_render(void);

/* 鼠标点击候选条；返回 1=已消费（点中候选/条内空白）。
 * 必须在窗口和顶栏命中之前调用，否则候选浮层会穿透到下层窗口。 */
int  hs_ime_click(int x, int y);
int  hs_ime_mouse_release(void);

#endif
