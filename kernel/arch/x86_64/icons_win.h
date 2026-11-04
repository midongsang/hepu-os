/* V0.13.9 P13: Windows 11 风格位图图标 (32x32 RGBA, 背景键控抠透明) */
#ifndef ICONS_WIN_H
#define ICONS_WIN_H

#include <stdint.h>

#define WIN_ICON_COUNT 21
#define WIN_ICON_W 32
#define WIN_ICON_H 32

#define WIN_ICON_FOLDER  0
#define WIN_ICON_TEXT    1
#define WIN_ICON_IMAGE   2
#define WIN_ICON_AUDIO   3
#define WIN_ICON_VIDEO   4
#define WIN_ICON_ARCHIVE 5
#define WIN_ICON_PDF     6
#define WIN_ICON_EXE     7
#define WIN_ICON_CODE    8
#define WIN_ICON_SHEET   9
#define WIN_ICON_FONT   10
#define WIN_ICON_SEARCH 11
#define WIN_ICON_SETTINGS 12
#define WIN_ICON_CLOCK  13
#define WIN_ICON_WEATHER 14
#define WIN_ICON_LOCATION 15
#define WIN_ICON_SHARE  16
#define WIN_ICON_TRASH  17
#define WIN_ICON_GLOBE  18
#define WIN_ICON_INFO   19
#define WIN_ICON_FILE   20  /* P19.5: 通用未知文件 (空白文档+折角) */

extern const uint8_t win_icons[WIN_ICON_COUNT][WIN_ICON_W * WIN_ICON_H * 4];

#endif
