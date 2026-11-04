---
title: DEVELOPMENT_STATUS
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# 赫普OS 开发状态与剩余项整理

> 整理日期：2026-09-23
> 当前版本：HepuBoot v0.08 (UEFI) / 内核 V1.02+ / Rust V6.64
> 本文档汇总项目当前状态、已完成里程碑、剩余开发项和文档索引。

---

## 一、当前系统状态

### 1.1 核心能力（已验证）

| 领域 | 能力 | 状态 | 验证 |
|------|------|------|------|
| **引导** | HepuBoot UEFI引导 | ✅ 完成 | v0.08，GOP 1024x768 |
| | 三模式降级（文本→VGA→VBE） | ✅ 完成 | 自动降级 |
| | F8引导菜单 | ✅ 完成 | 多选项 |
| **内核** | 微内核架构 | ✅ 完成 | 抢占式调度 |
| | 内存管理（伙伴分配器/VMA/大页） | ✅ 完成 | 全部自测PASS |
| | 内存压缩（zram） | ✅ 完成 | 7项自测PASS |
| | 显存管理（VRAM/VVRAM/虚拟化） | ✅ 完成 | 自测PASS |
| | 文件系统（ext2/ext4/exfat/VFS） | ✅ 完成 | 读写验证 |
| | 网络协议栈（TCP/IP/DHCP/DNS/HTTP） | ✅ 基础 | 协议层完成 |
| | 安全机制（MAC/权限/审计） | ✅ 完成 | 16/16 PASS |
| | 多格式支持（ELF/PE32/Mach-O） | ✅ 完成 | 13个程序rc=0 |
| **图形** | VGA文本模式 | ✅ 完成 | 80x25 |
| | VGA 320x200 256色 | ✅ 完成 | gfxtest |
| | UEFI GOP高分辨率 | ✅ 完成 | 1024x768 32bpp |
| | TrueType抗锯齿字体 | ✅ 完成 | DejaVu Sans 16px |
| | 窗口管理器（基础） | ✅ 完成 | wm.c |
| **Rust生态** | hepu-sys系统调用库 | ✅ 完成 | 14+系统调用 |
| | 标准库函数（字符串/格式化/分配器） | ✅ 完成 | snprintf/strcmp等 |
| | IPC消息机制 | ✅ 完成 | ipc_send/ipc_recv |
| | 服务发现机制 | ✅ 完成 | service_register/lookup |
| | 6个Rust程序 | ✅ 完成 | 全部rc=0 |
| **应用** | 任务管理器 | ✅ 完成 | task_manager |
| | 三个经典游戏 | ✅ 完成 | 贪吃蛇/俄罗斯方块/扫雷 |
| | 手机模式应用（8+） | ✅ 完成 | 时钟/计算器/音乐等 |

### 1.2 最近修复的关键BUG

| BUG | 描述 | 修复版本 |
|-----|------|----------|
| BUG-17 | multiboot magic被破坏导致内存初始化失败 | V6.60 |
| BUG-18 | 内核BSS段膨胀到108MB导致可用内存为0 | V6.60 |
| BUG-19 | 系统调用返回值写入偏移量错误(36→32) | V6.64 |

---

## 二、剩余开发项（按优先级）

### P0 · 立即进行（当前主线）

| # | 项目 | 说明 | 依赖 | 预估工作量 |
|---|------|------|------|-----------|
| 1 | **file-server端到端IPC验证** | file-server启动时自动注册服务，fs-client通过服务名自动连接，验证完整文件操作IPC | 服务发现已完成 | 1-2天 |
| 2 | **BSD Socket API封装** | hepu-sys中添加socket/bind/listen/accept/connect/send/recv封装 | 内核已有TCP/IP | 2-3天 |
| 3 | **Rust网络服务试点** | 用Rust实现简单的HTTP服务器（net-server） | Socket API | 3-5天 |

### P1 · 短期（1-2周）

| # | 项目 | 说明 | 依赖 | 预估工作量 |
|---|------|------|------|-----------|
| 4 | **file-server并发请求处理** | 支持多客户端同时连接，请求队列 | 端到端验证 | 2-3天 |
| 5 | **mobile.c代码重构** | 4668行→<2000行，按功能模块拆分 | 无 | 3-5天 |
| 6 | **hepu-sys库扩展** | mmap/munmap/ioctl/fcntl系统调用，更多标准库函数 | 无 | 2-3天 |
| 7 | **应用商店基础框架** | 应用列表、安装、卸载、更新机制 | 文件系统 | 5-7天 |

