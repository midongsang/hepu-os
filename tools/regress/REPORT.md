# HepuOS 回归测试报告（test/regress-2026-11-17）

- 分支：`test/regress-2026-11-17`
- 基线/HEAD：`3d0bd23` fix(arch32): 安全启动三处"验证恒通过"的空桩改成明确返回未实现
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
| 64-Enter 解锁→桌面 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_4.png | 两次 Enter：lock->login (key)、login->desktop (unlocked) 状态迁移日志齐全；桌面 NONBLACK min=2073597 / 阈值 2073597 |
| 64-鼠标解锁路径 | 64 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log | 本次只测键盘 Enter 路径；ebf438c 补的锁屏点击→登录页→点按钮桌面路径未单独取证（未记录坐标/点击事件） |
| 32-菜单 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_4.png | 菜单方向键 down+ret 选择 32 位项；截图 4 张 |
| 32-加载屏 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_4.png | 32 位加载阶段截图 4 张（32 位内容偏稀疏） |
| 32-锁屏 | 32 | **UNVERIFIED** | — | 32 位本树兜底关闭锁屏（串口无 booting to lock 标记），锁屏状态记 N/A，未伪装通过 |
| 32-桌面 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/desktop_4.png | 32 位旧桌面到达（串口 elastic free desktop init done + first frame）；NONBLACK min=786399，本树 32 位为旧稀疏桌面，不套 1080p 全量非黑阈值 |
| 32-登录/认证路径 | 32 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/ser.log | 本次 32 链路直进旧桌面，登录页未被拉起（仓库内 login_show 无外部调用方）；0de2133 三处显式未实现桩（login_authenticate / login_unlock_screen / mu_private_unlock）未被调用，串口无 LOGIN WARNING。认证行为属未验证，不是已通过；要复现需先具备拉起登录页的入口。预期/回归判断：0de2133 意图是把假校验改成显式未实现并保留无密码放行，并非让登录注定失败；现有证据既未触发该路径，也不能据此判定为回归。 |
| 保护模式 64 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/protect64_text.png | 文本终端进入，提示符 hepu@os:~ 在场，help/ls 回显可见；注：64 位 shell 无 ktest 命令 |
| 保护模式 32 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect32/protect32_text.png | 文本终端进入，提示符 + help 命令列表回显在场；取证以串口为准（VGA 文本缓冲未刷入图形帧缓冲，截图为深色残留） |
| 64 位 KUnit | 64 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/ser.log | 64 位保护 shell 执行 ktest 返回 Command not found: ktest；本次未取得 64 位 KUnit 结果 |
| 安全启动验证桩 | host | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log | 3d0bd23 将三处空桩改为返回未实现（-2）；本次非严格模式启动未触发 verify 调用，串口未见 NOT implemented 警告，桩路径未被 exercised |
| 主机端 ASan/UBSan | host | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/host-tests.log | 通过 7 / 失败 1；失败项 elf：host 测试仅链接 elf64.c，缺 compat_gate.c（定义 compat_gate_detect_elf）及进程表依赖，属测试链接清单未随源码重构同步（脚手架/耦合问题，未做隔离编译验证，不据此判定内核运行时回归）；=== 结果: 通过 7 / 失败 1 === |
| 图形栈边界自检 | host | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/boundary.log | check_gfx_boundary.sh 退出码 0 |
| 32 位冒烟六项 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/smoke32.txt | serial=1 initrd=1 net=1 desk=1 fault=0 |
| 32 位 KUnit 套件 | 32 | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/ser32.txt | 套件自判结果=FAIL；失败套件=4 / 用例=6。本次为 3d0bd23 首次留存基线快照（/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/baseline/3d0bd23-kunit32.txt），无更早留存基线可比对新增；失败用例明细见 ser32.txt |

## 像素计数明细

判定口径：桌面 1920×1080 = 2,073,600 像素，64 位桌面 NONBLACK ≥ 2,073,597 判真桌面；锁屏米白底，非黑占比 ≥ 50% 记在场；32 位旧桌面不套 1080p 阈值，仅记录计数。

- **64 位**：桌面帧 4，NONBLACK 最低=2073597（PASS，阈值 2073597）；锁屏帧 4，NONBLACK 最低=2073597（PASS）
- **32 位**：桌面帧 4，NONBLACK 最低=786399（PASS，阈值 N/A（32 位旧桌面不套 1080p 阈值））；锁屏帧 0，NONBLACK 最低=None（UNVERIFIED）

## 加载屏推进证据

