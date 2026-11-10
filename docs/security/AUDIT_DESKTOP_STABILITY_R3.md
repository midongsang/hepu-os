---
title: AUDIT_DESKTOP_STABILITY_R3
status: active
sys_version: 0.54.11
updated: 2026-11-11
owner: kernel
source_of_truth: null
supersedes: null
---

# 第三轮：桌面体验与系统稳定性修复

## 缺陷 1：HepuShell 窗口槽位泄漏

### 证据
- `hs_window_create` 旧实现按 `window_count` 线性追加槽位，计数只增不减。
- 4 条真实关闭路径（关闭按钮、标题栏×、Alt+Tab 切换器、Ctrl+W）只置 `visible=0`，
  不回收槽位、不从 `z_order` 移除。
- 反复开关 32 次（`HS_MAX_WINDOWS`）后 create 恒返回 NULL，无法再开窗。

### 修复
- 用独立的 `id==-1` 标记空闲槽（区别于"最小化/布局隐藏"的 visible=0）。
- 新增统一回收函数 `hs_window_recycle`，create 改为复用空闲槽并保持 window_count 高水位语义。
- 新增自包含主机测试 `winslot_test.c`（T1-T4 不变量），对照实验证明旧算法在第 32 次创建失败。

## 缺陷 2：纯 APIC 模式下 PIT 中断冻结，桌面画面永久停在首帧（严重）

### 证据链
- QEMU 启动后所有截图 md5 完全相同、时钟停摆、外部输入画面无响应，但 QEMU 占高 CPU。
- gdb 多次独立采样：CPU#0 恒停在 `desktop64_run` 紧循环；`pit_ticks` 冻结在 71，
  而 `g_kernel_tick64` 持续快速增长（LAPIC timer 仍工作）。
- 断点实验：`pit64_interrupt` 断点不命中，`keyboard64_interrupt` 断点命中。
- 解析真实 MADT：存在 Interrupt Source Override，把 **ISA IRQ0(PIT) 覆盖到 GSI 2**
  （IRQ1 键盘无覆盖，仍在 GSI 1）。
- 根因：`apic64_switch_to_apic_mode` 把 PIT 错误配置在 IOAPIC pin 0，而 PIT 实际
  经覆盖接在 pin 2，中断永不投递；c51fa4e 的渲染节流又用 `pit64_get_ms()`（冻结值），
  首帧后 `now-last_render` 恒为 0，永远跳过渲染。

### 修复（通用，非硬编码）
- `acpi.c/h`：新增 ISA IRQ→GSI 映射表与 `acpi_irq_to_gsi()`，MADT 解析处理 type=2 覆盖；
  新增可在中断切换前调用的 `acpi_init_irq_overrides()`（只依赖分页）。
- `kernel_main_64.c`：切换纯 APIC 模式前先解析中断覆盖。
- `apic64.c`：PIT/键盘/鼠标重定向统一经 `acpi_irq_to_gsi()` 翻译到真实 GSI。

## 验证
- 内核编译通过（无新增警告），完整镜像构建成功。
- QEMU：`pit_ticks` 恢复 1kHz 增长；OOBE 键盘操作后画面实时从 1/4 进入 2/4；
  鼠标坐标随移动正常更新；顶栏时钟走时。
- 主机测试 6/6 通过（含新增 winslot）。
- 落地提交：`0b4750f`（窗口槽位泄漏 + 纯 APIC 模式 PIT 中断冻结，两项均已关闭）。

## 已复核关闭遗留

- **R3-CURSOR**（测量误解，关闭，非真 bug，无需修复）：实测确认 Shell 选定坐标源后逐帧直拷、渲染无二次偏移。
  - 符号/偏移（nm + 反汇编互证）：`g_shell`=0x23ced60；`mouse_x` 偏移 0x5148（0x23d3ea8）、`mouse_y` 偏移 0x514c（0x23d3eac）；`hepushell_update_mouse` 直拷赋值。
  - PS/2 路径 7 组采样，`g_shell` 内坐标与 `ps2mouse64.c` 静态 `mouse_x/y` 逐点相等（512,384 → 562,384 → 612,384；钳制 1020,767）。
  - tablet 路径跟随 USB tablet 绝对坐标（`abs_x` 偏移 0x30、`abs_y` 偏移 0x34、结构 0x48、基址 0x30b1e0，基线 (0,0)）；HMP `mouse_move` 只发 PS/2 相对 delta、改不到绝对指针设备，`mouse_set 0/1/2` 无效、7.2.0 无绝对定位命令，故 tablet 光标停初始 (0,0) 被误读为「光标不动/坐标错」。
  - 误解机制：两套独立坐标源直接对比 + 混用相对/绝对语义；实际无错位。
  - 可选加固（非缺陷，本轮未实施）：tablet 首个报告前坐标默认屏幕中心而非 (0,0)（`usb64.c:223`、`:1787`，约 ≤5 行）。
  - 详见 OPEN_BUGS「已复核关闭条目 · R3-CURSOR」。