### P2 · 中期（1-2月）

| # | 项目 | 说明 | 依赖 | 预估工作量 |
|---|------|------|------|-----------|
| 8 | **UI组件库** | 按钮/输入框/列表/菜单/对话框/布局管理器 | 图形子系统 | 1-2周 |
| 9 | **完整C标准库** | musl或newlib移植，POSIX兼容层 | 无 | 2-3周 |
| 10 | **动态链接支持** | .so共享库、动态加载器、插件系统 | 标准库 | 2-3周 |
| 11 | **GDB远程调试** | gdbstub、内核符号表、断点/单步 | 无 | 1-2周 |
| 12 | **第三方库移植** | zlib/libpng/libjpeg/openssl/curl | 标准库 | 2-3周 |
| 13 | **HTTPS/TLS加密** | TLS协议栈、证书验证 | 密码学已有 | 1-2周 |

### P3 · 长期（2-6月）

| # | 项目 | 说明 | 依赖 | 预估工作量 |
|---|------|------|------|-----------|
| 14 | **UEFI高级功能** | Secure Boot、图形化引导菜单、链式加载 | HepuBoot | 2-3周 |
| 15 | **企业级特性** | PXE网络引导、TPM测量引导、系统快照回滚 | UEFI | 1-2月 |
| 16 | **多语言运行时** | Python/JavaScript/Go运行时 | 标准库 | 2-3月 |
| 17 | **游戏生态** | SDL移植、游戏引擎、开发工具 | UI组件库 | 2-3月 |
| 18 | **真机硬件适配** | 触屏驱动、电源键唤醒、显示器电源关断 | 需真机 | 持续 |

### P4 · 环境限制（无法在QEMU验证）

| # | 项目 | 说明 |
|---|------|------|
| 19 | 触屏驱动真机适配 | Goodix GT9xx / FocalTech FT5x06 I2C控制器 |
| 20 | 触摸/电源键唤醒 | 自动休眠后触摸/电源键唤醒 |
| 21 | 真机显示器电源关断 | ACPI/APM真正关断 |
| 22 | VGA字体plane 2恢复 | QEMU std VGA读回恒0x0F，需真机验证 |

---

## 三、文档整理

### 3.1 文档清单与状态

| 文档 | 版本 | 行数 | 状态 | 说明 |
|------|------|------|------|------|
| `DESIGN.md` | V4.64头部 | 13920 | ⚠️ 需整理 | 核心设计+迭代史，版本不一致，过大 |
| `DESIGN_V1.11.md` | V1.11 | 866 | ✅ 定稿 | 正式设计规范 |
| `INDEX.md` | — | 147 | ⚠️ 需更新 | 文档导航，缺少新文档 |
| `TODO.md` | — | 103 | ⚠️ 严重过时 | 很多项已完成 |
| `PRIORITY_ROADMAP.md` | — | 295 | ⚠️ 过时 | P0项大多已完成 |
| `DEVELOPMENT_ROADMAP.md` | V1.0 | 639 | ⚠️ 部分过时 | 路线图，需更新状态 |
| `RUST_DEVELOPMENT.md` | V6.64 | 518 | ✅ 最新 | Rust开发指南 |
| `BOOTLOADER_BUG_HISTORY.md` | — | 1755 | ✅ 最新 | BUG-1~19 |
| `CHANGELOG.md` | — | 139 | ⚠️ 需更新 | 变更记录 |
| `HEPBOOT_DESIGN.md` | — | 286 | ✅ 较新 | HepuBoot设计 |
| `UEFI_BOOT_DESIGN.md` | — | 838 | ✅ 较新 | UEFI引导方案 |
| `MEMORY_VRAM_STORAGE.md` | V1.0 | 991 | ✅ 较新 | 内存/显存/存储管理 |
| `MEMORY_STORAGE_DEVELOPMENT_PLAN.md` | — | 419 | ✅ 较新 | 内存存储开发计划 |
| `GUI_DESIGN.md` | V2.35 | 277 | ✅ 活跃 | GUI设计总纲 |
| `REFACTOR_PROGRESS.md` | — | 622 | ✅ 最新 | 重构进展 |
| `MOBILE_REFACTOR_PLAN.md` | — | 412 | ✅ 较新 | mobile重构计划 |
| `TECH_ROADMAP.md` | — | 160 | ✅ 已确认 | 编程语言选型 |
| `HUG_FORMAT.md` | V1.0 | 175 | ✅ 定稿 | .hug格式规范 |
| `INSTALL_GUIDE.md` | V3.82 | 117 | ⚠️ 需更新 | 安装指南 |
| `OPEN_SOURCE_POLICY.md` | V1.0 | 172 | ✅ 定稿 | 开源政策 |
| `BRAND_GUIDE.md` | V1.0 | 230 | ✅ 定稿 | 品牌VI |
| `BUG_FIX_GUIDELINES.md` | — | 102 | ✅ 活跃 | BUG修复规范 |
| **DEVELOPMENT_STATUS.md** | V1.0 | — | 🆕 新增 | 本文档 |

