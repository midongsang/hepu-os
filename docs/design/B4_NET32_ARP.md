# B-4：32 位 ARP / ICMP / TCP 修复记录

日期：2026-11-25

## 1. 现象

32 位内核对 `ping 10.0.2.2` 报 `ARP miss`，`tcp connect` 停在 `SYN_SENT`；
DHCP 能拿到地址，但 `ifconfig` 的 Link 显示 `DOWN`。历史证据在
`evidence-ledger-2026-11-18/32/`。

## 2. 根因

### 2.1 ARP 字段主机序/网络序混用

`kernel/net/arp.c` 里的 `hepu_arp_packet_t` 按主机序使用，但直接写入
`hw_type/proto_type/opcode/sender_ip/target_ip`，没有做 `htons/htonl`。
小端机器发出的 ARP 请求在线上变成：

- `hw_type=0x0100`（应为 `0x0001`）
- `proto_type=0x0008`（应为 `0x0800`）
- `opcode=0x0100/0x0002`（应为 `0x0001/0x0002`）
- `sender_ip` 还是 0

QEMU slirp 不会回复这种畸形请求；接收端又按主机序比较，真实回复也被丢弃。
所以 DHCP 的 UDP 广播能工作，ARP 之后的 ICMP/TCP 首包全部卡住。

### 2.2 ARP 等待与 Ping 标志顺序

`ip_send()` 在 ARP 未命中时只发请求并返回 `-1`；`ping_once()` 在
`ip_send()` 返回后才清 `icmp_reply_received`，而 `ip_send()` 现在会同步
poll 等待 ARP 回复，可能把已经到达的 Echo Reply 清掉。

### 2.3 TCP 字段与校验和

`kernel/net/tcp.c` 的端口、seq/ack、window 直接按主机序写入；校验和伪首部
按主机序累加，TCP 头却按网络序落内存，二者不一致。结果是 SYN 能到主机
但校验和/端口字段不合法，连接建立不了。

### 2.4 ifconfig 只看 RTL8139

`cmd_ifconfig()` 只调用 `rtl8139_get_stats()`。e1000 在位时 RTL8139 不在位，
Link 恒为 `DOWN`，和实际链路状态相反。

## 3. 改动

- `kernel/net/arp.c`：新增 `arp_htons/arp_htonl/arp_ntohs/arp_ntohl`；
  请求/回复的所有线上字段按大端写入；接收时先转主机序再校验/入缓存；
  `sender_ip` 填 `ip_get_local()`；对合法请求回 ARP Reply。
- `kernel/net/ip.c`：ARP 未命中时发请求并最多 poll 100 tick（约 1 秒），
  命中后继续发原包，解决 ping/TCP 首包。
- `kernel/net/icmp.c`：先清 `icmp_reply_received` 再调用 `ip_send()`；
  Echo Reply 增加串口证据行，便于无人值守测试。
- `kernel/net/tcp.c`：端口/seq/ack/window 按大端写入，接收时转主机序；
  重写含伪首部的 TCP 校验和；`tcp_connect()` 发 SYN 后同步 poll 等待
  SYN-ACK 并回 ACK，串口打印 established/timeout。
- `kernel/sched/shell.c`：`ifconfig` 同时读取 RTL8139 和 e1000 的统计，
  链路状态取两者或。

## 4. 验证

测试环境：32 位 `build_v250/kernel.elf`，QEMU 256MB，`-nic user,model=e1000`，
主机侧 19091 端口监听。可复跑：

```bash
bash tools/test_net32.sh 19091
```

本次串口日志 `/tmp/hepu_net32_ser.log`，门禁日志 `/tmp/b4_gate.log`。

```text
hepu@os:~$ dhcp
DHCP configuration complete
hepu@os:~$ ifconfig
UP
hepu@os:~$ ping 10.0.2.2
[IP] ARP miss, sending request
[IP] ARP resolved
[IP] packet sent
[IP] ICMP packet received
[ICMP] echo reply id=0x00001234 seq=0x00000001
[PING] reply
... 后续 3 个 seq 同样 reply
hepu@os:~$ tcp connect 10 0 2 2 19090
[TCP] connect SYN_SENT
[IP] packet sent
[IP] TCP packet received
[IP] packet sent
[TCP] connection established
[TCP] connect established
```

主机侧同时记录到：

```text
listener up
accepted ('127.0.0.1', 44992)
```

结论：32 位 ARP 请求/应答、ICMP Echo（4/4）和 TCP 三次握手均打通。

## 5. 已知边界

- 只在 QEMU user-mode networking + e1000 下验证，未覆盖真机交换机/路由器。
- ARP 缓存超时、ARP 请求重传、TCP 数据重传未在本轮压测；本轮只验证
  首包、连接建立和主机 accept。
- 32 位 shell 的 `ping` 命令只打印 VGA 结果，串口证据由 `[PING] reply`
  和 `[ICMP] echo reply` 提供。
