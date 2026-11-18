# HepuOS 桌面四项功能实测报告（2026-11-18，QEMU/TCG）

- 验证环境：QEMU x86_64，TCG，`-m 512`，单实例；guest IP `10.0.2.15`，网关 `10.0.2.2`，DNS `10.0.2.3`。
- 镜像：`.toolchain/run/shot.img`（含本轮重建的 `build_x86_64/hepu-kernel64.bin`）。
- 交互：QMP abs 指针被 guest 拒绝（`Input handler not found for event type abs`，设备为 PS/2 相对指针），实测走 monitor `mouse_move` 相对位移 + 每步截图闭环核对；按钮 down/up 走 QMP。
- 构建：64 位 `make -C kernel/arch/x86_64`（`flock /tmp/hepu-build.lock`）三轮均 **0 error**；仅有一条与本轮无关的既有多字符常量 warning（`hpdesk.c` `'日'`）。
- 证据目录：`docs/status/evidence-desktop-gui-2026-11-18/`（`shots/`、`serial/`、`tlsserver.log`）。

## 结论一览

| # | 项 | 结论 | 关键证据 |
|---|---|---|---|
| 1 | 设置 → Accounts 页 + 改密输入 | **PASS** | `shots/02-accounts-page.jpg`、`shots/12-accounts-input-mode.jpg`、`shots/13-accounts-password-echo.jpg`、`serial/accounts-mode.txt` |
| 2 | 设置 → Display 屏保循环 | **PASS** | `shots/03…07`（Off→1 min→5 min→10 min→Off） |
| 3 | 顶栏电源菜单弹出/收起 | **PASS**（含一处顶栏裁剪 bug 修复） | `shots/08`（旧版只露 Log out）、`shots/10-power-menu-open-fixed.jpg`、`shots/11-power-menu-closed.jpg` |
| 4 | `tls12` TLS1.2 探针 | **PASS**（ServerHello 解析级；非完整握手） | `shots/14-tls12-serverhello-ok.jpg`、`serial/tls12-probe.txt`、`serial/tls12-server-established.txt`、`tlsserver.log` |

> 四项均在真实 QEMU 会话里取得可视化或串口/服务端证据；未使用"代码看着对"作为 PASS 依据。

---

## 1. 设置 → Accounts 页（`hpdesk.c` page 5 + auth64_user_name）—— PASS

### 判据
- 页面可见 `User: hepu`、角色行、`Change password` 按钮；
- 点按钮进入改密输入模式；键入字符后输入框/回显区可见掩码（不是只有按钮变色）。

### 实测证据
- `shots/02-accounts-page.jpg`：Accounts 页渲染 `Accounts` / `User: hepu` / `Role: (none)` / `Change password`。
- `shots/12-accounts-input-mode.jpg`：点击按钮后进入输入模式，占位文字 `Enter new password`。
- `shots/13-accounts-password-echo.jpg`：键入 `abcdef` 后输入框出现 6 个掩码圆点。
- `serial/accounts-mode.txt`：`[ACCT] password input mode on`（进入模式的串口标记）。

### 口径说明 / 排除项
- 角色显示 `(none)`：auth64 口径 0=`(none)` / 1=`Normal` / 2=`Developer`；全新镜像无持久化账户，显示 `(none)` 是真实状态反映，**不是渲染 bug**。
- 未覆盖：≥6 字符提交账户（`auth64_create_account`）路径、`<6 字符` 的 `Too short` 提示分支；本轮只验证到输入捕获与掩码回显。

### 本轮必要修复（Accounts 输入回显缺口）
旧渲染路径不读 `g_acct_pw_mode`/`g_acct_pw_buf`，即使进入模式也没有可见输入框/回显。在 Accounts 渲染块按钮下方补了输入框：空模式画占位文字，有字符按长度画掩码圆点；进入模式的点击分支补串口标记 `[ACCT] password input mode on`。

## 2. 设置 → Display 屏保（`g_screensaver_min` / `hpdesk_render` / `hpdesk_handle_key`）—— PASS

### 判据
反复点击屏保行，取值在 `Off → 1 min → 5 min → 10 min → Off` 间循环，每步截图。

