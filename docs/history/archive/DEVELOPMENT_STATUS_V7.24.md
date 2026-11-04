---
title: DEVELOPMENT_STATUS_V7.24
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# 赫普OS 开发状态总览 V7.24

> 更新日期：2026-09-28
> 当前版本：V7.24
> 内核大小：约2.1MB
> 代码统计：400+ C文件，270+ 头文件，约12万行C代码

---

## 一、已完成功能

### 1. 引导系统（HepuBoot UEFI v0.08）
- ✅ UEFI引导器（自研，非GRUB）
- ✅ GOP高分辨率模式选择
- ✅ EDID刷新率检测
- ✅ 分块读取大内核（64KB/块）
- ✅ Secure Boot签名引导（db密钥签名）
- ✅ Shim链式加载（可选，手动选择）
- ✅ F8引导菜单
- ✅ 三模式降级：文本→VGA→VBE

### 2. 内核核心
- ✅ 微内核架构
- ✅ 内存管理（PMM/页表/Slab/堆/页面缓存/Swap/Zram/回收）
- ✅ 进程调度（优先级/就绪位图/CPU亲和性）
- ✅ IPC机制（消息传递/共享内存）
- ✅ 系统调用（78个）
- ✅ 中断处理（PIC/APIC）
- ✅ 定时器（PIT/HPET）

### 3. 文件系统
- ✅ ext2文件系统
- ✅ VFS虚拟文件系统
- ✅ IDE驱动（PIO模式）
- ✅ AHCI驱动（DMA模式）
- ✅ VVRAM虚拟显存（动态降级）
- ✅ 目录操作/文件权限

### 4. 网络协议栈
- ✅ TCP/UDP/IP/ARP
- ✅ DHCP客户端
- ✅ DNS解析
- ✅ HTTP客户端
- ✅ TLS加密
- ✅ QUIC协议
- ✅ BSD Socket API封装

### 5. 图形子系统（可插拔架构 V7.00+）
- ✅ 统一绘图API（graphics.h）
- ✅ 四个后端：virtio-gpu / GOP / VBE / VGA
- ✅ 后端自动探测和切换
- ✅ 显存分区管理（用户可自定义大小）
- ✅ 高级绘图（线/圆/椭圆/三角形/圆角矩形）
- ✅ 双缓冲渲染
- ✅ 内核启动参数 graphics=xxx
- ✅ 多显示器支持（基础框架）

### 6. 字体系统（V7.12-V7.24）
- ✅ 8x16位图字体
- ✅ 抗锯齿位图字体（Alpha混合）
- ✅ 字号缩放（8-64px）
- ✅ 字体缓存（256条目，FIFO）
- ✅ TrueType解析器（完整实现）
  - 表目录解析（head/hhea/maxp/cmap/glyf/loca/hmtx）
  - cmap格式4（Unicode段映射）
  - 简单字形端点解析
  - 二次贝塞尔曲线细分
  - 扫描线填充（非零环绕）
  - 4x4超采样抗锯齿
  - 复合字形支持
- ✅ CJK中文字体支持（基础框架）
- ✅ 多字体管理（最多8个字体）
- ✅ 字体回退机制
- ✅ 字体配置文件（启动时自动加载）
- ✅ Shell命令：fonttest/fontsize/fonttype/fontload/fontlist/cjkfonttest

### 7. 桌面环境
- ✅ 现代桌面（扁平化设计）
- ✅ 状态栏（系统托盘/分辨率/刷新率/时钟）
- ✅ Dock栏（窗口状态指示）
- ✅ 窗口管理（创建/移动/缩放/关闭/最小化/最大化）
- ✅ 右键菜单（9个菜单项）
- ✅ 图标（渐变+圆角+阴影+高光）
- ✅ UI组件库（按钮/输入框/复选框/滑块）
- ✅ 系统设置（字体大小等）
- ✅ 关机/重启（ACPI/8042/CF9）
- ✅ 鼠标支持
- ✅ 应用商店（演示版）

### 8. 用户态
- ✅ 21个C用户态程序
- ✅ 13个Rust文件（net-server等）
- ✅ Rust交叉编译工具链（hepu-i686 target）
- ✅ initrd自定义格式（HEPURD）

### 9. 安全机制
- ✅ Secure Boot签名引导
- ✅ 沙盒机制（compat_sandbox_path_allowed）
- ✅ 内存隔离（分页）

### 10. 开发工具
- ✅ 四件套交付（源码+ISO+IMG+数据盘）
- ✅ 错误历史文档（3600+行，38个已记录BUG）
- ✅ 完整文档体系（30+文档）

---

## 二、剩余开发项

### P0 · 高优先级

| # | 待办项 | 说明 | 状态 |
|---|--------|------|------|
| 1 | 应用商店UI交互完善 | 当前为演示版本，完整键盘/鼠标交互待开发 | 待办 |
| 2 | 真机高分辨率验证 | V6.94-V6.98高分辨率仅在QEMU测试，真机未验证 | 待办 |
| 3 | 多显示器支持完善 | EDID读取、显示输出检测、热插拔检测 | 待办 |
| 4 | PAE完整切换验证 | PAE模式下驱动/中断/用户进程完整性待验证 | 进行中（独立分支） |

### P1 · 中优先级

| # | 待办项 | 说明 | 状态 |
|---|--------|------|------|
| 5 | TrueType指令解释（Hinting） | 提升小字号清晰度，可选 | 待办 |
| 6 | 更多Rust用户态服务 | 图形服务/AI运行时/隐私金库 | 待办 |
| 7 | HTTPS/TLS加密完善 | 当前35%完成 | 待办 |
| 8 | 存储显存自动挂载 | 当前20%完成 | 待办 |
| 9 | 文档整理 | TODO.md/PRIORITY_ROADMAP.md严重过时 | 待办 |
| 10 | CJK中文字体完整支持 | 需要中文字体文件和完整渲染验证 | 待办 |

