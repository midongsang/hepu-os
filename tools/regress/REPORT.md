# HepuOS 回归测试报告（test/regress-2026-11-17）

- 分支：`test/regress-2026-11-17`
- 基线提交：`a787a01` merge(login): 登录密码三分支校验 + 锁屏/登录中文化 + 首设向导优先级修复
- 本次回归提交：`1020fc1` test(regress): 3d0bd23 回归测试脚本与首份基线报告
- 证据目录：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17`
- 运行环境：2 核 / 无 /dev/kvm / 全 TCG；QEMU 全机串行（/tmp/hepu-qemu.lock），编译串行（/tmp/hepu-build.lock）
- 三态口径：**PASS** 取证通过；**FAIL** 真实失败；**UNVERIFIED** 未能取证，不计通过

## 三态结果表

| 项目 | 架构 | 状态 | 证据路径 | 说明 |
|---|---|---|---|---|
| 64-菜单 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_4.png | UEFI 引导菜单 3 项可选，截图 4 张 |
| 64-加载屏（连续视频） | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/loading.mp4 | 加载过程连续录屏；TCG + 外部抢核下阶段推进很快，见抽帧明细 |
| 64-加载阶段帧覆盖 | 64 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_1_menu.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_2_preheating.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_3_handoff.png | 已采到阶段：menu（上下文帧）、Preheating kernel pages、Preparing to hand off；未采到独立帧：Loading 64-bit kernel image、Verifying kernel image（亚秒阶段，采样间隔内闪过；视频连续但无静态帧取证） |
| 64-锁屏 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_4.png | 米白底锁屏；串口 booting to lock 标记在场；NONBLACK min=2073597 |
| 64-登录页 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_4.png | 锁屏任意键进入登录页；登录页帧 4 张，另空口令拒绝提示帧 1 张 |
| 64-空 Enter 登录探测 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_reject_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log | 只按 Enter（空口令）不能进桌面：已配置账户分支（configured），空口令被拒并留在登录页；串口 `login: auth failed` 在场；账户加载标记=1。 |
| 64-密码登录→桌面 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_4.png | 输入夹具口令 `hepu123` 后提交进入桌面（串口 login -> desktop）；该口令为本回归 fixture 选择，仓库无内置默认口令；桌面 NONBLACK min=2073597 / 阈值 2073597 |
| 64-鼠标解锁路径 | 64 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log | 本次只测键盘路径；锁屏点击→登录页→点按钮桌面路径未单独取证（未记录坐标/点击事件） |
| 32-菜单 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_4.png | 菜单方向键 down+ret 选择 32 位项；截图 4 张 |
| 32-加载屏 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_4.png | 32 位加载阶段截图 4 张（32 位内容偏稀疏） |
| 32-锁屏 | 32 | **UNVERIFIED** | — | 32 位本树兜底关闭锁屏（串口无 booting to lock 标记），锁屏状态记 N/A，未伪装通过 |
| 32-桌面（multiboot 直启补充取证） | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_4.png | 直启 kernel.elf 图形桌面到达（串口 elastic free desktop init done + first frame）；NONBLACK min=2073598 / 2,073,600（≈100.00%），32 位桌面不套 1080p 全量非黑阈值。旧基线 786,399（≈38%）为部分首帧稀疏画面，本基线上直启桌面已整帧上色，旧数值不成立。 |
| 32-UEFI 链路桌面帧 | 32 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_4.png | UEFI 引导链路在 linuxhello 处崩溃并重启回灌引导菜单，该路径下 desktop_* 帧为菜单残留，NONBLACK=2073600 不计入真实桌面；直启桌面见上一行 |
| 32-linuxhello 用户态崩溃 | 32 | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/ser.log | 直启与 UEFI 两条路径均在 [AUTO] linuxhello start 后退出/重启：直启 -no-reboot 下 QEMU 退出，UEFI 路径重启回灌菜单。旧基线同位置 linuxhello 故障被回收（rc=3 继续跑后续应用），本次 escalate 为重启；a787a01 未改动 32 位内核源码（仅 Makefile 兼容层目录迁移，行为声明不变），根因未隔离编译验证，暂记为既有不稳定项存疑，不直接判为本次合并回归。 |
| 32-登录/认证路径 | 32 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/ser.log | 本次 32 链路直进旧桌面，登录页未被拉起（仓库内 login_show 无外部调用方）；0de2133 三处显式未实现桩（login_authenticate / login_unlock_screen / mu_private_unlock）未被调用，串口无 LOGIN WARNING。认证行为属未验证，不是已通过。预期/回归判断：改动意图是把假校验改成显式未实现并保留无密码放行，并非让登录注定失败；现有证据既未触发该路径，也不能据此判定为回归。 |
| 保护模式 64 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/protect64_text.png | 文本终端进入，提示符 hepu@os:~ 在场，help/ls 回显可见；注：64 位 shell 无 ktest 命令 |
| 保护模式 32 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect32/protect32_text.png | 文本终端进入，提示符 + help 命令列表回显在场；取证以串口为准（VGA 文本缓冲未刷入图形帧缓冲，截图为深色残留） |
| 64 位 KUnit | 64 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/ser.log | 64 位保护 shell 执行 ktest 返回 Command not found: ktest；本次未取得 64 位 KUnit 结果 |
| 安全启动验证桩 | host | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log | 3d0bd23 将三处空桩改为返回未实现（-2）；本次非严格模式启动未触发 verify 调用，串口未见 NOT implemented 警告，桩路径未被 exercised |
| 主机端 ASan/UBSan | host | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/host-tests.log | 通过 7 / 失败 1；失败项 elf：host 测试仅链接 elf64.c，缺 compat_gate.c（定义 compat_gate_detect_elf）及进程表依赖，属测试链接清单未随源码重构同步（脚手架/耦合问题，未做隔离编译验证，不据此判定内核运行时回归）；=== 结果: 通过 7 / 失败 1 === |
| 图形栈边界自检 | host | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/boundary.log | check_gfx_boundary.sh 退出码 0 |
| 32 位冒烟六项 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/smoke32.txt | serial=1 initrd=1 net=1 desk=1 fault=0 |
| 32 位 KUnit 套件 | 32 | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/ser32.txt | 套件自判结果=FAIL；失败套件=4 / 用例=6。与 3d0bd23-kunit32.txt 基线快照失败项完全一致，属**既有基线项，非本轮新增**；快照 /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/baseline/a787a01-kunit32.txt；失败用例明细见 ser32.txt |

## 像素计数明细

判定口径：桌面 1920×1080 = 2,073,600 像素，64 位桌面 NONBLACK ≥ 2,073,597 判真桌面；锁屏米白底，非黑占比 ≥ 50% 记在场；32 位旧桌面不套 1080p 阈值，仅记录计数。

- **64 位**：桌面帧 4，NONBLACK 最低=2073597（PASS，阈值 2073597）；锁屏帧 4，NONBLACK 最低=2073597（PASS）；登录页/拒绝提示帧 5，NONBLACK 最低=2073597
- **32 位**：桌面帧 4，NONBLACK 最低=2073600（UNVERIFIED，阈值 N/A（UEFI 链路线帧为崩溃重启残留菜单，不计入））；锁屏帧 0，NONBLACK 最低=None（UNVERIFIED）；登录页/拒绝提示帧 0，NONBLACK 最低=None
- **32 位直启补充**：桌面帧 4，NONBLACK 最低=2073598（PASS），占比 100.00%；UEFI 链路残留菜单帧不计入桌面计数。

## 加载屏推进证据

- 视频：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/loading.mp4`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_1_menu.png`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_2_preheating.png`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_3_handoff.png`
- 未采到独立静态帧的阶段：Loading 64-bit kernel image、Verifying kernel image（UNVERIFIED）。

## 失败 / 未验证项与原因

- **64-加载阶段帧覆盖（64 / UNVERIFIED）**：已采到阶段：menu（上下文帧）、Preheating kernel pages、Preparing to hand off；未采到独立帧：Loading 64-bit kernel image、Verifying kernel image（亚秒阶段，采样间隔内闪过；视频连续但无静态帧取证）；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_1_menu.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_2_preheating.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_3_handoff.png`
- **64-鼠标解锁路径（64 / UNVERIFIED）**：本次只测键盘路径；锁屏点击→登录页→点按钮桌面路径未单独取证（未记录坐标/点击事件）；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log`
- **32-锁屏（32 / UNVERIFIED）**：32 位本树兜底关闭锁屏（串口无 booting to lock 标记），锁屏状态记 N/A，未伪装通过；证据 ``
- **32-UEFI 链路桌面帧（32 / UNVERIFIED）**：UEFI 引导链路在 linuxhello 处崩溃并重启回灌引导菜单，该路径下 desktop_* 帧为菜单残留，NONBLACK=2073600 不计入真实桌面；直启桌面见上一行；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_4.png`
- **32-linuxhello 用户态崩溃（32 / FAIL）**：直启与 UEFI 两条路径均在 [AUTO] linuxhello start 后退出/重启：直启 -no-reboot 下 QEMU 退出，UEFI 路径重启回灌菜单。旧基线同位置 linuxhello 故障被回收（rc=3 继续跑后续应用），本次 escalate 为重启；a787a01 未改动 32 位内核源码（仅 Makefile 兼容层目录迁移，行为声明不变），根因未隔离编译验证，暂记为既有不稳定项存疑，不直接判为本次合并回归。；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/ser.log`
- **32-登录/认证路径（32 / UNVERIFIED）**：本次 32 链路直进旧桌面，登录页未被拉起（仓库内 login_show 无外部调用方）；0de2133 三处显式未实现桩（login_authenticate / login_unlock_screen / mu_private_unlock）未被调用，串口无 LOGIN WARNING。认证行为属未验证，不是已通过。预期/回归判断：改动意图是把假校验改成显式未实现并保留无密码放行，并非让登录注定失败；现有证据既未触发该路径，也不能据此判定为回归。；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/ser.log`
- **64 位 KUnit（64 / UNVERIFIED）**：64 位保护 shell 执行 ktest 返回 Command not found: ktest；本次未取得 64 位 KUnit 结果；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/ser.log`
- **安全启动验证桩（host / UNVERIFIED）**：3d0bd23 将三处空桩改为返回未实现（-2）；本次非严格模式启动未触发 verify 调用，串口未见 NOT implemented 警告，桩路径未被 exercised；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log`
- **主机端 ASan/UBSan（host / FAIL）**：通过 7 / 失败 1；失败项 elf：host 测试仅链接 elf64.c，缺 compat_gate.c（定义 compat_gate_detect_elf）及进程表依赖，属测试链接清单未随源码重构同步（脚手架/耦合问题，未做隔离编译验证，不据此判定内核运行时回归）；=== 结果: 通过 7 / 失败 1 ===；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/host-tests.log`
- **32 位 KUnit 套件（32 / FAIL）**：套件自判结果=FAIL；失败套件=4 / 用例=6。与 3d0bd23-kunit32.txt 基线快照失败项完全一致，属**既有基线项，非本轮新增**；快照 /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/baseline/a787a01-kunit32.txt；失败用例明细见 ser32.txt；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/ser32.txt`