### 实测证据
- `shots/03-display-screensaver-off.jpg`：基线 Off。
- `shots/04-screensaver-1min.jpg`：1 min。
- `shots/05-screensaver-5min.jpg`：5 min。
- `shots/06-screensaver-10min.jpg`：10 min。
- `shots/07-screensaver-off-cycle-back.jpg`：循环回到 Off。

### 排除项 / 未覆盖
- 可选加强项（设 1 min、静置 ~70s 看黑屏、任意键唤醒）本轮未做；结束时屏保已为 Off（代码默认 `g_screensaver_min = 0`，全新启动即 Off）。

## 3. 顶栏电源菜单（`g_power_menu_open` + `hs_top_bar_render` / `hpdesk_mouse_down`）—— PASS（修复裁剪 bug）

### 现象（旧版）
`shots/08-power-menu-open.jpg`：菜单只露出第一行 `Log out`，`Reboot` / `Shut down` 被裁掉。

### 根因
菜单绘制在 `hs_top_bar_render()` 内，`my = bar_top + bar_h + hs_sy(4)`，y 起点本身正确（在顶栏下方），**不是起点算进顶栏**。真正裁剪发生在图层合成：`HS_PANEL_TOP_BAR` 面板送进顶栏层，`hs_layer_draw_a()` 的 `ph` 来自 `hs_panel_bounds()` 的顶栏常态高度；超出层 surface 高度的两行被 gfx64 叶子函数静默丢弃。

### 修复（只改菜单这一处，未动全局裁剪逻辑）
在 `hs_panel_bounds()` 的 `HS_PANEL_TOP_BAR` 分支：仅当 `g_power_menu_open` 时条件化把 `*oh` 扩到容纳菜单三层高，常态几何不变。64 位层走 kmalloc（堆 32MB），扩层约 1.77MB，不越界。命中区与绘制几何同源，未改。

### 实测证据（修复后）
- `shots/10-power-menu-open-fixed.jpg`：`Log out / Reboot / Shut down` 三项全部可见。
- `shots/11-power-menu-closed.jpg`：点菜单外空白区，菜单收起。
- 对照旧版 `shots/08-power-menu-open.jpg` / `shots/09-power-menu-closed.jpg`。
- **全程未点** `Log out` / `Reboot` / `Shut down`。

## 4. `tls12` TLS1.2 探针（`kernel/net/tls.c` `tls12_probe`）—— PASS（ServerHello 解析级）

### 判据
桌面终端敲 `tls12`（默认 `10.0.2.2:4433`）；串口出现 TCP 建连 / ClientHello / 收包 / ServerHello 解析；服务端有入站连接记录。

### 实测证据
- `shots/14-tls12-serverhello-ok.jpg`：终端 `tls12: probing 10.0.2.2 ...` → `tls12: ServerHello parsed OK`。
- `serial/tls12-probe.txt`：
  - `[TLS12] DNS 10.0.2.2 -> resolved`
  - `[TLS12] tcp established`
  - `[TLS12] sent ClientHello (101 bytes)`
  - `[TLS12] received 512 bytes`
  - `tls12: ServerHello parsed OK`
- `serial/tls12-server-established.txt`：宿主侧 `ss -tnp` 记录 `qemu-system-x86…:39368 ↔ openssl…:4433` ESTABLISHED，证明 slirp 已把 guest TCP 转发到宿主且 s_server 接受。
- `tlsserver.log`：`openssl s_server -accept 4433 -cert cert.pem -key key.pem -www`（证书 CN=test，自签 1 天）。

### 本轮必要修复（两处，均在 `kernel/net/tls.c`）
1. **TCP 状态码口径错误**：探针等待 `tcp64_get_state() == 1`，但 `net64_tcp.c` 状态码 `TCP_LISTEN=1`、`TCP_ESTABLISHED=4`；连接已建立也会被判成未建立。改为比较 `==4`。
2. **SNI 扩展数据长度多算 1 字节**：旧 `sni_len = host_len + 2 + 1 + 3`，大于实际扩展体（`list_len(2)+name_type(1)+name_len(2)+name(host_len)=host_len+5`）。OpenSSL 越界读下一扩展，报 `bad extension` 并回 alert。改为 `host_len + 5`。

