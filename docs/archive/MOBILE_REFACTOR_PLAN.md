# mobile.c 重构拆分计划

## 当前状态
- 文件：`kernel/sched/mobile.c`
- 大小：4668行
- 已拆分：mobile_calc.c、mobile_clock.c、mobile_music.c、mobile_power.c

## 功能模块清单

| 模块 | 函数 | 行范围 | 大小 | 优先级 |
|------|------|--------|------|--------|
| 主界面/手机桌面 | mobile_run_phone | 826-1289 | ~460行 | P1 |
| 短信编写 | mobile_run_compose | 1289-1533 | ~245行 | P2 |
| 对话/会话 | mobile_run_conversation | 1533-1939 | ~405行 | P2 |
| 通讯录 | mobile_run_contacts | 1939-2435 | ~495行 | P1 |
| 短信主界面 | mobile_run_sms | 2435-2766 | ~330行 | P2 |
| 设置 | mobile_run_settings | 2766-2992 | ~225行 | P1 |
| 权限管理 | mobile_run_perm_mgr | 2992-3109 | ~117行 | P3 |
| 隐私看板 | mobile_run_privacy | 3109-3164 | ~55行 | P3 |
| 相机 | mobile_run_camera | 3164-3482 | ~318行 | P1 |
| 相册 | mobile_run_album | 3482-3711 | ~229行 | P2 |
| 书签 | mobile_run_bookmarks | 3711-3875 | ~164行 | P3 |
| 浏览器 | mobile_run_browser | 3875-3978 | ~103行 | P2 |
| 终端 | mobile_run_terminal | 3978-4134 | ~156行 | P3 |
| 文件管理 | mobile_run_files | 4134-4369 | ~235行 | P2 |
| 关于 | mobile_run_about | 4369-4394 | ~25行 | P3 |
| AI助手 | mobile_run_ai | 4394-4668 | ~274行 | P1 |

## 拆分原则

### 1. 共享变量处理
以下变量是全局共享的，保留在mobile.c中：
- `mob_page`, `mob_running`, `mob_focus`, `mob_notify_open`
- `sms_unread_count`, `mob_esc_arm`, `mob_esc_left`
- `accent_idx`, `ui_tron`, `sys_lang_en`, `mob_accent`
- `mob_time12`, `mob_invert`, `mob_datefmt`
- `mob_app_names`, `mob_app_colors`, `mob_app_ids`

### 2. 共享函数处理
以下UI辅助函数保留在mobile.c中：
- `ui_title()`, `ui_draw_app_bg()`, `mob_time_str()`
- `lang_save()`, `lang_load()`

### 3. 拆分步骤
1. 创建新的模块文件（如mobile_sms.c）
2. 把对应函数复制到新文件
3. 在新文件中添加必要的#include和extern声明
4. 在mobile.c中删除对应函数定义
5. 在mobile.h中添加函数声明
6. 更新Makefile，添加新文件到编译列表
7. 编译验证，确保没有链接错误

## 推荐拆分顺序（从易到难）

### P3（最简单，先练手）
1. **mobile_about.c** - 关于页面（25行，最简单）
2. **mobile_privacy.c** - 隐私看板（55行）
3. **mobile_browser.c** - 浏览器（103行）
4. **mobile_perm_mgr.c** - 权限管理（117行）
5. **mobile_bookmarks.c** - 书签（164行）
6. **mobile_terminal.c** - 终端（156行）

### P2（中等复杂度）
7. **mobile_sms.c** - 短信相关（compose+conversation+sms，~980行）
8. **mobile_album.c** - 相册（229行）
9. **mobile_files.c** - 文件管理（235行）
10. **mobile_settings.c** - 设置（225行）

### P1（最复杂，依赖多）
11. **mobile_contacts.c** - 通讯录（495行）
12. **mobile_camera.c** - 相机（318行）
13. **mobile_ai.c** - AI助手（274行）
14. **mobile_phone.c** - 主界面（460行，最后拆）

## 注意事项

