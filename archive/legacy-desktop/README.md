# 旧 32 位桌面（desktop_render.c）

**状态：已迁出内核，保留源码，不参与任何构建。**

## 这是什么

Hepu OS 早期 32 位的桌面实现，4522 行，195 KB。
包含：桌面背景、图标网格、上下文菜单、电源对话框、鼠标光标、任务栏、
窗口绘制（旧字号/旧配色那一套）。

它和 `kernel/arch/x86_64/hpdesk.c` 是**两套完全平行的桌面**，不共享任何一行代码。

## 为什么迁出

1. **语义混乱**：两套桌面同时存在于内核里，`hpdesk` 是实际在跑的那套，这一套只能从
   shell 命令或自检流程里被拉起来。一旦拉起来，看到的是另一个样子的"桌面"，
   用户没法判断哪个才是系统桌面。它的 7 个调用点已在上一轮全部摘掉。
2. **架构定位相悖**：32 位是微内核，应用在用户态。把 4522 行 GUI 塞进 ring0
   与这个定位冲突，也让 32/64 两套桌面永远无法共用。
   （这一条 `init_sequence.c` 里 D-7 的注释已经写过。）
3. **改一次视觉要改两遍**：它不共享 `hpdesk` 的任何绘制代码，两边永远对不齐。

## 为什么不直接删

它确实有 `hpdesk` 没有的东西（图标网格、上下文菜单、电源对话框等），
直接删是丢能力。用户明确要求**保留**，所以迁到内核外存档。

## 为什么不能直接当用户态程序编译

它依赖一批**内核内部符号**：

```
gfx_put_pixel / gfx_fill_rect / gfx_draw_rect / gfx_fill_rounded_rect
gfx_draw_rounded_rect / gfx_fill_circle / gfx_draw_circle / gfx_draw_ellipse
gfx_draw_line / gfx_draw_hline / gfx_draw_vline / gfx_draw_triangle
gfx_draw_string_font / gfx_flip / gfx_vbe_sync_mode
font_get_size / mouse_get_buttons / mouse_get_x / mouse_get_y / pit_*
```

所以在用户态编译会全部 undefined reference。
`user/apps/legacy/` 那种目录是会被 `user/Makefile` 编的，不能放在那儿。

## 位置选择的依据

内核构建用的是 `find $(KERNEL_DIR) -name "*.c"`，只扫 `kernel/` 树。
本文件放在仓库根的 `archive/` 下，任何 Makefile 都不会扫到，
也不会有人误以为它还在系统里。

## 要恢复它需要做什么

按 `docs/architecture/` 的分层判据走，不要另起一套：

1. 把 `gfx_*` 调用换成 **`SYS_FB_DESC`（拿 framebuffer 描述）+ `SYS_MMAP`（把 LFB 映射进
   ring3）**，自己在用户态算像素；
2. `mouse_*` 换成从输入设备读事件（或由桌面服务转发）；
3. `font_*` 换成用户态字体加载；
4. 然后它才是一个真正的"可切换桌面"应用，而不是内核里的一段 GUI。

这也是 `docs/planning/TECH_DEBT_ROUTE.md` 里 §5-3 / §5-4 的方向。

## 历史

- 迁出前位置：`kernel/arch/x86/desktop_render.c`
- 调用点摘除：commit `c6d0490`（7 处，`shell.c` 1 处 + `shell_cmd_ui.c` 6 处）
- 迁出：本提交
