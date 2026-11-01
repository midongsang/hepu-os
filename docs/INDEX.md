# 赫普OS 项目文档导航（INDEX）

> 本文档是赫普OS项目的**唯一文档入口**，梳理全部文档的定位与关系。
> 最后更新：2026-10-13 ｜ 版本：v0.10.110

---

## 0. 快速入口

| 我想... | 看这个文档 |
|---------|-----------|
| 了解系统整体设计 | [design/DESIGN.md](design/DESIGN.md) |
| 快速开始开发 | [guides/DEVELOPER_GUIDE.md](guides/DEVELOPER_GUIDE.md) |
| 查看开发路线图 | [planning/DEVELOPMENT_ROADMAP.md](planning/DEVELOPMENT_ROADMAP.md) |
| 查看64位开发计划 | [planning/X86_64_DEVELOPMENT_PLAN.md](planning/X86_64_DEVELOPMENT_PLAN.md) |
| 查看错误历史 | [status/BOOTLOADER_BUG_HISTORY.md](status/BOOTLOADER_BUG_HISTORY.md) |
| 查看系统架构图 | [diagrams/](../docs/diagrams/) |

---

## 1. 核心设计（design/）

| 文档 | 说明 | 状态 |
|------|------|------|
| `DESIGN.md` | 核心设计文档 + 全量迭代史（786KB） | 活跃 |
| `SYSTEM_ARCHITECTURE_V8.40.md` | 系统架构规范 V8.40 | 定稿 |
| `KERNEL_ARCHITECTURE_ANTI_BURDEN.md` | 内核架构：规避历史包袱设计准则 | 活跃 |
| `X86_64_ARCHITECTURE.md` | 64位架构设计 | 活跃 |
| `GUI_DESIGN.md` | GUI设计总纲（桌面+手机双模式） | 活跃 |
| `BRAND_GUIDE.md` | 品牌VI视觉识别规范 | 定稿 |

## 2. 引导与启动（boot/）

| 文档 | 说明 | 状态 |
|------|------|------|
| `HEPBOOT_DESIGN.md` | HepuBoot引导器设计（UEFI+BIOS双模式） | 活跃 |
| `UEFI_BOOT_DESIGN.md` | UEFI引导方案设计 | 活跃 |
| `BOOTLOADER_DEFINITION_PLAN.md` | 引导器定义与成熟度规划 | 活跃 |
| `BOOT_SPLASH_DESIGN.md` | 启动画面设计（方案A/B） | 活跃 |

## 3. 开发规划（planning/）

| 文档 | 说明 | 状态 |
|------|------|------|
| `DEVELOPMENT_ROADMAP.md` | 软件开发路线图（六大方向） | 活跃 |
| `PRIORITY_ROADMAP.md` | 开发优先级规划（P0-P4） | 待更新 |
| `TECH_ROADMAP.md` | 技术路线：C核心+Rust用户态渐进 | 已确认 |
| `X86_64_DEVELOPMENT_PLAN.md` | 64位支持开发规划 | 活跃 |
| `RUST_DEVELOPMENT.md` | Rust用户态开发指南 | 活跃 |
| `SYSTEM_VERIFICATION_PLAN.md` | 系统验证计划（四周52套件610用例） | 活跃 |

## 4. 子系统设计（subsystems/）

| 文档 | 说明 | 状态 |
|------|------|------|
| `GRAPHICS_SUBSYSTEM_DESIGN.md` | 可插拔图形子系统设计（VGA/VBE/virtio-gpu） | 活跃 |
| `MEMORY_VRAM_STORAGE.md` | 内存、显存与存储资源管理详解 | 活跃 |
| `DYNAMIC_LINKING_V8.34.md` | 动态链接设计 V8.34 | 定稿 |
| `API_REFERENCE_V8.05.md` | API参考手册 V8.05 | 待更新 |
| `HUG_FORMAT.md` | .hug应用二进制格式规范 | 定稿 |

## 5. 安全与审计（security/）

| 文档 | 说明 | 状态 |
|------|------|------|
| `SECURITY_AUDIT_V7.65.md` | 安全审计报告（29个漏洞A-Z+AA） | 已修复 |
| `BUG_FIX_GUIDELINES.md` | BUG修复规范与流程 | 活跃 |
| `OPEN_SOURCE_POLICY.md` | 开源协议+厂商分发政策 | 定稿 |

## 6. 状态与记录（status/）

| 文档 | 说明 | 状态 |
|------|------|------|
| `BOOTLOADER_BUG_HISTORY.md` | 错误历史（111个bug，15240行） | 持续更新 |
| `CHANGELOG.md` | 版本变更记录 | 待更新 |
| `VERSIONING.md` | 版本号命名规范（重要版本.默认版本.bug版本） | 定稿 |

## 7. 指南（guides/）

| 文档 | 说明 | 状态 |
|------|------|------|
| `DEVELOPER_GUIDE.md` | 开发者快速上手指南 | 待更新 |
| `INSTALL_GUIDE.md` | 安装/试用/QEMU开发验证指南 | 待更新 |
| `REAL_HARDWARE_TEST_GUIDE_V7.94.md` | 真机测试指南 V7.94 | 活跃 |
| `ENVIRONMENT_MAINTENANCE.md` | 开发环境维护（QEMU/工具链持久化） | 活跃 |

## 8. 归档（archive/）

> 以下为旧版本状态/计划文档，保留供历史参考，不再维护。

- `DESIGN_V1.11.md` — 旧版系统设计规范
- `DEVELOPMENT_PLAN_V0.10.5.md` / `V7.46.md` — 旧版开发计划
- `DEVELOPMENT_STATUS.md` / `V7.10.md` / `V7.24.md` — 旧版开发状态
- `PROJECT_STATUS_V6.80.md` / `V6.81.md` — 旧版项目进度
- `REMAINING_TASKS_V0.10.0.md` / `V0.10.5.md` — 旧版剩余任务
- `KERNEL_RESOURCE_EXTENSION_V6.76~79.md` — 旧版资源扩展设计
- `MEMORY_STORAGE_DEVELOPMENT_PLAN.md` — 旧版内存存储计划
- `MOBILE_REFACTOR_PLAN.md` — mobile.c重构计划
- `REFACTOR_PROGRESS.md` — 重构进展记录
- `X86_64_PROGRESS.md` / `X86_64_SUPPORT_PLAN.md` — 旧版64位进展
- `HEPBOOT_MATURITY_PLAN.md` — HepuBoot成熟度计划
- `TODO.md` — 旧版待办清单

---

## 9. 项目根目录文档

| 文档 | 说明 |
|------|------|
| `README.md` | 项目入口：简介/快速开始/当前状态 |
| `CONTRIBUTING.md` | 贡献指南 |
| `CODE_OF_CONDUCT.md` | 社区行为准则 |
| `CONTRIBUTORS.md` | 贡献者名单 |
| `LICENSE` | GPLv3 + HEPU附加条款 |

---

## 10. 资源目录

- `diagrams/` — 系统架构图、流程图（SVG/PNG）
- `screenshots/` — 系统截图