1. **不要一次性拆分太多**，每次只拆一个模块，编译验证通过后再拆下一个
2. **共享变量用extern声明**，不要在多个文件中定义
3. **新文件要#include "mobile.h"**，获取共享变量和函数声明
4. **Makefile要更新**，把新文件加入SRCS列表
5. **编译验证**，确保没有未定义引用或重复定义

## 已完成拆分

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 关于 | mobile_about.c | ~35行 | ✅ 已完成 |
| 隐私看板 | mobile_privacy.c | ~55行 | ✅ 已完成 |

## 待拆分模块

- 权限管理（mobile_perm_mgr.c）
- 书签（mobile_bookmarks.c）
- 终端（mobile_terminal.c）
- 浏览器（mobile_browser.c，依赖较多）
- 相册（mobile_album.c）
- 文件管理（mobile_files.c）
- 设置（mobile_settings.c）
- 通讯录（mobile_contacts.c）
- 相机（mobile_camera.c）
- AI助手（mobile_ai.c）
- 短信相关（mobile_sms.c）
- 主界面（mobile_phone.c）

## 拆分经验

1. **共享变量用extern声明**，不要在多个文件中定义
2. **新文件要#include "mobile.h"**，获取共享变量和函数声明
3. **Makefile自动收集所有.c文件**，不需要手动添加
4. **从最简单的模块开始**，about（25行）→ privacy（55行）→ 逐步复杂
5. **每次只拆一个模块**，编译验证通过后再拆下一个

## 已完成拆分（更新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 关于 | mobile_about.c | ~35行 | ✅ 已完成 |
| 隐私看板 | mobile_privacy.c | ~55行 | ✅ 已完成 |
| 终端 | mobile_terminal.c | ~45行 | ✅ 已完成 |

mobile.c 从 4668行 → 4554行（减少114行）

## 已完成拆分（最终）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 关于 | mobile_about.c | ~35行 | ✅ 已完成 |
| 隐私看板 | mobile_privacy.c | ~55行 | ✅ 已完成 |
| 终端 | mobile_terminal.c | ~45行 | ✅ 已完成 |

mobile.c 从 4668行 → 4565行（减少103行）

## 暂不拆分的模块（依赖复杂）

- **perm_mgr**：和privacy模块共享pkg_short函数
- **bookmarks**：依赖太多web相关变量和函数
- **browser**：依赖太多web相关变量和函数
- **files**：依赖太多ext2文件系统函数和全局变量

## 后续拆分建议

1. 先把共享函数（pkg_short、perm_en等）提取到mobile_common.c
2. 然后再拆分perm_mgr、privacy等模块
3. 复杂模块（files、browser、camera）最后拆分

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | ~80行 | ✅ 已提取 |
| 关于 | mobile_about.c | ~35行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | ~55行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | ~45行 | ✅ 已拆分 |
| 权限管理 | mobile_perm_mgr.c | ~105行 | ✅ 已拆分 |

mobile.c 从 4668行 → ~4350行（减少约318行）

## 共享函数提取

已提取到 mobile_common.c：
- perm_en：权限名转英文
- perm_grant_name：授权状态名
- pkg_short：包名提取短名
- privacy_app_name：应用名查找

## 后续拆分建议

1. **继续拆分中等模块**：bookmarks、album、settings
2. **最后拆分复杂模块**：files、browser、camera、phone
3. **注意依赖关系**：拆分前先检查共享函数和变量

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理 | mobile_perm_mgr.c | 128行 | ✅ 已拆分 |

mobile.c 从 4668行 → 4398行（减少270行）

## 暂不拆分的模块（依赖复杂/交错）

- **browser/bookmarks**：和权限相关函数交错在一起，需要先分离权限函数
- **files**：依赖太多ext2文件系统函数和全局变量
- **camera/album**：共享很多相机相关变量和函数

## 后续拆分建议

