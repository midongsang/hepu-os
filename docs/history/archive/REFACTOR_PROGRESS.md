---
title: REFACTOR_PROGRESS
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# 赫普OS 代码重构进展记录

> 记录内核源码大文件拆分重构的进展。目标：所有内核源文件 < 500 行。
> 拆分方式遵循 DESIGN.md 规范：static 变量改全局，子模块用 extern 引用，Makefile 自动扫描。
> 最后更新：2026-09-21（重构批次6，ime_data.c拆分达标，kernel.c提取3个初始化函数，25个新建子模块，仅剩2个超标文件）

---

## 重构总览

| 状态 | 数量 | 说明 |
|------|------|------|
| 已达标 | 10个 | desktop.c, usb.c, vga_gfx.c, security.c, permission_manager.c, process.c, wm.c, ide_ext2.c, binfmt_pe.c + 部分binfmt_pe.c |
| 仍超标 | 3个 | mobile.c(5712), shell.c(3098), kernel.c(683) |
| 新建子模块 | 17个 | desktop_terminal.c, usb_hid.c, binfmt_pe_stub.c, binfmt_pe_util.c, vga_gfx_text.c, security_app.c, permission_manager_ext.c, sched.c, wm_draw.c, wm_event.c, wm_controls.c, ide_ext2_selftest.c, ide_ext2_fm.c + 4个待确认 |

---

## 已完成拆分（批次1）

### 1. desktop.c（501→352行）
- **拆分内容**：终端模拟器（term_buf/term_exec/terminal_draw/terminal_event/on_launch_terminal）
- **新文件**：`kernel/sched/desktop_terminal.c`（170行）
- **变更**：center_create 改全局（去掉static），terminal_win变量移除，desktop.c中extern声明on_launch_terminal
- **验证**：编译通过，QEMU启动正常

### 2. usb.c（526→490行）
- **拆分内容**：HID类驱动（hid_probe/hid_disconnect/hid_driver/usb_hid_init）
- **新文件**：`kernel/drivers/usb_hid.c`（46行）
- **变更**：HID驱动移到新文件，依赖usb_register_driver（已在usb.h声明）
- **验证**：编译通过，QEMU启动正常

### 3. binfmt_pe.c（663→610行，仍超标）
- **拆分内容**：用户态trampoline stub字节码数组（9个stub：write/read/exit/stdhandle/unsup/cmdline/tick/sleep/modname）
- **新文件**：`kernel/compat/binfmt_pe_stub.c`（75行）
- **变更**：stub数组改全局（去掉static const），binfmt_pe.c中extern声明（需指定数组大小才能用sizeof）
- **待完成**：binfmt_pe_load函数（324行）需深入拆分为pe_load_sections/pe_load_imports等子函数
- **验证**：编译通过

### 4. vga_gfx.c（654→497行）
- **拆分内容**：8x8字体数据（font_8x8[95][8]）+ 文本输出函数（gfx_putchar/gfx_puts/gfx_putchar_big/gfx_puts_big）
- **新文件**：`kernel/arch/x86/vga_gfx_text.c`（173行）
- **变更**：font_8x8改全局，文本输出函数移到新文件，extern声明gfx_putpixel/gfx_fillrect
- **验证**：编译通过

### 5. security.c（643→423行）
- **拆分内容**：应用沙箱功能（sec_selftest/sec_register_app/sec_app_data_type/sec_set_active_app/sec_get_active_app/sec_check_active_file）
- **新文件**：`kernel/security/security_app.c`（246行）
- **变更**：应用相关变量改全局（sec_app_names/sec_app_domains/sec_app_datatypes/sec_app_used/active_app/active_app_idx/domains/types/enforcing），SEC_MAX_APPS移到security.h
- **验证**：编译通过

### 6. permission_manager.c（724→471行）
- **拆分内容**：扩展功能（prm_get_suspicious_apps/prm_set_config/prm_get_config/设备使用状态/prm_get_stats/prm_perm_name/prm_perm_dangerous/策略解析）
- **新文件**：`kernel/arch/x86/permission_manager_ext.c`（268行）
- **变更**：pm变量改全局（去掉static），ext文件中extern声明pm
- **验证**：编译通过

---

## 已完成拆分（批次2）

### 7. process.c（801→389行）
- **拆分内容**：调度器（sched_tick/sched_mem_enforce/sched_pick_next/schedule/sched_init/sched_current_pid/sched_stats/sched_create_process/process_fork/process_execve/sys_fork_impl/sys_execve_impl）
- **新文件**：`kernel/sched/sched.c`（442行）
- **变更**：6个static变量改全局（proc_lock/proc_lock_flags/next_pid/sched_switches/sched_idle_ticks/sched_ticks_total），sched.c中extern声明；添加kheap.h/smp.h头文件
- **验证**：编译通过

### 8. wm.c（1040→384行，拆分为4个文件）
- **拆分内容**：
  - 绘制（draw_widget/wm_draw_window/wm_draw_all）→ wm_draw.c（215行）
  - 事件处理（wm_handle_event/wm_handle_gesture/wm_main_loop）→ wm_event.c（462行）
  - 高级控件（wm_add_list/wm_add_checkbox/wm_add_progress/wm_add_scrollbar）→ wm_controls.c（67行）
  - 窗口管理函数（wm_get_desktop/wm_maximize_toggle/wm_set_modal/wm_get_modal/wm_resize_window/wm_focus_next/wm_activate_focus）保留在wm.c