- 视频：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/loading.mp4`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_1_menu.png`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_2_preheating.png`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_3_handoff.png`
- 未采到独立静态帧的阶段：Loading 64-bit kernel image、Verifying kernel image（UNVERIFIED）。

## 失败 / 未验证项与原因

- **64-加载阶段帧覆盖（64 / UNVERIFIED）**：已采到阶段：menu（上下文帧）、Preheating kernel pages、Preparing to hand off；未采到独立帧：Loading 64-bit kernel image、Verifying kernel image（亚秒阶段，采样间隔内闪过；视频连续但无静态帧取证）；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_1_menu.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_2_preheating.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_3_handoff.png`
- **64-鼠标解锁路径（64 / UNVERIFIED）**：本次只测键盘 Enter 路径；ebf438c 补的锁屏点击→登录页→点按钮桌面路径未单独取证（未记录坐标/点击事件）；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log`
- **32-锁屏（32 / UNVERIFIED）**：32 位本树兜底关闭锁屏（串口无 booting to lock 标记），锁屏状态记 N/A，未伪装通过；证据 ``
- **32-登录/认证路径（32 / UNVERIFIED）**：本次 32 链路直进旧桌面，登录页未被拉起（仓库内 login_show 无外部调用方）；0de2133 三处显式未实现桩（login_authenticate / login_unlock_screen / mu_private_unlock）未被调用，串口无 LOGIN WARNING。认证行为属未验证，不是已通过；要复现需先具备拉起登录页的入口。预期/回归判断：0de2133 意图是把假校验改成显式未实现并保留无密码放行，并非让登录注定失败；现有证据既未触发该路径，也不能据此判定为回归。；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/ser.log`
- **64 位 KUnit（64 / UNVERIFIED）**：64 位保护 shell 执行 ktest 返回 Command not found: ktest；本次未取得 64 位 KUnit 结果；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/ser.log`
- **安全启动验证桩（host / UNVERIFIED）**：3d0bd23 将三处空桩改为返回未实现（-2）；本次非严格模式启动未触发 verify 调用，串口未见 NOT implemented 警告，桩路径未被 exercised；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log`
- **主机端 ASan/UBSan（host / FAIL）**：通过 7 / 失败 1；失败项 elf：host 测试仅链接 elf64.c，缺 compat_gate.c（定义 compat_gate_detect_elf）及进程表依赖，属测试链接清单未随源码重构同步（脚手架/耦合问题，未做隔离编译验证，不据此判定内核运行时回归）；=== 结果: 通过 7 / 失败 1 ===；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/host-tests.log`
- **32 位 KUnit 套件（32 / FAIL）**：套件自判结果=FAIL；失败套件=4 / 用例=6。本次为 3d0bd23 首次留存基线快照（/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/baseline/3d0bd23-kunit32.txt），无更早留存基线可比对新增；失败用例明细见 ser32.txt；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/ser32.txt`

## 新提交意图对照（基线 3d0bd23）

- **ebf438c（锁屏开关接通 + 鼠标解锁）**：Enter 键盘路径 lock→login→desktop 已取证 PASS；鼠标点击路径未单独取证（UNVERIFIED）。
- **0de2133（32 位认证桩诚实化）**：改动意图是去除假校验、保留无密码放行，不是让登录注定失败；本次登录页未被拉起，桩路径未触发（UNVERIFIED），不判定为回归。
- **0202461（移除常驻验收探针）**：64 桌面仍正常到达；串口未见被移除的 SB-08 / shell autostart 探针常驻输出。
- **3d0bd23（安全启动空桩返回 -2）**：本次非严格模式启动未触发 verify 调用，串口未见 NOT implemented 警告；桩路径未被 exercised（UNVERIFIED）。


## 基线对比

- 本次基线提交：`3d0bd23`；本目录为该基线上首份可复跑回归数据，未与 5f93c2d 旧树结果混比。
- 32 位 KUnit 基线快照：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/baseline/3d0bd23-kunit32.txt`（4 套件失败 / 6 用例失败）。
- 主机端 ASan/UBSan：既往证据未留存跑批日志，本次首建基线（host-tests.log）；7 过 1 败，失败项性质见上表。
- 图形栈边界：本次 boundary.log 退出码 0（PASS）。
- 32 位冒烟六项：serial/initrd/net/desk=1，fault=0。

## 一条命令重跑

```bash
cd <wt-regress worktree>
bash tools/regress/run_all.sh
```
跳过构建：`SKIP_BUILD=1 bash tools/regress/run_all.sh`；自定义证据目录：`HEPU_EV=<dir> bash tools/regress/run_all.sh`；单步：`bash tools/regress/<boot64|boot32|protect64|protect32|smoke32|record_loading>.sh`。