1. **先分离权限相关函数**：mobile_permission_prompt、mobile_perm_request、mobile_perm_denied_page
2. **再拆分browser模块**：分离后browser模块就独立了
3. **拆分camera/album模块**：这两个模块紧密相关，可以一起拆分
4. **最后拆分phone/sms模块**：最复杂的模块

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理+弹窗 | mobile_perm_mgr.c | 212行 | ✅ 已拆分 |

mobile.c 从 4668行 → 4314行（减少354行）

## 权限相关函数分离

已分离到 mobile_perm_mgr.c：
- mobile_permission_prompt：权限请求弹窗
- mobile_perm_request：通用权限请求
- mobile_perm_denied_page：权限被拒提示页
- mobile_perm_asked：会话内已询问标记

## 后续拆分建议

1. **拆分browser模块**：权限函数分离后，browser模块就独立了
2. **拆分camera/album模块**：这两个模块紧密相关，可以一起拆分
3. **最后拆分phone/sms模块**：最复杂的模块

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理+弹窗 | mobile_perm_mgr.c | 215行 | ✅ 已拆分 |
| 浏览器+书签 | mobile_browser.c | 228行 | ✅ 已拆分 |

mobile.c 从 4668行 → 4085行（减少583行）

## browser模块拆分内容

已拆分到 mobile_browser.c：
- web_url, web_url_len, web_page（全局变量）
- web_site_keys, web_site_titles, web_site_line2（内置站点数据）
- web_bm_names, web_bm_count（书签数据）
- web_match（URL站点匹配）
- web_bookmarks_load, web_bookmark_add, web_bookmark_del（书签管理）
- mobile_run_bookmarks（书签列表页）
- mobile_draw_webpage（网页绘制）
- mobile_run_browser（浏览器主界面）

## 后续拆分建议

1. **拆分camera/album模块**：这两个模块紧密相关，可以一起拆分
2. **拆分settings模块**：设置界面
3. **最后拆分phone/sms模块**：最复杂的模块

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理+弹窗 | mobile_perm_mgr.c | 215行 | ✅ 已拆分 |
| 浏览器+书签 | mobile_browser.c | 252行 | ✅ 已拆分 |
| 相机+相册 | mobile_camera.c | ~450行 | ✅ 已拆分 |

mobile.c 从 4668行 → 3637行（减少1031行）

## camera/album模块拆分内容

已拆分到 mobile_camera.c：
- **全局变量**：cam_count、cam_bmp_buf、album_names、album_open、album_thumbs、album_thumb_valid
- **相机函数**：camera_photo_name、camera_save_bmp、mobile_run_camera
- **照片管理**：photo_delete、mobile_view_photo
- **相册函数**：album_cam_shots、album_build_thumbs、mobile_run_album
- **BMP支持**：320x200 256色BMP生成/读取/缩略图

## 后续拆分建议

1. **拆分settings模块**：设置界面
2. **拆分contacts模块**：联系人
3. **最后拆分phone/sms模块**：最复杂的模块
4. **拆分files模块**：文件管理器（依赖ext2）

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理+弹窗 | mobile_perm_mgr.c | 215行 | ✅ 已拆分 |
| 浏览器+书签 | mobile_browser.c | 252行 | ✅ 已拆分 |
| 相机+相册 | mobile_camera.c | 463行 | ✅ 已拆分 |
| 设置 | mobile_settings.c | ~180行 | ✅ 已拆分 |

mobile.c 从 4668行 → 3465行（减少1203行）

## settings模块拆分内容

已拆分到 mobile_settings.c：
- **14项设置**：WiFi、蓝牙、音量、网络、关于、电源、权限、隐私、强调色、时间制、主题、壁纸、日期格式、系统语言
- **主题预览**：Dark/Light/TRON三主题切换
- **视口滚动**：14项设置超出屏幕，支持W/S滚动
- **多语言**：中英文界面切换

## 后续拆分建议

