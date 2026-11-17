# hepu-os 只读代码审计报告（2026-11-17）

- 审计基线：分支 `audit/2026-11-17`，基于 main `5f93c2d`（fix(net32): RCTL.BSIZE 写错位段）。
- 审计方式：只读 grep / 读码；未修改任何功能代码；未 push。
- 复核方式：每条结论均给出 `文件:行号`，可直接打开对应文件复核。
- 证据分级：
  - **已确认**：仓库内代码/构建脚本可直接佐证。
  - **待验证**：需要运行时证据（实机/QEMU 录制、编译链接、镜像打包清单）才能定论，报告内附验证方法。

## 报告目录

| # | 主题 | 文件 |
|---|------|------|
| 1 | 调试残留清查：自检程序清单与开机自动 exec 调用点 | [01-debug-residue.md](01-debug-residue.md) |
| 2 | 32/64 桌面 `hpdesk.c` 是否共用同一份源码 | [02-hpdesk-shared.md](02-hpdesk-shared.md) |
| 3 | 双架构能力/协议/初始化步骤不对称清单 | [03-arch-asymmetry.md](03-arch-asymmetry.md) |
| 4 | 定义但未调用的关键函数（安全/权限），死代码 vs 预留接口 | [04-unused-security-functions.md](04-unused-security-functions.md) |
| 5 | 危险模式、静默成功假实现，以及 fbsyn 事故机制复盘 | [05-dangerous-patterns-fbsyn.md](05-dangerous-patterns-fbsyn.md) |

## 审计范围与边界

- 仓库根：`/home/user/Doubao/chats/38452875115179010/hepu-os`；本人工作树：`/home/user/Doubao/chats/38452875115179010/wt-audit`。
- 他人工作树 `wt-lockfix`（分支 `feat/lock-switch-2026-11-17`）只读未写。
- 覆盖目录：`kernel/`（arch/x86、arch/x86_64、shared/security、shared/desktop、security、drivers、net、fs、mm）、`user/apps/`、顶层 `Makefile`、`kernel/arch/x86_64/Makefile`、`user/Makefile`、`tools/hepboot/`。
- 未执行全量编译；静态引用计数不等于链接期死代码结论，凡需要链接器/运行时佐证的条目均标 **待验证**。

## 最高优先级问题（摘要，详见各报告）

1. **安全假实现**：32 位登录/解锁/私钥解锁路径忽略密码参数直接放行（`kernel/arch/x86/login.c:76-91,151-168`；`kernel/arch/x86/multi_user.c:230-232`）；安全启动验签/验哈希为空桩（`kernel/arch/x86/secure_boot.c:49-99`）。
2. **登录路径未接密码校验**：64 位 `auth64_verify_password` 只被提权路径调用，开机进入桌面不校验密码（`kernel/arch/x86_64/auth64.c:321,354-361`；`desktop64.c:242-246`）。
3. **开机仍有持续运行的验收探针**：桌面主循环每 5 帧跑一次 FPU 探针（`kernel/arch/x86_64/desktop64.c:296-314`）；文本兜底 shell 路径无条件连跑 4 个测试程序（`shell64.c:3464-3488`）。
4. **双架构驱动/能力双份实现且不对称**：seccomp、私有地址空间、SMP、FB_FLIP、留屏 magic 仅 64 位；网卡/AHCI/USB/存储/音频各有两份独立实现。
