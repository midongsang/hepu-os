# HepuOS 64 位内核 TLS 端到端测试脚手架（tools/tls_e2e）

分支：`feat/tls-e2e-harness-2026-11-25`，基线：`73753fa`。
本目录只新增测试脚本 / 证书生成 / 驱动脚本与说明，**不修改 `kernel/` 源码**。

## 1. 文件清单

| 文件 | 作用 |
| --- | --- |
| `gen_certs.sh` | 本地 CA（RSA 2048 + SHA-256）+ 合法服务器证书 + 故意错误 SAN 的负向证书；幂等，打印绝对路径与 SHA-256 指纹 |
| `start_s_server.sh` | 启停 `openssl s_server`：`-accept 4433 -www -tls1_2 -msg`，日志落盘，pidfile 只杀自己 |
| `serial_relay.py` | 串口 unix socket 中继：guest TX 落日志，命令 FIFO 回注 guest RX |
| `run_qemu_guest.sh` | QEMU 启动 / 等待 / 断言 / 停止；slirp `e1000`、`-display none`、pidfile 清理 |
| `build_image.sh` | 基线 64 位内核构建 + 出图（flock 包住固定路径），副本落到运行时目录 |
| `run_tls_e2e.sh` | 总驱动：证书 → s_server → QEMU → 串口断言 → 清理；含 `negative` 负向模式 |
| `evidence/serial-baseline-73753fa.log` | 基线 73753fa 实跑串口日志快照（证据） |

## 2. 固定运行时路径（/tmp）

- 运行时根目录：`/tmp/hepu-tls-e2e/`
- 证书目录：`/tmp/hepu-tls-e2e/certs/`
  - `ca.crt / ca.key`，`server.crt / server.key`（SAN: `DNS:localhost, IP:10.0.2.2`），
    `server-bad.crt / server-bad.key`（SAN: `DNS:wrong.example.org, IP:192.0.2.1`）
- 日志：`serial.log`（客户机串口）、`s_server.log`（宿主握手）、`s_client.log`（宿主自握手）、`summary.txt`
- pidfile：`qemu.pid`、`relay.pid`、`s_server.pid`
- 锁：`/tmp/hepu-img64.lock`（镜像构建）、`/tmp/hepu-tls4433.lock`（4433 联调）
- 运行镜像副本：`/tmp/hepu-tls-e2e/hepu64.img`（不直接占用 `/tmp/hepu64_single.img`）

## 3. 复现命令序列

```bash
cd /home/user/Doubao/chats/38452875115179010/hepu-work-tls-e2e

# 正向会话（缺镜像时自动构建；连续跑两次结果一致，之间按 pidfile 自清）
HEPU_SKIP_BUILD=1 bash tools/tls_e2e/run_tls_e2e.sh

# 负向对照：不起 s_server，宿主侧验证 4433 无人监听
HEPU_SKIP_BUILD=1 bash tools/tls_e2e/run_tls_e2e.sh negative

# 仅重建证书（强制）
HEPU_REBUILD_CERTS=1 bash tools/tls_e2e/gen_certs.sh

# 单独构建/刷新镜像
bash tools/tls_e2e/build_image.sh
```

环境变量：`HEPU_SKIP_BUILD=1` 复用已有镜像；`HEPU_BOOT_TIMEOUT=90` 启动等待秒数；
`HEPU_PORT=4433`；`HEPU_EXPECT_TLS=1` 打开客户机 TLS 握手断言（见第 6 节）。

## 4. QEMU 与 s_server 关键参数

- QEMU：`qemu-system-x86_64 -L <datadir> -m 256 -smp 1 -cpu max -no-reboot`
  `-drive file=<img>,format=raw,if=ide,snapshot=on,file.locking=off`
  `-display none -vga std`
  `-monitor unix:<mon.sock>,server,nowait`
  `-serial unix:<serial.sock>,server,nowait`
  `-nic user,model=e1000`（slirp；客户机经网关 `10.0.2.2` 访问宿主）
- s_server：`openssl s_server -accept 0.0.0.0:4433 -www -tls1_2 -msg -cert server.crt -key server.key`
  显式 `-tls1_2`：OpenSSL 3.0 默认先谈 TLS1.3，而客户机切片只按 1.2 解析。

## 5. 基线 73753fa 实测证据（evidence 快照）

快照：`tools/tls_e2e/evidence/serial-baseline-73753fa.log`

- 启动进桌面/锁屏，**没有**串口 shell：
  - `hepu@os` 提示符命中 **0** 行；
  - `[INIT] serial shell (pre-desktop) entered` 命中 **0** 行；
  - 第 424 行 `[Hepu Desktop] booting to lock screen`；
  - 第 430 行 `[DESKTOP64] Desktop running with mouse support`。
