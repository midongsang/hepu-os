/* ============================================================================
 * 赫普OS - 统一应用注册表 (V2.51)
 *
 * 应用双模式注册：
 *  - 手机模式 (mobile.c) 与桌面模式 (desktop.c) 共享同一份应用清单；
 *  - 每个应用声明模式标志：仅手机 / 仅桌面 / 双模式；
 *  - 双模式应用可同时从手机桌面与桌面启动器进入。
 *
 * 驱动消费方式：
 *  - 手机模式：appreg_mobile_count() 取可启动数，appreg_mobile_launch(slot) 启动；
 *  - 桌面模式：appreg_desktop_count() 取可启动数，appreg_desktop_launch(slot) 启动。
 *  - 实际启动动作由消费方注册的 launcher 回调执行（解耦注册表与 UI 模块）。
 * ========================================================================== */
#ifndef HEPU_APPREG_H
#define HEPU_APPREG_H

#include <stdint.h>

/* ---- 应用模式标志 ---- */
#define HEPU_APP_MOBILE   0x01   /* 手机模式可启动 */
#define HEPU_APP_DESKTOP  0x02   /* 桌面模式可启动 */
#define HEPU_APP_BOTH     (HEPU_APP_MOBILE | HEPU_APP_DESKTOP)

/* 应用启动回调：slot 为模式侧网格序号；返回 0=正常返回, 非0=错误 */
typedef int (*hepu_app_launch_fn)(int slot);

/* ---- 应用描述 ---- */
typedef struct {
    uint16_t        id;          /* 注册表内唯一 id */
    uint8_t         mode;        /* HEPU_APP_* 标志 */
    uint8_t         mobile_slot; /* 手机模式网格槽位 (0 基) */
    uint8_t         desktop_slot;/* 桌面模式图标槽位 (0 基) */
    const char     *name_zh;     /* 中文名 */
    const char     *name_en;     /* 英文名 */
} hepu_app_t;

/* ---- 注册表 API ---- */
int                appreg_init(void);
int                appreg_count(void);
const hepu_app_t  *appreg_get(int idx);

/* 模式侧 launcher 注入 (由 mobile.c / desktop.c 在初始化时注册) */
void  appreg_set_mobile_launcher(hepu_app_launch_fn fn);
void  appreg_set_desktop_launcher(hepu_app_launch_fn fn);

/* 手机模式侧查询/启动 */
int   appreg_mobile_count(void);
int   appreg_mobile_slot_to_idx(int slot);      /* 手机槽位 → 注册表 idx */
const char *appreg_mobile_name(int slot);       /* 手机槽位中文名 */
int   appreg_mobile_launch(int slot);           /* 启动手机槽位应用 */

/* 桌面模式侧查询/启动 */
int   appreg_desktop_count(void);
int   appreg_desktop_slot_to_idx(int slot);
const char *appreg_desktop_name(int slot);
int   appreg_desktop_launch(int slot);

#endif /* HEPU_APPREG_H */