1. **拆分contacts模块**：联系人管理
2. **最后拆分phone/sms模块**：最复杂的模块
3. **拆分files模块**：文件管理器（依赖ext2）
4. **拆分ai模块**：AI助手

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理+弹窗 | mobile_perm_mgr.c | 215行 | ✅ 已拆分 |
| 浏览器+书签 | mobile_browser.c | 252行 | ✅ 已拆分 |
| 相机+相册 | mobile_camera.c | 463行 | ✅ 已拆分 |
| 设置 | mobile_settings.c | 215行 | ✅ 已拆分 |
| AI助手 | mobile_ai.c | ~60行 | ✅ 已拆分 |

mobile.c 从 4668行 → 3408行（减少1260行）

## ai模块拆分内容

已拆分到 mobile_ai.c：
- **权限门**：麦克风权限（弹窗授权，拒绝则提示页）
- **语音识别**：ai_speech_recognize原型调用
- **对话界面**：中英文对话显示
- **麦克风按钮**：[A]键触发识别

## 后续拆分建议

1. **拆分contacts模块**：联系人管理（较大，约760行，依赖IME）
2. **最后拆分phone/sms模块**：最复杂的模块
3. **拆分files模块**：文件管理器（依赖ext2）

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理+弹窗 | mobile_perm_mgr.c | 215行 | ✅ 已拆分 |
| 浏览器+书签 | mobile_browser.c | 252行 | ✅ 已拆分 |
| 相机+相册 | mobile_camera.c | 463行 | ✅ 已拆分 |
| 设置 | mobile_settings.c | 215行 | ✅ 已拆分 |
| AI助手 | mobile_ai.c | 82行 | ✅ 已拆分 |
| 电话 | mobile_phone.c | ~220行 | ✅ 已拆分 |

mobile.c 从 4668行 → 3191行（减少1477行）

## phone模块拆分内容

已拆分到 mobile_phone.c：
- **拨号盘**：3x4拨号键盘，呼叫/挂断按钮
- **通话记录**：C键切换，Enter回拨，X删除，D清空（二次确认）
- **联系人联动**：W/S切换联系人，Enter填号拨号
- **通讯录预填**：从通讯录进入电话时自动预填号码
- **权限门**：PHONE权限 + CONTACTS权限

## 后续拆分建议

1. **拆分sms模块**：短信应用（约330行）
2. **拆分contacts模块**：联系人管理（较大，约760行，依赖IME）
3. **拆分files模块**：文件管理器（依赖ext2）

## 已完成拆分（最新）

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 共享函数 | mobile_common.c | 71行 | ✅ 已提取 |
| 关于 | mobile_about.c | 40行 | ✅ 已拆分 |
| 隐私看板 | mobile_privacy.c | 66行 | ✅ 已拆分 |
| 终端 | mobile_terminal.c | 50行 | ✅ 已拆分 |
| 权限管理+弹窗 | mobile_perm_mgr.c | 215行 | ✅ 已拆分 |
| 浏览器+书签 | mobile_browser.c | 252行 | ✅ 已拆分 |
| 相机+相册 | mobile_camera.c | 463行 | ✅ 已拆分 |
| 设置 | mobile_settings.c | 215行 | ✅ 已拆分 |
| AI助手 | mobile_ai.c | 82行 | ✅ 已拆分 |
| 电话 | mobile_phone.c | 256行 | ✅ 已拆分 |
| 文件管理 | mobile_files.c | 264行 | ✅ 已拆分 |

mobile.c 从 4668行 → 2956行（减少1712行）

## files模块拆分内容

已拆分到 mobile_files.c：
- **ext2文件浏览**：目录列表，文件/文件夹标识
- **文件操作**：复制/剪切/粘贴/新建文件夹/重命名/删除
- **权限门**：数据域权限检查（Contacts/SMS/Phone）
- **MAC安全**：文件访问和删除的MAC门卫
- **磁盘检测**：未检测到磁盘时显示提示

## 验证结果

- 编译验证：通过，无警告
- QEMU运行时验证：通过，系统成功启动，macho_demo正常运行
- 内核魔数：02b0ad1b 正常

## 后续拆分建议

1. **拆分sms模块**：短信应用（约650行，分两部分）
2. **拆分contacts模块**：联系人管理（较大，约760行，依赖IME）