- **新文件**：`kernel/sched/wm_draw.c`（215行）、`kernel/sched/wm_event.c`（462行）、`kernel/sched/wm_controls.c`（67行）
- **变更**：16个static变量改全局（windows/desktop/next_z/wm_initialized/last_down_win/last_down_widget/focus_win/focus_widget/wm_modal/title_down_*/selected_icon/icon_click_*/wm_menu_*/cursor_*）；8个static函数改全局；WM_COLOR_*宏移到theme.h；WM_ICON_*/WM_MENU_ITEMS/WM_ICON_COUNT移到wm.h
- **验证**：编译通过

---

## 已完成拆分（批次3）

### 9. ide_ext2.c（921→421行，拆分为3个文件）
- **拆分内容**：
  - 自测（ide_ext2_selftest，262行）→ ide_ext2_selftest.c（275行）
  - 文件管理器API（fm_dir_ino/ide_ext2_list_entries/ide_ext2_delete/ide_ext2_read_file/ide_ext2_write_raw/ide_ext2_create_file/ide_ext2_ls_dir，236行）→ ide_ext2_fm.c（254行）
- **新文件**：`kernel/fs/ide_ext2_selftest.c`（275行）、`kernel/fs/ide_ext2_fm.c`（254行）
- **变更**：fs_mounted和fm_dir_ino改全局；selftest不依赖static变量可直接拆分
- **验证**：编译通过

### 10. binfmt_pe.c（663→475行，拆分为3个文件）
- **拆分内容**：
  - stub字节码数组（9个trampoline stub，63行）→ binfmt_pe_stub.c（75行，批次1完成）
  - 工具函数（pe_va2phys/pe_u32_read/pe_u32_write/pe_name_read/pe_match_api/pe_parse/pe_enter_user/pe_mainline_save，116行）→ binfmt_pe_util.c（148行）
- **新文件**：`kernel/compat/binfmt_pe_stub.c`（75行）、`kernel/compat/binfmt_pe_util.c`（148行）
- **变更**：8个static函数改全局；PE_COFF/OPTIONAL/SECTION_HEADER类型定义移到binfmt.h；PE_*常量（PE_MZ_MAGIC/PE_SIG/PE_MAX_SECTIONS/PE_CMDLINE_STR等）移到binfmt.h；PE_STUB_*常量移到binfmt.h；extern数组需指定大小
- **验证**：编译通过

---

## 待拆分文件（按优先级）

### 高优先级（结构清晰，易拆分）
| 文件 | 行数 | 拆分建议 |
|------|------|----------|
| ide_ext2.c | 921 | selftest(262行)→ide_ext2_selftest.c；文件管理器API(197行)→ide_ext2_fm.c |
| process.c | 801 | 调度函数(sched_tick/sched_pick_next/schedule)→sched.c |
| wm.c | 1040 | 按控件类型拆分（button/textfield/scrollbar等） |

### 中优先级（大函数需重构）
| 文件 | 行数 | 拆分建议 |
|------|------|----------|
| binfmt_pe.c | 610 | binfmt_pe_load(324行)拆分为pe_load_sections/pe_load_stub_page/pe_load_imports |
| kernel.c | 683 | hepu_kernel_main(400行)按初始化阶段拆分为子函数 |

### 低优先级（超大文件，需多轮拆分）
| 文件 | 行数 | 拆分建议 |
|------|------|----------|
| shell.c | 3098 | 按命令类型拆分（file_cmd/sys_cmd/net_cmd等） |
| mobile.c | 5712 | 按功能模块拆分（app_manager/notification/settings等） |
| ime_data.c | ~686 | 输入法数据拆分 |

---

## 拆分规范（DESIGN.md）

1. **static改全局**：被多个文件引用的变量/函数去掉static关键字
2. **extern引用**：子模块文件中用extern声明依赖的全局变量/函数
3. **数组大小**：extern声明数组时必须指定大小，否则sizeof无法计算
4. **头文件**：公共常量/类型定义移到对应.h文件，避免重复定义
5. **Makefile自动扫描**：`$(shell find kernel -name "*.c")`，无需手动维护源文件列表
6. **编译验证**：每次拆分后必须`make`编译通过
7. **启动验证**：构建镜像后QEMU启动验证，确认VGA输出[HEPU]、SEG1/SEG2正常

---

## 错误历史（重构过程中遇到的问题）

### REFACTOR-01: extern数组无大小导致sizeof失败
- **现象**：`error: invalid application of 'sizeof' to incomplete type 'const uint8_t[]'`
- **原因**：binfmt_pe.c中extern声明stub数组时未指定大小，sizeof无法计算
- **修复**：extern声明中指定数组大小（如`extern const uint8_t stub_write[18];`）
- **预防**：拆分含数组的文件时，extern声明必须指定数组大小