## 新提交意图对照（基线 a787a01）

- **9e8f2a3 登录页接入账户密码三分支认证**：未配置账户放行 / 已配置 sha256 比对 / 失败留登录页；本轮按该三分支实测空 Enter 与夹具密码两条路径，结果见上表。
- **dec60c9 锁屏/登录文案接入多语言表**：登录页为中文化后渲染；自动化按键不受文案影响。
- **10e42fa 向导与锁屏状态优先级修复**：锁屏/登录态优先于 OOBE 模态，按键不被向导吞掉。
- **261a027 / a787a01 合并**：并入首设向导优先级修复；64 桌面仍正常到达。


## 基线对比

- 本次基线提交：`a787a01`；上一基线 3d0bd23 证据已归档至 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/old-3d0bd23/`，未与本次结果混比。
- 32 位 KUnit 基线快照：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/baseline/a787a01-kunit32.txt`（4 套件失败 / 6 用例失败；比对结论：same_as_baseline）。
- 主机端 ASan/UBSan：7 过 1 败，失败项性质见上表。
- 图形栈边界：本次 boundary.log 退出码 0（PASS）。
- 32 位冒烟六项：serial/initrd/net/desk=1/1/1/1，fault=0。

## 一条命令重跑

```bash
cd <wt-regress worktree>
bash tools/regress/run_all.sh
```
跳过构建：`SKIP_BUILD=1 bash tools/regress/run_all.sh`；自定义证据目录：`HEPU_EV=<dir> bash tools/regress/run_all.sh`；单步：`bash tools/regress/<boot64|boot32|desktop32_direct|protect64|protect32|smoke32|record_loading>.sh`。
