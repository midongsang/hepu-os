# hepu-os 只读代码审计报告（2026-11-17）

- 审计基线：分支 `audit/2026-11-17`，基于 main `5f93c2d`（fix(net32): RCTL.BSIZE 写错位段）。
- main 复核：基线之后 main 已前进到 `3d0bd23`；相关条目的最新状态见报告 05 §5.9，涉及提交 `0202461`（关常驻探针）、`0de2133`（32 位认证显式未实现）、`3d0bd23`（安全启动返 -2）。
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

1. **64 位登录路径未接密码校验（未修，最高优先）**：`auth64_verify_password` 只被提权路径调用，开机进入桌面不校验密码（`kernel/arch/x86_64/auth64.c:321,357`；`desktop64.c:242-246`）；main `3d0bd23` 复核仍未接登录/锁屏路径。
2. **32 位认证无真实校验**：基线为忽略密码直接放行（`login.c:76-91,151-168`；`multi_user.c:230-232`）；`0de2133` 已改为显式告警/声明未实现，返回仍放行，真实 hash 校验仍未做。
3. **安全启动无真实验签/验哈希**：基线为空桩恒通过（`secure_boot.c:49-99`）；`3d0bd23` 已改为返回 `SB_ERR_NOT_IMPLEMENTED(-2)`，签名/哈希算法仍未实现。
4. **常驻验收探针已关闭（`0202461`）**：桌面循环 SB-08 探针改 `if(0)`、shell autostart 四程序改 `autostart_selftest=0`；启动期一次性 BSP 浮点自检保留，不算持续负载。未关闭项：`kernel_main_64.c:1339-1342,1365,1369` 的 autorun 与 uproc 取证探针仍在启动路径。
5. **双架构驱动/能力双份实现且不对称**：seccomp、私有地址空间、SMP、FB_FLIP、留屏 magic 仅 64 位；网卡/AHCI/USB/存储/音频各有两份独立实现。