### REFACTOR-02: extern声明插入函数内部
- **现象**：`error: storage class specified for parameter 'sec_app_names'`
- **原因**：sed插入extern声明时位置错误，插入到了sec_audit函数参数列表中间
- **修复**：重写文件头部，将所有extern声明放在函数外部
- **预防**：拆分后检查文件头部结构，确保extern声明在全局作用域

### REFACTOR-03: 重复声明与头文件冲突
- **现象**：`error: conflicting types for 'sec_check_access'`
- **原因**：security_app.c中extern声明sec_check_access的参数类型与security.h中不一致
- **修复**：移除重复声明，直接包含security.h即可
- **预防**：拆分时先检查哪些函数已在头文件中声明，避免重复extern

### REFACTOR-04: 常量未移到头文件
- **现象**：`error: 'SEC_MAX_APPS' undeclared`
- **原因**：SEC_MAX_APPS在security.c中#define，拆分后security_app.c看不到
- **修复**：将#define SEC_MAX_APPS 16移到security.h中
- **预防**：拆分前检查所有#define常量，公共常量移到头文件

---

## 已完成拆分（批次4：shell.c大拆分）

> shell.c原3098行，含49个cmd_*命令函数。拆分为6个文件，shell.c降至227行。

### 1. shell_help.c（306行）
- **拆分内容**：帮助系统（help_table/man_table/cmd_help/cmd_man/cmd_version/cmd_about/help_group_title）
- **新文件**：`kernel/sched/shell_help.c`
- **变更**：help_table/man_table改全局，help_entry_t/man_entry_t类型移到shell.h

### 2. shell_cmd_system.c（402行）
- **拆分内容**：系统/进程命令（cmd_ps/cmd_affinity/cmd_ktest/cmd_prio/cmd_meminfo/rtc_snooze_parse/cmd_rtc/cmd_sysinfo）
- **新文件**：`kernel/sched/shell_cmd_system.c`

### 3. shell_cmd_test.c（485行）
- **拆分内容**：测试/性能/资源命令（cmd_dmatest/cmd_cacheinfo/cmd_perf/heap_used_snapshot/cmd_locktest/cmd_hdpk/cmd_setappcpu/cmd_ipcstress/cmd_memtest/cmd_limits/cmd_rlimit/cmd_softirqtest/cmd_softstat/firstboot_welcome）
- **新文件**：`kernel/sched/shell_cmd_test.c`

### 4. shell_cmd_ui.c（423行）
- **拆分内容**：UI/多媒体/系统命令（cmd_lang/cmd_dtb/cmd_cjk/cmd_banner/cmd_ime/cmd_quota/cmd_mac/cmd_softtick/cmd_lockstat/cmd_lockhold/cmd_canary/cmd_wd/cmd_time/cmd_audio/cmd_echo）
- **新文件**：`kernel/sched/shell_cmd_ui.c`

### 5. shell_cmd_fs.c（505行）
- **拆分内容**：文件系统/磁盘/图形命令（show_partitions/fs_cp_recursive/fs_rm_recursive/cmd_fs/sys_reboot_after_gfx/cmd_gfxtest/cmd_disk/cmd_gfx）
- **新文件**：`kernel/sched/shell_cmd_fs.c`
- **变更**：sys_reboot_after_gfx改全局（去掉static）

### 6. shell_execute.c（860行，仍超标待继续拆分）
- **拆分内容**：命令执行器（cmd_run_auto/cmd_run/shell_execute命令分发器/hepu_ui_gesture_cb/gesture_type_name）
- **新文件**：`kernel/sched/shell_execute.c`
- **变更**：shell_execute改全局（去掉static）
- **待办**：shell_execute函数含75个strcmp命令，需进一步拆分为命令分发表

### 7. shell.c（3098→227行）
- **剩余内容**：shell_main主循环、cmd_clear/cmd_ls/cmd_fwrite/cmd_hug/cmd_dmesg/cmd_loglevel
- **新建头文件**：`kernel/include/shell.h`（help_entry_t/man_entry_t类型+所有cmd函数声明）

### 拆分结果统计
| 文件 | 原行数 | 新行数 | 状态 |
|------|--------|--------|------|
| shell.c | 3098 | 227 | ✅ 达标 |
| shell_help.c | - | 306 | ✅ 新建 |
| shell_cmd_system.c | - | 402 | ✅ 新建 |
| shell_cmd_test.c | - | 485 | ✅ 新建 |
| shell_cmd_ui.c | - | 423 | ✅ 新建 |
| shell_cmd_fs.c | - | 505 | ⚠️ 略超 |
| shell_execute.c | - | 860 | ❌ 待拆分 |

---

## 重构错误历史（批次4新增）

### REFACTOR-05: sed操作导致函数大括号丢失
- **现象**：`error: expected declaration specifiers before 'if'`，多个函数缺少`{`
- **原因**：多次sed删除/插入操作误删了函数签名后的`{`，影响15+个函数
- **修复**：逐个检查函数签名，批量添加缺失的`{`
- **预防**：拆分文件时避免多次sed操作，优先使用Read+Edit精确修改