- slirp 出站可达（强证据，说明 NAT/网关链路通）：
  - 第 463 行 `[ARP64] resolved 0xA000202 -> ...`（网关 10.0.2.2）；
  - 第 503 行 `[ARP64] resolved 0xA000203 -> ...`（10.0.2.3）；
  - 第 506 行 `[HTTP64] Connecting to 115.231.101.162:80...`；
  - 第 524 行 `[WX] weather fetched temp=9 desc=阴`。
- 键盘/串口回显现象：注入的字符（`z`、`hello`）只在桌面日志流里回显
  （快照第 485、491、498、499 行等），**不进入任何命令行**；monitor sendkey
  同样只在桌面回显，无法驱动 shell。
- 宿主侧 harness 自测：`s_server` 在 4433 监听，`openssl s_client -tls1_2`
  自握手完成；`s_server.log` 记录 `<<< ClientHello` 与 `>>> TLS 1.2 ServerHello`、
  `Certificate`、`ServerHelloDone`。

### 本次稳定运行的证书 SHA-256（证书被复用时不变；`HEPU_REBUILD_CERTS=1` 后以 `summary.txt` 为准）

- `ca.crt`：`C2:D1:D1:1A:07:BC:76:60:1F:32:BF:C7:A9:0F:05:BB:37:4D:06:DA:09:27:9D:81:4F:AC:5F:4B:F5:90:93:E7`
- `server.crt`：`76:65:D8:A6:A0:FC:08:2B:68:7F:61:B0:A8:64:6E:9B:5F:77:20:F8:D4:1E:AE:CB:A1:80:99:4A:1C:B1:AF:41`
- `server-bad.crt`：`13:98:6C:EA:6E:FC:EF:37:5C:81:AC:5E:BB:AA:AD:39:63:32:03:2D:0C:C8:A7:0B:82:A6:A4:3E:70:72:59:69`

## 6. 给 TLS 实现分支预留的断言接口

客户机 `tls12` 命令在 `kernel/arch/x86_64/shell64.c`（命令表约 3320 行，
实现 `tls12_probe` 于 `kernel/net/tls.c`）。桌面构建没有串口 shell，TLS 分支应在
**自己分支**加一个默认关闭的开机自检钩子（编译期宏/镜像探针文件），在自检里跑
`tls12 10.0.2.2 4433`，再由本脚手架断言串口日志：

```bash
# 等待正则命中（超时秒）
bash tools/tls_e2e/run_qemu_guest.sh wait-for "Desktop running" 90

# 在超时内等待所有固定串出现
bash tools/tls_e2e/run_qemu_guest.sh assert-wait 30 \
  "[TLS12] sent ClientHello" \
  "[TLS12] ServerHello: version=0x0303"
```

总驱动已接好开关：`HEPU_EXPECT_TLS=1 bash tools/tls_e2e/run_tls_e2e.sh` 会等待
上述两条 `[TLS12]` 标记；未开开关时只统计计数、不据此判失败。

## 7. 未覆盖 / 失败边界（如实记录，未推测）

1. **客户机侧真实 TLS 往返未取证**：基线镜像直接进桌面，无串口 shell，无法敲入
   `tls12 10.0.2.2 4433`；73753fa 上没有抓到客户机发出的 ClientHello，也没有
   客户机解析到的 ServerHello 日志。
2. **客户机 → 宿主 `10.0.2.2:4433` TCP 可达性：未验证**。已验证的是 slirp 出站
   到外部地址；宿主侧 127.0.0.1:4433 监听与自握手正常。宿主机 loopback 经 slirp
   别名回环这一段，要等自检钩子跑通客户机 TCP 连接后才能下结论。
3. **客户机侧负向对照未验证**：`negative` 模式目前只在宿主侧确认 4433 无监听时
   连接被拒；“无服务器时客户机命令失败”这一条同样缺 shell 通道，待自检钩子接入。
4. **64 位 DHCP/出站时序**：ARP 与外部连接在启动后约 30–50s 出现；机器仅 2 核，
   并行构建会拖慢启动，`HEPU_BOOT_TIMEOUT` 给到 90s 仍偏紧时可调大。
5. **sendkey 丢键风险未在本基线实测**：命令通道走的是串口 FIFO 优先；若未来在
   文本模式构建里回退到 monitor sendkey，逐字符 0.12s 间隔仍有丢键可能，需要
   回显校对。
6. **证书链/SAN 校验未启用**：客户机当前切片只解析 ServerHello，不校验证书；
   `server-bad.crt` 留给后续证书校验阶段使用。
7. 本分支未加任何开机自动执行钩子；桌面进 shell 的驱动方式（串口旁路、boot 参数、
   桌面终端坐标点击）在 73753fa 上均不可用，原因见第 5 节证据。