### 阶段拆解（断在哪一步）
- 修复前：`DNS resolved` → `[TLS12] tcp not established` → `probe failed`；服务端无入站连接。先排除"服务端卡死"（旧 s_server 本机回环 `openssl s_client` 超时；重启后本机回环 TLSv1.3 握手成功）。
- 修状态码后：TCP 建连、ClientHello 发出，但服务端 `tls_collect_extensions: bad extension`，只收到 7 字节 alert。
- 修 SNI 长度后：TCP 建连 → ClientHello 101B → 收 512B → ServerHello 解析 OK；服务端持有 ESTABLISHED 连接，无 bad-extension 报错。

### 口径说明 / 遗留
- 探针只解析 ServerHello，**未走完整握手**（Certificate 校验、Finished、密钥交换）。
- 遗留小问题（不影响本次 PASS 判据）：`[TLS12] ServerHello: version=0x%4x cipher=0x%4x` 一行的 `%4x` 未被 `vga_printf` 展开（打印出字面量），属日志格式化瑕疵；同段 `comp=771`（=0x0303）标签口径待后续核对。

---

## 本轮代码改动清单（仅本轮相关，未触碰别线文件）

| 文件 | 改动 |
|---|---|
| `kernel/arch/x86_64/hpdesk.c` | ① 电源菜单层包围盒条件化扩高（顶栏裁剪 bug）；② Accounts 输入框 + 掩码圆点渲染；③ 改密模式进入串口标记；④ `hpdesk_handle_pending_app(int app)` 改参数传入 |
| `kernel/arch/x86_64/hpdesk.h` | `hpdesk_handle_pending_app(int app)` 声明同步 |
| `kernel/arch/x86_64/desktop64.c` | pending app 调用处把已取走的 `pend` 传入消费方（旧路径 handler 回去读已清零的 `g_pending_app`，启动请求被静默丢弃） |
| `kernel/net/tls.c` | ① TCP_ESTABLISHED 状态码 1→4；② SNI 扩展数据长度多算 1 字节修正 |

### 附带修复（接线 bug，使终端可从 Dock 拉起）
`desktop64.c` 主循环用 `__atomic_exchange_n(&g_pending_app, 0, …)` 取走请求并清零，旧 handler 再回去读全局（已是 0=`HS_APP_NONE`）直接 early return，Dock/启动器点任何图标都不启动。改为把 `pend` 作为参数传入 handler。修复后实测点 T 图标出现 `[HSAPPS] terminal created`，终端窗口正常打开。

### 未触碰（别线改动）
`kernel/arch/aarch64/m1/*`、`epoll64.*`、`user/apps/epolltest.c`、`docs/planning/ROADMAP*`、`docs/architecture/ARM64_REALBOARD_EVAL.md` 等一律未改、未提交。

## 服务端与进程收尾状态
- 宿主 4433：`openssl s_server -accept 4433 -cert /tmp/tlstest/cert.pem -key /tmp/tlstest/key.pem -www`，本机回环 `openssl s_client` 握手成功（TLSv1.3，CN=test）。
- 本轮 QEMU 实例在归档完成后关闭；仅保留其他 agent 线的 QEMU（`hepu_vfs.img`，VNC 5903），未 kill、未复用其端口/socket。
- 提交仅 `git add <具体路径>`，未 `git add -A`；未 push（环境无推送凭据）。

## 遗留问题与下一步
1. **TLS 完整握手**：当前探针止于 ServerHello 解析；需补 Certificate 处理、密钥交换（ECDHE/PSK）与 Finished 校验，才能算端到端 TLS 1.2 会话。
2. **Accounts 提交路径**：≥6 字符建账户、`<6 字符` 提示分支未实测；角色写入与持久化待验。
3. **屏保黑屏唤醒**：1 min 静置黑屏 + 按键唤醒加强项未跑。
4. **TLS 日志格式化**：`vga_printf` 不支持 `%4x`，ServerHello 版本/密码套件打印出字面量；`comp` 标签口径待核。