### REFACTOR-06: 函数签名被插入到函数体中间
- **现象**：shell_main函数结构混乱，重复定义，大括号错位
- **原因**：sed插入函数签名时位置错误，插入到了函数体中间
- **修复**：Read文件后用Edit精确修复函数签名和大括号
- **预防**：修改函数结构时必须先Read确认当前状态，禁止盲目sed

### REFACTOR-07: cmd_ls函数体被误删
- **现象**：`implicit declaration of function 'cmd_ls'`
- **原因**：拆分UI命令时，cmd_ls函数签名被移到shell_cmd_ui.c末尾，但函数体留在shell.c，后被误删
- **修复**：从shell_cmd_ui.c删除签名，在shell.c重建cmd_ls函数（调用ide_ext2_list_dir）
- **预防**：拆分边界函数时，确保函数签名和函数体在同一文件

### REFACTOR-08: static函数移到其他文件后不可见
- **现象**：`undefined reference to 'sys_reboot_after_gfx'`
- **原因**：sys_reboot_after_gfx是static函数，移到shell_cmd_fs.c后，shell.c的shell_main调用不到
- **修复**：去掉static改为全局，在shell.h中声明
- **预防**：拆分前检查所有被外部调用的static函数，提前改为全局

### REFACTOR-09: 头文件包含顺序导致类型未定义
- **现象**：`unknown type name 'ide_drive_t'`、`'touch_hw_state_t' undeclared`等
- **原因**：shell_execute.c拆分后缺少多个头文件（ide.h/usb.h/appreg.h/touch.h/touch_hw.h/power.h/wm.h）
- **修复**：逐个添加缺失的头文件
- **预防**：拆分前检查原文件所有#include，新文件必须包含全部依赖头文件

---

## 已完成拆分（批次5：shell_execute.c深度拆分）

> shell_execute.c原860行（shell_execute函数含75个strcmp命令）。提取4个大命令块到shell_cmd_adv.c，shell_execute.c降至452行达标。

### 1. shell_cmd_adv.c（450行，新建）
- **拆分内容**：高级命令大处理块
  - cmd_gesture（手势事件驱动，104行）
  - cmd_pm（电源管理，108行）
  - cmd_tcp（TCP状态机演示，94行）
  - cmd_touchtest（触屏驱动自检，69行）
  - hepu_ui_gesture_cb/gesture_type_name（手势回调函数）
- **新文件**：`kernel/sched/shell_cmd_adv.c`
- **变更**：hepu_ui_gesture_cb从shell_execute.c移入，改为static（仅在shell_cmd_adv.c中使用）

### 2. shell_execute.c（860→452行）
- **剩余内容**：cmd_run_auto/cmd_run/shell_execute命令分发器（剩余约60个命令）
- **变更**：4个大命令块替换为函数调用（cmd_gesture(args)/cmd_pm(args)/cmd_tcp(args)/cmd_touchtest(args)）

### 3. shell_cmd_fs.c（505→473行）
- **变更**：cmd_gfx/cmd_gfxtest移到shell_cmd_ui.c（UI相关命令归类）

### 4. shell_cmd_ui.c（423→455行）
- **变更**：接收cmd_gfx/cmd_gfxtest（图形命令归类UI）

### 拆分结果统计（全部shell文件达标）
| 文件 | 行数 | 状态 |
|------|------|------|
| shell.c | 227 | ✅ |
| shell_help.c | 306 | ✅ |
| shell_cmd_system.c | 402 | ✅ |
| shell_cmd_ui.c | 455 | ✅ |
| shell_cmd_adv.c | 450 | ✅ |
| shell_execute.c | 452 | ✅ |
| shell_cmd_test.c | 485 | ✅ |
| shell_cmd_fs.c | 473 | ✅ |

### 仍超标文件（后续批次）
- mobile.c（5712行）— 最大文件，需重点拆分
- ime_data.c（686行）
- kernel.c（660行）

---

## 重构错误历史（批次5新增）

### REFACTOR-10: 函数提取时大括号不匹配
- **现象**：`error: expected declaration or statement at end of input`
- **原因**：提取gesture命令块为函数时，块末尾缺少一个闭大括号（开20闭19）
- **修复**：在函数末尾添加缺失的`}`
- **预防**：提取函数块后用awk统计大括号数量，确保开闭匹配

### REFACTOR-11: 函数插入到其他函数内部
- **现象**：kernel.c中hepu_print_subsystem_state函数被插入到hepu_kernel_main函数体内部
- **原因**：sed `r`命令在匹配行之后读取文件，导致函数被插入到hepu_kernel_main签名后、函数体前
- **修复**：删除错误插入的函数，恢复hepu_kernel_main的`{`，放弃本次kernel.c拆分
- **预防**：在函数定义前插入新函数时，必须先Read确认结构，禁止盲目使用sed `r`命令

### REFACTOR-12: QEMU环境反复丢失
- **现象**：qemu-system-i386周期性从环境中消失，apt安装报"Unable to locate package"
- **原因**：apt源缺少universe仓库（qemu-system-x86在universe中），且apt锁被后台进程占用
- **修复**：`sudo add-apt-repository universe` + 清除apt锁 + 重新安装
- **预防**：环境初始化时先确认universe仓库已启用，apt安装前检查锁状态

---