### P2 · 低优先级

| # | 待办项 | 说明 | 状态 |
|---|--------|------|------|
| 11 | 字体子集化 | 减小字体文件大小 | 待办 |
| 12 | 64bpp广色域验证 | 代码已支持，QEMU只支持32bpp | 待办 |
| 13 | 触屏驱动真机适配 | 需要真机硬件 | 待办 |
| 14 | 触摸/电源键唤醒 | 触屏驱动前置 | 待办 |
| 15 | Python/JavaScript运行时 | 长期规划 | 待办 |
| 16 | 动态链接/共享库 | 长期规划 | 待办 |
| 17 | 包管理器 | 长期规划 | 待办 |

---

## 三、已知问题

| # | 问题 | 影响 | 状态 |
|---|------|------|------|
| 1 | 工具丢失问题 | apt源常被重置，qemu/ovmf/mtools等反复丢失 | 已知，每次需重装 |
| 2 | Shim链式加载在QEMU非Secure Boot环境循环重启 | 已放弃作为默认方案，改为直接引导 | 已解决 |
| 3 | 真机Secure Boot需手动导入证书 | 用户需在BIOS导入HEPU_DB.crt | 已知，文档已说明 |
| 4 | 字体缓存共享缓冲区 | 缓存条目存储指针而非副本，渲染新字形会覆盖 | 已知，简化实现 |
| 5 | TrueType光栅化递归深度 | 贝塞尔曲线递归细分可能栈溢出 | 已知，有max_count保护 |

---

## 四、版本历史

| 版本 | 主要内容 | 状态 |
|------|----------|------|
| V6.99 | 真机UEFI/Secure Boot兼容性修复 | ✅ 已交付 |
| V7.00 | 可插拔图形子系统启动 | ✅ 已交付 |
| V7.01-V7.02 | 内核集成+探测逻辑修复 | ✅ 已交付 |
| V7.03 | virtio-gpu后端基础框架 | ✅ 已交付 |
| V7.04 | 高级绘图API | ✅ 已交付 |
| V7.05-V7.06 | desktop_render.c迁移 | ✅ 已交付 |
| V7.07 | 统一API抗锯齿字体 | ✅ 已交付 |
| V7.08 | VBE后端（传统BIOS支持） | ✅ 已交付 |
| V7.09 | 内核启动参数+多显示器支持 | ✅ 已交付 |
| V7.10 | virtio-gpu完整virtqueue+2D加速 | ✅ 已交付 |
| V7.11 | 文档整理+多显示器信息显示 | ✅ 已交付 |
| V7.12 | 字体管理器+可缩放抗锯齿字体 | ✅ 已交付 |
| V7.13 | 桌面文字渲染迁移到字体管理器 | ✅ 已交付 |
| V7.14 | 字体测试命令+多字号渲染验证 | ✅ 已交付 |
| V7.15 | 字体配置界面+系统设置集成 | ✅ 已交付 |
| V7.16 | 字体大小实时调整+动态桌面字体 | ✅ 已交付 |
| V7.17 | TrueType解析器+设置变更实时生效 | ✅ 已交付 |
| V7.18 | TrueType集成到字体管理器+字体切换 | ✅ 已交付 |
| V7.19 | 字体文件从文件系统加载 | ✅ 已交付 |
| V7.20 | TrueType完整光栅化 | ✅ 已交付 |
| V7.21 | 复合字形支持 | ✅ 已交付 |
| V7.22 | CJK中文字体支持基础框架 | ✅ 已交付 |
| V7.23 | 多字体管理+字体回退 | ✅ 已交付 |
| V7.24 | 字体配置文件（启动时自动加载） | ✅ 已交付 |

---

## 五、技术栈

- **核心内核**：C语言（C11/C17）
- **最底层**：汇编语言（启动/上下文切换/中断入口）
- **用户态**：C + Rust（渐进引入）
- **引导**：自研HepuBoot UEFI（C + EFI）
- **构建**：Make + GCC交叉编译
- **测试**：QEMU + OVMF（UEFI固件）
- **文档**：Markdown

---

## 六、四件套交付物

1. **源码包**：hepu-os-vX.XX-src.zip（约47MB）
2. **UEFI ISO**：hepu-os-uefi-v0.08.iso（约101MB）
3. **UEFI IMG**：hepu-os-uefi-v0.08.img（256MB）
4. **ext2数据盘**：ext2_disk_uefi.img（64MB，含12个Rust用户态程序）

---

## 七、关键命令

### Shell命令
- `gfxinfo` - 图形子系统信息
- `gfxtest_subsys` - 图形子系统自测（10项）
- `fonttest` - 字体管理器测试
- `fontsize <8-64>` - 设置字体大小
- `fonttype <bitmap|aa|truetype>` - 切换字体类型
- `fontload <path>` - 加载TrueType字体
- `fontlist [index]` - 列出/选择字体
- `cjkfonttest` - CJK中文字体测试
- `gui` / `edesktop` - 启动图形桌面
- `appstore` - 应用商店
- `pae_switch` - PAE切换（独立分支）

### 构建命令
```bash
make                    # 编译内核
make clean              # 清理
```

### QEMU测试命令
```bash
qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd \
  -drive format=raw,file=build/hepu-os-uefi-v0.08.img,snapshot=on \
  -drive format=raw,file=build/ext2_disk_uefi.img,snapshot=on \
  -vga std -display none -net none -serial file:log.txt
```