### 3.2 文档整理建议

1. **DESIGN.md拆分**：13920行过大，建议拆分为：
   - `DESIGN_CORE.md`：核心架构设计（第1-14章）
   - `DESIGN_HISTORY.md`：版本迭代史（附录）
   - 保持`DESIGN.md`作为入口索引

2. **TODO.md更新**：标记已完成项，移除过时内容，链接到本文档

3. **PRIORITY_ROADMAP.md更新**：更新P0项状态，调整优先级

4. **INDEX.md更新**：添加新文档（UEFI_BOOT_DESIGN、MEMORY_VRAM_STORAGE、DEVELOPMENT_STATUS等）

5. **CHANGELOG.md更新**：补充V6.60-V6.64变更记录

---

## 四、技术路线确认

### 4.1 编程语言策略

| 层次 | 语言 | 状态 |
|------|------|------|
| 最底层（引导/上下文切换/中断） | 汇编 | ✅ 已实施 |
| 核心内核（调度/内存/IPC/文件系统） | C语言 | ✅ 已实施 |
| 用户态服务（文件/网络/图形/AI） | Rust（渐进引入） | ✅ 试点中 |
| 应用开发 | C/Rust/兼容层 | ✅ 多格式支持 |

### 4.2 引导策略

| 方案 | 状态 | 说明 |
|------|------|------|
| HepuBoot UEFI（主线） | ✅ 活跃 | v0.08，GOP高分辨率 |
| 自研BIOS引导（legacy） | ⚠️ 维护 | VBE暂禁用 |
| GRUB分支 | ❌ 已放弃 | 不作为安全模式 |

---

## 五、构建与验证

### 5.1 统一构建命令

```bash
# 完整构建（内核+引导器+ISO+IMG+数据盘）
bash tools/hepboot/uefi/build_uefi_image.sh

# Rust程序编译
cd user/rust
cargo build --target hepu-i686.json -Zbuild-std=core,alloc,compiler_builtins -Zjson-target-spec
```

### 5.2 QEMU验证命令

```bash
qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd \
    -cdrom build/hepu-os-uefi-v0.08.iso \
    -hda build/ext2_disk_uefi.img \
    -vga std -display none -net none \
    -serial file:/tmp/test.log
```

### 5.3 四件套交付物

| 产物 | 路径 | 大小 |
|------|------|------|
| 项目源码 | `hepu-os/` | — |
| ISO镜像 | `build/hepu-os-uefi-v0.08.iso` | 100MB |
| IMG镜像 | `build/hepu-os-uefi-v0.08.img` | 256MB |
| 数据盘 | `build/ext2_disk_uefi.img` | 64MB |

---

## 六、关键经验教训

1. **汇编栈偏移量必须仔细计算**：BUG-19的根因是返回值写入偏移量错误(36→32)，存在了很长时间才被发现
2. **静态数组严格控制大小**：BUG-18中BSS段膨胀到108MB，教学用途数组不应超过实际需要
3. **multiboot参数必须最早保存**：BUG-17中_start入口必须最先保存eax/ebx，后续任何代码都可能破坏
4. **工具丢失问题**：新Bash会话中apt源常被重置，需重新切换阿里云源并重装工具
5. **每次拆分必须验证**：mobile.c重构经验表明，每次提取函数后必须编译验证
6. **文档版本与镜像版本必须对应**：交付四件套时确保文档版本与镜像版本一致

---

## 七、更新记录

| 日期 | 动作 |
|------|------|
| 2026-09-23 | 创建本文档；汇总当前状态、剩余开发项、文档清单；确认技术路线 |