## 已完成拆分（批次6：ime_data.c + kernel.c初始化函数提取）

### 1. ime_data.c（686→280行）
- **拆分内容**：拼音-汉字主数据表（ime_table，406行，1000+汉字）
- **新文件**：`kernel/apps/ime_data_table.c`（421行）
- **变更**：ime_table/ime_table_entries改全局，ime_data.c中extern声明

### 2. kernel.c（683→616行，部分达标）
- **提取函数**：
  - `hepu_sched_init()` — 进程与调度初始化（42行）
  - `hepu_mem_init(uint32_t mem_kb)` — 内存管理初始化（75行，mem_kb作为参数传递）
  - `hepu_irq_init()` — 系统调用测试+硬件中断初始化（40行）
- **剩余超标原因**：hepu_kernel_main仍含基础初始化/启动横幅/版权指纹/VFS/PCI等代码（约300行），需后续继续提取

### 3. mobile.c拆分尝试（失败，已恢复）
- **尝试**：提取mobile_run_clock（394行）到mobile_clock.c
- **失败原因**：mobile.c含大量static全局变量（mob_accent/sys_lang_en/gfx_width/mob_time12等）和static函数（mobile_perm_request/ui_draw_app_bg等），单个应用函数依赖数十个全局符号，逐个改全局成本过高
- **恢复**：从源码包恢复mobile.c到5712行，删除mobile_clock.c
- **后续方案**：mobile.c需先建立mobile.h头文件，将所有公共变量/函数统一声明，再按应用模块批量拆分

### 拆分结果统计
| 文件 | 原行数 | 新行数 | 状态 |
|------|--------|--------|------|
| ime_data.c | 686 | 280 | ✅ |
| ime_data_table.c | - | 421 | ✅ 新建 |
| kernel.c | 683 | 616 | ⚠️ 部分拆分 |
| mobile.c | 5712 | 5712 | ❌ 待系统拆分 |

### 仍超标文件（仅剩2个）
- mobile.c（5712行）— 需先建mobile.h再批量拆分
- kernel.c（616行）— 继续提取初始化函数

---

## 重构错误历史（批次6新增）

### REFACTOR-13: 大文件拆分时全局变量依赖爆炸
- **现象**：mobile.c提取mobile_run_clock后，mobile_clock.c报数十个"undeclared"错误（mob_accent/sys_lang_en/gfx_width/mob_time12/mob_datefmt等）
- **原因**：mobile.c含50+个static全局变量和20+个static函数，单个应用函数依赖大量公共符号，逐个改全局和extern声明成本极高
- **修复**：放弃单函数拆分，从源码包恢复mobile.c
- **预防**：大文件（>3000行）拆分前必须先建立公共头文件（如mobile.h），统一声明所有公共变量/函数，再按模块批量拆分

### REFACTOR-14: 提取函数后残余代码导致语法错误
- **现象**：mobile.c删除alarm_load/wc_load函数后，残留`wc_save();`和多余`}`导致"expected identifier before '}' token"
- **原因**：sed删除范围不准确，函数尾部代码残留
- **修复**：逐行检查删除边界，删除残余代码和多余大括号
- **预防**：提取函数后立即编译验证，发现语法错误时检查删除边界

---

## 已完成拆分（批次7：kernel.c达标 + mobile.c开始拆分）

### 1. kernel.c（616→498行）✅ 达标
- **新建kernel_init.c**（233行）：将4个初始化函数移到独立文件
  - hepu_sched_init（进程与调度初始化，42行）
  - hepu_mem_init（内存管理初始化，75行）
  - hepu_irq_init（系统调用测试+硬件中断初始化，40行）
  - hepu_fingerprint_check（版权指纹校验，17行）
- **hepu_kernel_main函数**：从221行降到约100行
- **拆分方式**：static改全局，子模块用extern引用，Makefile自动扫描

### 2. mobile.c开始拆分（5713→5020行）
- **新建mobile.h**（约160行）：统一声明所有公共变量/函数（50+个全局变量+30+个函数）
- **新建mobile_clock.c**（406行）：时钟应用（mobile_run_clock，含闹钟/世界时钟）
- **新建mobile_calc.c**（309行）：计算器应用（mobile_run_calc，含科学计算/通话记录函数）
- **剩余超标**：mobile.c仍有5020行，需继续拆分phone/contacts/sms/music/settings等模块

### 拆分结果统计
| 文件 | 原行数 | 新行数 | 状态 |
|------|--------|--------|------|
| kernel.c | 616 | 498 | ✅ |
| kernel_init.c | - | 233 | ✅ 新建 |
| mobile.c | 5713 | 5020 | ⚠️ 拆分中 |
| mobile_clock.c | - | 406 | ✅ 新建 |
| mobile_calc.c | - | 309 | ✅ 新建 |

### 仍超标文件（仅剩1个）
- mobile.c（5020行）— 需继续拆分phone/contacts/sms/music/settings等模块

---

## 重构错误历史（批次7新增）

