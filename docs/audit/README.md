# hepu-os 只读代码审计报告（2026-11-17）

- 审计基线：分支 `audit/2026-11-17`，基于 main `5f93c2d`（fix(net32): RCTL.BSIZE 写错位段）。
- main 复核：基线之后 main 已前进到 `61c53fa`。涉及提交：`0202461`（关常驻探针）、`0de2133`（32 位认证显式未实现）、`3d0bd23`（安全启动返 -2）、`10e42fa`（OOBE/锁屏优先级）、`9e8f2a3`（64 位登录页三分支认证，经 `261a027`、合并提交 `a787a01` 合入 main）；`53e53cb`、`61c53fa` 只动 l10n 兜底与 bar_height 默认值，未触及认证链。最新待修排序见报告 05 §5.9。
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

1. **32 位桌面登录/解锁/私密空间仍无真实校验（未修，最高优先）**：`login.c:85-101,164-170`、`multi_user.c:232-237` 仍 `(void)password` + WARNING + 返回放行；`0de2133` 只做了告警化，未接真校验。`3d0bd23..61c53fa` 区间这三处无改动（现状为审计在 61c53fa 树上复核）。
2. **安全启动无真实验签/验哈希**：基线为空桩恒通过（`secure_boot.c:49-99`）；`3d0bd23` 已改为返回 `SB_ERR_NOT_IMPLEMENTED(-2)`，签名/哈希算法仍未实现。
3. **64 位登录已接三分支校验，但残留绕过面**：实现来自 `9e8f2a3`（经 `261a027`、`a787a01` 合入 main），`61c53fa` 复核仍在原位；`hpdesk.c:4622-4642` 调 `auth64_verify_password`（`auth64.c:321`）：未配置账户放行 / 已配置 sha256 比对 / 失败留登录页。绕过面：`hepu.lock=0` 或 `/system.conf lock=0` 直进桌面（`hpdesk.c:4587-4601,7624-7627`）；账户未配置放行；32 位编译 `#else` 直解（`:4638-4640`）。基于 `3d0bd23` 的"登录未接校验"旧结论已作废。
4. **常驻验收探针已关闭（`0202461`）**：桌面循环 SB-08 探针改 `if(0)`、shell autostart 四程序改 `autostart_selftest=0`；启动期一次性 BSP 浮点自检保留，不算持续负载。未关闭项：`kernel_main_64.c:1339-1342,1365,1369` 的 autorun 与 uproc 取证探针仍在启动路径。
5. **双架构驱动/能力双份实现且不对称**：seccomp、私有地址空间、SMP、FB_FLIP、留屏 magic 仅 64 位；网卡/AHCI/USB/存储/音频各有两份独立实现。
