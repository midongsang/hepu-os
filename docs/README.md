---
title: 赫普OS 文档索引
status: active
sys_version: 0.56.2
updated: 2026-11-11
generator: tools/gen_docs_index.py
source_of_truth: frontmatter of docs/*
---

# 赫普OS 文档索引

> 本文件由 `tools/gen_docs_index.py` 自动生成，禁止手写。文档分区：`现在是什么`(architecture) / `契约是什么`(specs) / `怎么做`(guides) / `下一步做什么`(planning) / `启动与固件`(boot) / `安全策略与审计`(security) / `现在到哪了`(status) / `过去发生了什么`(history) / `截图`(screenshots) / `架构图`(diagrams)。
> 唯一事实源：VERSIONING.md(版本)、ROADMAP.md(优先级)、SYSCALL_ABI.md(系统调用，源码生成)。


## architecture

- [ANTI_BURDEN](architecture/ANTI_BURDEN.md)
- [BRAND_GUIDE](architecture/BRAND_GUIDE.md)
- [赫普OS 系统设计（现行）](architecture/DESIGN.md)
- [DESKTOP_SHELL](architecture/DESKTOP_SHELL.md)
- [GRAPHICS](architecture/GRAPHICS.md)
- [GUI](architecture/GUI.md)
- [KERNEL](architecture/KERNEL.md)
- [MEMORY_STORAGE](architecture/MEMORY_STORAGE.md)
- [OVERVIEW](architecture/OVERVIEW.md)
- [Hepu OS 核心哲学与原生契约](architecture/PHILOSOPHY.md)
- [Hepu Shell 设计原则](architecture/SHELL_DESIGN.md)
- [UNIFIED_SOURCE_TREE](architecture/UNIFIED_SOURCE_TREE.md)
- [USB 与无线输入支持（V0.13.62）](architecture/USB.md)

## specs

- [API_REFERENCE](specs/API_REFERENCE.md)
- [赫普OS 驱动操作接口（DRIVER_OPS）](specs/DRIVER_OPS.md)
- [DYNAMIC_LINKING](specs/DYNAMIC_LINKING.md)
- [HUG_FORMAT](specs/HUG_FORMAT.md)
- [赫普OS 系统调用 ABI（源码自动生成）](specs/SYSCALL_ABI.md)

## guides

- [BUILD](guides/BUILD.md)
- [ENVIRONMENT](guides/ENVIRONMENT.md)
- [INSTALL](guides/INSTALL.md)
- [REAL_HARDWARE_TEST](guides/REAL_HARDWARE_TEST.md)

## planning

- [Android HAL 移植方案（ANDROID_HAL_PORTING）](planning/ANDROID_HAL_PORTING.md)
- [COMPATIBILITY_LAYER](planning/COMPATIBILITY_LAYER.md)
- [兼容层落地路线（COMPAT_ROADMAP）](planning/COMPAT_ROADMAP.md)
- [Hepu 完整开发路线图](planning/FULL_ROADMAP.md)
- [内核优化与兼容预埋路线（KERNEL_OPT_PLAN）](planning/KERNEL_OPT_PLAN.md)
- [模块化设计准则（MODULARITY）](planning/MODULARITY.md)
- [组合B：SMP 硬化包实施计划（PLAN_SMP_HARDENING_R5）](planning/PLAN_SMP_HARDENING_R5.md)
- [赫普OS 开发路线图（唯一优先级来源）](planning/ROADMAP.md)
- [RUST](planning/RUST.md)
- [SMP 决策对比：选项 A 完整硬化 vs 选项 B 显式单核声明（R5）](planning/SMP_DECISION_A_VS_B_R5.md)
- [STORAGE_MEMORY](planning/STORAGE_MEMORY.md)
- [SYSTEM_VERIFICATION](planning/SYSTEM_VERIFICATION.md)
- [TECH_ROADMAP](planning/TECH_ROADMAP.md)
- [双视图交互方案（VIEW_SWITCH）](planning/VIEW_SWITCH.md)

## boot

- [BOOTLOADER_DEFINITION_PLAN](boot/BOOTLOADER_DEFINITION_PLAN.md)
- [BOOT_SPLASH_DESIGN](boot/BOOT_SPLASH_DESIGN.md)
- [HEPBOOT_DESIGN](boot/HEPBOOT_DESIGN.md)
- [UEFI_BOOT_DESIGN](boot/UEFI_BOOT_DESIGN.md)

## security

- [AUDIT_64BIT](security/AUDIT_64BIT.md)
- [64位内核安全审计（第二轮）](security/AUDIT_64BIT_R2.md)
- [AUDIT_DESKTOP_STABILITY_R3](security/AUDIT_DESKTOP_STABILITY_R3.md)
- [BUG_FIX_GUIDELINES](security/BUG_FIX_GUIDELINES.md)
- [FIX_SECURITY_BUNDLE_A_R4](security/FIX_SECURITY_BUNDLE_A_R4.md)
- [OPEN_SOURCE_POLICY](security/OPEN_SOURCE_POLICY.md)

## status

- [赫普OS 未关闭问题清单](status/OPEN_BUGS.md)
- [V0.33 开发路线板](status/PROGRESS_V033.md)
- [VERSIONING](status/VERSIONING.md)

## history

- [DESIGN_ITERATIONS](history/DESIGN_ITERATIONS.md) `(frozen)`
- [DESIGN_REVISION_NOTES](history/DESIGN_REVISION_NOTES.md) `(frozen)`

## history/devlog

- [00-original-full](history/devlog/00-original-full.md) `(frozen)`
- [01-boot-v4.94-v6.46](history/devlog/01-boot-v4.94-v6.46.md) `(frozen)`
- [02-v6.71-v7.65](history/devlog/02-v6.71-v7.65.md) `(frozen)`
- [03-v7.66-v0.10.5](history/devlog/03-v7.66-v0.10.5.md) `(frozen)`
- [04-x64-v0.10.6-v0.10.110](history/devlog/04-x64-v0.10.6-v0.10.110.md) `(frozen)`

## history/security

- [SECURITY_AUDIT_V7.65](history/security/SECURITY_AUDIT_V7.65.md) `(frozen)`

## 历史归档（history/archive）

`API_REFERENCE_V8.05`, `BRAND_GUIDE`, `COMPATIBILITY_LAYER_PLAN`, `DESIGN_V1.11`, `DESKTOP_SHELL_DEVELOPMENT`, `DEVELOPER_GUIDE`, `DEVELOPMENT_PLAN_V0.10.5`, `DEVELOPMENT_PLAN_V7.46`, `DEVELOPMENT_ROADMAP`, `DEVELOPMENT_STATUS`, `DEVELOPMENT_STATUS_V7.10`, `DEVELOPMENT_STATUS_V7.24`, `DYNAMIC_LINKING_V8.34`, `ENVIRONMENT_MAINTENANCE`, `GRAPHICS_SUBSYSTEM_DESIGN`, `GUI_DESIGN`, `HEPBOOT_MATURITY_PLAN`, `HUG_FORMAT`, `INSTALL_GUIDE`, `KERNEL_ARCHITECTURE_ANTI_BURDEN`, `KERNEL_RESOURCE_EXTENSION_V6.76`, `KERNEL_RESOURCE_EXTENSION_V6.77`, `KERNEL_RESOURCE_EXTENSION_V6.78`, `KERNEL_RESOURCE_EXTENSION_V6.79`, `MEMORY_STORAGE_DEVELOPMENT_PLAN`, `MEMORY_VRAM_STORAGE`, `MOBILE_REFACTOR_PLAN`, `PRIORITY_ROADMAP`, `PROJECT_STATUS_V6.80`, `PROJECT_STATUS_V6.81`, `REAL_HARDWARE_TEST_GUIDE_V7.94`, `REFACTOR_PROGRESS`, `REMAINING_TASKS_V0.10.0`, `REMAINING_TASKS_V0.10.5`, `RUST_DEVELOPMENT`, `SECURITY_AUDIT_64BIT`, `STORAGE_MEMORY_PLAN`, `SYSTEM_ARCHITECTURE_V8.40`, `SYSTEM_VERIFICATION_PLAN`, `TODO`, `X86_64_ARCHITECTURE`, `X86_64_DEVELOPMENT_PLAN`, `X86_64_PROGRESS`, `X86_64_SUPPORT_PLAN`

> 归档为只读历史，见 history/ 目录。