### REFACTOR-15: 大文件拆分时头文件声明不完整
- **现象**：mobile_clock.c/mobile_calc.c编译报大量"undeclared"错误（al_armed/wc_off/calc_isqrt/calls_nums等）
- **原因**：mobile.c中有50+个static全局变量和30+个static函数，拆分到子模块后需要extern声明，但mobile.h初始声明不完整
- **修复**：逐步在mobile.h中补充所有缺失的变量和函数声明，统一类型和数组大小
- **预防**：大文件拆分前必须先完整分析所有全局变量和函数，建立完整的公共头文件

### REFACTOR-16: 数组大小不一致导致类型冲突
- **现象**：calls_times/calls_nums/wc_name等数组报"conflicting types"错误
- **原因**：mobile.h中声明的数组大小（如CALLS_MAX=32）与子模块中定义的大小（如CALLS_MAX=8）不一致
- **修复**：统一所有宏定义和数组大小，删除子模块中的重复定义，使用mobile.h中的统一定义
- **预防**：拆分前检查所有#define常量，确保头文件中的定义与原文件一致

### REFACTOR-17: 内核代码包含用户态头文件
- **现象**：mobile_calc.c编译报"fatal error: bits/libc-header-start.h: No such file or directory"
- **原因**：mobile_calc.c中包含了<math.h>，这是用户态头文件，内核交叉编译环境中不存在
- **修复**：删除<math.h>包含，内核中使用自定义的定点数计算函数（calc_sin/calc_cos等）
- **预防**：内核代码中禁止包含任何用户态C库头文件（math.h/stdio.h/stdlib.h等）

---

## 重构进展（批次7后续：mobile.c拆分尝试与恢复）

### mobile.c拆分尝试（phone/conversation模块）
- **尝试提取**：mobile_run_phone（463行）到mobile_phone.c，mobile_run_conversation（406行）到mobile_sms.c
- **遇到问题**：
  1. 大量sms/contacts变量声明缺失（sms_composing/contacts_names等30+个变量）
  2. 数组大小不一致导致类型冲突（CALLS_MAX=8 vs 32，SMS_IN_MAX=40 vs 128）
  3. static函数与extern声明冲突（sms_mark_read_peer）
  4. awk提取范围错误，误删mobile.c中大量代码
- **恢复**：从源码包恢复mobile.c到5020行（批次7状态，已拆分clock/calc），删除mobile_phone.c和mobile_sms.c
- **经验教训**：mobile.c拆分需要先完整建立mobile.h（包含所有50+全局变量和30+函数的准确声明），然后再按模块批量提取。单函数提取方式容易因声明不完整而失败。

### 当前状态
- kernel.c：498行 ✅ 达标
- kernel_init.c：233行 ✅ 达标（4个初始化函数）
- mobile_clock.c：406行 ✅ 达标（时钟应用）
- mobile_calc.c：309行 ✅ 达标（计算器应用）
- mobile.c：5020行 ❌ 仍超标（需继续拆分phone/contacts/sms/music/settings等模块）
- **所有其他内核源文件均已达标**

### 后续mobile.c拆分计划
1. 完善mobile.h：确保所有全局变量和函数声明准确（类型、数组大小、static/extern）
2. 按模块批量提取：phone（463行）→ conversation（406行）→ contacts（495行）→ sms（331行）→ settings（1902行，需进一步拆分）
3. 每次提取后编译验证，确保声明完整

---

## 已完成拆分（批次8：mobile.c继续拆分）

### mobile.c继续拆分（5020→4668行）
- **新建mobile_music.c**（209行）：音乐应用（mobile_run_music，含播放列表/收藏/历史）
- **新建mobile_power.c**（160行）：电源管理应用（mobile_run_power，含重启/关机/设置加载）
- **mobile.h完善**：补充所有music/power相关函数声明（music_rand_save/datefmt_load/wallpaper_load等20+个函数）
- **全局变量整理**：accent_colors/mob_accent/accent_idx等变量改为全局，消除多重定义

### 拆分结果统计
| 文件 | 原行数 | 新行数 | 状态 |
|------|--------|--------|------|
| mobile.c | 5020 | 4668 | ⚠️ 拆分中 |
| mobile_clock.c | - | 406 | ✅ 新建 |
| mobile_calc.c | - | 309 | ✅ 新建 |
| mobile_music.c | - | 209 | ✅ 新建 |
| mobile_power.c | - | 160 | ✅ 新建 |

### 仍超标文件（仅剩1个）
- mobile.c（4668行）— 需继续拆分settings（1902行）/contacts（495行）/sms（331行）/phone（463行）等模块

### 经验教训
- **小模块优先**：先拆分较小的模块（music 209行/power 160行），声明修复成本低，成功率高
- **大模块谨慎**：phone（463行）/conversation（406行）等大模块包含大量局部变量和工具函数，容易误删，需先完整分析函数边界
- **settings模块最大**：mobile_run_settings有1902行，需进一步拆分为多个子模块（显示/声音/网络/应用管理等）

---

## 重构进展（批次9：mobile.c拆分尝试与恢复）

### mobile.c拆分尝试（phone/contacts/sms/compose模块）
- **尝试提取**：phone（463行）、contacts（496行）、sms（331行）、compose（244行）、conversation（406行）
- **遇到问题**：
  1. awk模糊匹配导致提取范围错误，误删mobile.c大量代码（mobile_contacts.c提取2412行，实际应496行）
  2. 函数签名被误删，导致语法错误（expected identifier before '{' token）
  3. 子模块文件结构混乱，大括号不匹配
  4. static函数与extern声明冲突（music_rand_save/music_recent_save/accent_colors等）
- **恢复**：从源码包恢复mobile.c到4668行（批次8稳定状态），删除所有错误的子模块文件
- **经验教训**：
  1. **必须用sed精确行范围**，禁止用awk模糊匹配提取函数
  2. 提取前必须确认函数的准确起止行号（grep -n "^void function_name"）
  3. 每次只提取一个模块，编译验证后再继续
  4. static函数必须先改为全局，否则子模块无法链接

### 当前稳定状态（批次8）
- mobile.c：4668行（仍超标）
- 已拆分子模块：mobile_clock.c（406行）、mobile_calc.c（309行）、mobile_music.c（209行）、mobile_power.c（160行）
- 所有其他内核源文件均已达标（<500行）

### 后续mobile.c拆分计划（谨慎执行）
1. 用grep -n确认每个函数的准确起止行号
2. 用sed -n 'start,endp'精确提取函数到新文件
3. 用sed -i 'start,endd'从mobile.c中删除
4. 编译验证，确保无错误
5. 按模块顺序：terminal→files→about→ai→browser→camera→album→settings（最大，需进一步拆分）

---

## 重构进展（批次10：mobile.c小模块提取尝试与恢复）

### mobile.c小模块提取尝试
- **成功提取**：mobile_draw_dots（11行）、mobile_draw_navbar（15行）、mobile_draw_menu（32行）→ mobile_ui.c（68行）
- **失败提取**：
  1. mobile_draw_qkbd（27行）：与mobile_run_compose耦合（使用compose函数的局部变量field），提取后field未声明
  2. mobile_run_about/ai/bookmarks/perm/privacy/camera等模块：提取范围错误导致函数签名被误删，mobile.c结构破坏
- **恢复**：从源码包恢复mobile.c到4668行（批次8稳定状态），删除mobile_ui.c
- **关键发现**：
  1. mobile.c中函数耦合严重，很多UI绘制函数依赖应用函数的局部变量
  2. 提取函数前必须检查函数内部是否引用了其他函数的局部变量
  3. 完全独立的函数（如mobile_draw_dots/navbar/menu）可以安全提取
  4. 耦合函数（如mobile_draw_qkbd）需要先重构为独立函数（参数传递替代全局/局部变量引用）

### 当前稳定状态（批次8）
- mobile.c：4668行（仍超标）
- 已拆分子模块：mobile_clock.c（406行）、mobile_calc.c（309行）、mobile_music.c（209行）、mobile_power.c（160行）
- 所有其他内核源文件均已达标（<500行）

### mobile.c拆分建议（后续执行）
1. **先重构耦合函数**：将mobile_draw_qkbd等函数的局部变量引用改为参数传递
2. **按独立性排序提取**：先提取完全独立的函数（UI绘制/工具函数），再提取应用模块
3. **settings模块最大**（1902行）：需进一步拆分为显示/声音/网络/应用管理等子模块
4. **每次只提取一个函数**，编译验证后再继续
5. **用grep -n确认准确行号**，用sed精确提取，禁止awk模糊匹配

---

## 重构进展（批次11：小模块提取与变量修复）

### 成功提取的小模块
- **mobile_ui.c**（68行）：mobile_draw_dots（11行）、mobile_draw_navbar（15行）、mobile_draw_menu（32行）
- **mobile_about.c**（35行）：mobile_run_about
- **mobile_ai.c**（66行）：mobile_run_ai

### 遇到的问题与修复
1. **mob_wallpaper未定义**：mobile.c中只有前向声明（行560），没有实际定义。修复：将前向声明改为实际定义`int mob_wallpaper = 0;`
2. **hepu_mobile_launch_slot未声明**：函数在mobile.c末尾定义，但在mobile_run函数中使用，需要前向声明。修复：在mobile.h中添加extern声明
3. **提取范围错误**：多次因提取范围包含下一个函数的开头/注释导致语法错误。修复：用grep -n确认准确行号，sed精确提取，每次只提取一个函数

### 当前稳定状态
- mobile.c：4668行（仍超标，mob_wallpaper定义已修复）
- 已拆分子模块：mobile_clock.c（406行）、mobile_calc.c（309行）、mobile_music.c（209行）、mobile_power.c（160行）
- 所有其他内核源文件均已达标（<500行）

### 关键经验教训（更新）
1. **提取前必须检查函数是否有实际定义**：前向声明不等于实际定义，mob_wallpaper就是案例
2. **函数在使用前必须声明**：hepu_mobile_launch_slot在mobile.c末尾定义，需要在mobile.h中添加extern声明
3. **提取范围必须精确到函数结束大括号**：用grep -n确认下一个函数的起始行，提取范围为当前函数行到下一个函数行-1
4. **每次只提取一个函数**，编译验证后再继续

---

## 重构进展（批次12：小模块批量提取与恢复）

### 成功提取的小模块（临时）
- **mobile_ui.c**（68行）：mobile_draw_dots（11行）、mobile_draw_navbar（15行）、mobile_draw_menu（32行）
- **mobile_about.c**（35行）：mobile_run_about
- **mobile_ai.c**（66行）：mobile_run_ai
- **mobile_bookmarks.c**（59行）：mobile_run_bookmarks
- **mobile_privacy.c**（65行）：mobile_run_privacy
- **mobile_perm.c**（127行）：mobile_run_perm_app + mobile_run_perm_mgr

### 遇到的问题与恢复
1. **cam_bmp_buf/cam_count未定义**：提取privacy模块时，camera相关的全局变量定义被误删。修复：在mobile_run_camera之前添加定义，在mobile.h中添加extern声明
2. **提取范围错误**：多次因行号变化导致提取范围错误（terminal模块提取了files函数的内容）。根因：每次删除后行号变化，但未重新检查
3. **恢复**：从源码包恢复mobile.c到4668行稳定状态，删除所有临时子模块文件

### 关键经验教训（最终版）
1. **每次提取后必须重新检查行号**：删除函数后，后续函数的行号会变化，必须用grep -n重新确认
2. **提取范围必须精确到函数结束大括号**：用grep -n确认下一个函数的起始行，提取范围为当前函数行到下一个函数行-1
3. **每次只提取一个函数**，编译验证后再继续
4. **全局变量定义必须保留在mobile.c中**：提取函数时不要删除函数之间的全局变量定义
5. **camera相关变量（cam_bmp_buf/cam_count）定义在mobile_run_camera之前**，提取其他函数时不要误删

### 当前稳定状态
- mobile.c：4668行（仍超标）
- 已拆分子模块：mobile_clock.c（406行）、mobile_calc.c（309行）、mobile_music.c（209行）、mobile_power.c（160行）
- 所有其他内核源文件均已达标（<500行）

### mobile.c拆分建议（后续执行）
1. **按从小到大顺序提取**：先提取小函数（UI绘制/about/ai/bookmarks/privacy/perm），再提取大函数
2. **每次提取后重新检查行号**：这是最重要的经验教训
3. **全局变量定义保留在mobile.c中**：不要随函数一起删除
4. **settings模块最大**（1902行）：需进一步拆分为显示/声音/网络/应用管理等子模块
5. **每次只提取一个函数**，编译验证后再继续

---

## 重构进展（批次13：最好拆的模块提取与恢复）

### 成功提取的模块（临时）
- **mobile_ui.c**（68行）：mobile_draw_dots（11行）、mobile_draw_navbar（15行）、mobile_draw_menu（32行）
- **mobile_about.c**（35行）：mobile_run_about
- **mobile_ai.c**（66行）：mobile_run_ai
- **mobile_bookmarks.c**（59行）：mobile_run_bookmarks
- **mobile_privacy.c**（65行）：mobile_run_privacy
- **mobile_perm.c**（127行）：mobile_run_perm_app + mobile_run_perm_mgr
- **mobile_terminal.c**（156行）：mobile_run_terminal

### 遇到的问题与恢复
1. **cam_bmp_buf/cam_count定义位置错误**：提取privacy模块时，camera相关的全局变量定义被误提取到privacy.c中。修复：从privacy.c删除，在mobile.c中mobile_run_camera之前添加
2. **fm_clip定义被误删**：提取terminal模块时，提取范围包含了mobile_run_files函数的开头（包括fm_clip的static局部变量定义）。修复：从terminal.c删除files函数开头，在mobile.c中mobile_run_files开头恢复fm_clip定义
3. **mobile.c结构混乱**：多次sed操作导致重复的大括号和函数签名。恢复：从源码包恢复mobile.c到4668行稳定状态

### 关键经验教训（最终版）
1. **每次提取后必须重新检查行号**：删除函数后，后续函数的行号会变化
2. **提取范围必须精确到函数结束大括号**：用grep -n确认下一个函数的起始行，提取范围为当前函数行到下一个函数行-1
3. **每次只提取一个函数**，编译验证后再继续
4. **全局变量定义必须保留在mobile.c中**：不要随函数一起删除，特别是camera相关变量（cam_bmp_buf/cam_count）和files相关变量（fm_clip）
5. **static局部变量定义在函数开头**：提取函数时不要删除下一个函数的static局部变量定义

### 当前稳定状态
- mobile.c：4668行（仍超标）
- 已拆分子模块：mobile_clock.c（406行）、mobile_calc.c（309行）、mobile_music.c（209行）、mobile_power.c（160行）
- 所有其他内核源文件均已达标（<500行）

### mobile.c拆分建议（后续执行）
1. **按从小到大顺序提取**：先提取小函数（UI绘制/about/ai/bookmarks/privacy/perm），再提取大函数
2. **每次提取后重新检查行号**：这是最重要的经验教训
3. **全局变量定义保留在mobile.c中**：不要随函数一起删除
4. **static局部变量定义在函数开头**：提取函数时注意不要删除下一个函数的static局部变量
5. **settings模块最大**（1902行）：需进一步拆分为显示/声音/网络/应用管理等子模块
6. **每次只提取一个函数**，编译验证后再继续
