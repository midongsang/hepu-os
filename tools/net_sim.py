#!/usr/bin/env python3
"""赫普OS 网络栈验证模拟器
- 模拟 DHCP 服务器 (10.0.2.2): DISCOVER->OFFER, REQUEST->ACK
- 模拟 DNS 服务器 (10.0.2.3): A 记录应答
- 模拟 ARP 应答
- 模拟 TCP/HTTP 服务器 (10.0.2.3:80): 三次握手 + HTTP 响应
用法: python3 tools/net_sim.py [端口]   # 默认 4000, QEMU -netdev socket,connect=127.0.0.1:4000
"""
import socket, struct, sys, threading, time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
CLIENT_MAC = b'\x52\x54\x00\x12\x34\x56'   # QEMU e1000 默认 MAC
SIM_MAC = b'\x02\x00\x00\x00\x00\x02'
SIM_IP = 0x0A000203   # 10.0.2.3
GW_IP = 0x0A000202    # 10.0.2.2
CLIENT_IP = 0x0A00020F  # 10.0.2.15 (DHCP 分配)
NETMASK = 0xFFFFFF00

def ip_ntoa(ip):
    return f"{(ip>>24)&0xFF}.{(ip>>16)&0xFF}.{(ip>>8)&0xFF}.{ip&0xFF}"

DNS_TABLE = {
    b"api.open-meteo.com": 0x0A000203,
    b"open-meteo.com": 0x0A000203,
    b"example.com": 0x0A000203,
}

# TCP 连接状态: {sport: {state, seq, ack, ...}}
tcp_conns = {}

def chksum(data):
    if len(data) % 2:
        data += b'\x00'
    s = 0
    for i in range(0, len(data), 2):
        s += (data[i] << 8) | data[i+1]
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF

def ip_chksum(header):
    return chksum(header)

def make_eth(dst, eth_type, payload):
    return dst + SIM_MAC + struct.pack('!H', eth_type) + payload

def make_arp_reply(req_ip):
    # ARP 请求解析
    # 以太网帧: dst+src+type(0x0806) + arp
    op = struct.unpack('!H', req_ip[6:8])[0]
    sha = req_ip[8:14]
    spa = struct.unpack('!I', req_ip[14:18])[0]
    tpa = struct.unpack('!I', req_ip[24:28])[0] if len(req_ip) >= 28 else SIM_IP
    # 应答: spa 用被查询的 IP (网关 10.0.2.2 与服务器 10.0.2.3 同为本模拟器)
    arp = struct.pack('!HHBBH', 1, 0x0800, 6, 4, 2)
    arp += SIM_MAC + struct.pack('!I', tpa)
    arp += sha + struct.pack('!I', spa)
    return make_eth(sha, 0x0806, arp)

def make_udp(src_port, dst_port, src_ip, dst_ip, payload):
    udp_len = 8 + len(payload)
    udp = struct.pack('!HHHH', src_port, dst_port, udp_len, 0)
    # IP 头
    ip = struct.pack('!BBHHHBBH', 0x45, 0, 20 + udp_len, 0, 0, 64, 17, 0)
    ip += struct.pack('!II', src_ip, dst_ip)
    ip_csum = ip_chksum(ip)
    ip = ip[:10] + struct.pack('!H', ip_csum) + ip[12:]
    return ip + udp + payload

def make_dhcp_reply(req_pkt, msg_type, yiaddr, xid):
    """构造 DHCP OFFER/ACK 应答 (req_pkt = 客户端 DHCP 包)"""
    # DHCP 包 (240 字节头 + 选项)
    pkt = bytearray(240 + 64)
    pkt[0] = 2          # op = 应答
    pkt[1] = 1          # htype = 以太网
    pkt[2] = 6          # hlen
    pkt[3] = 0          # hops
    pkt[4:8] = xid      # xid (网络序)
    pkt[8:10] = b'\x00\x00'  # secs
    pkt[10:12] = b'\x00\x00' # flags
    pkt[12:16] = struct.pack('!I', 0)       # ciaddr
    pkt[16:20] = struct.pack('!I', yiaddr)  # yiaddr
    pkt[20:24] = struct.pack('!I', SIM_IP)  # siaddr (服务器)
    pkt[24:28] = struct.pack('!I', 0)       # giaddr
    # chaddr = 客户端 MAC
    pkt[28:34] = CLIENT_MAC
    pkt[236:240] = struct.pack('!I', 0x63825363)  # magic cookie
    # 选项
    o = 240
    def opt(code, val):
        nonlocal o
        pkt[o] = code; pkt[o+1] = len(val); pkt[o+2:o+2+len(val)] = val
        o += 2 + len(val)
    opt(53, bytes([msg_type]))                          # DHCP 消息类型
    opt(54, struct.pack('!I', SIM_IP))                  # server id = 10.0.2.2
    opt(1, struct.pack('!I', NETMASK))                  # subnet mask
    opt(3, struct.pack('!I', GW_IP))                    # router
    opt(6, struct.pack('!I', SIM_IP))                   # DNS = 10.0.2.3
    opt(51, struct.pack('!I', 86400))                   # lease time
    pkt[o] = 255                                        # END
    return bytes(pkt[:o+1])

def dns_encode_name(name):
    out = b''
    for label in name.split(b'.'):
        out += bytes([len(label)]) + label
    return out + b'\x00'

def parse_dns_name(pkt, off):
    labels = []
    while True:
        l = pkt[off]
        if l == 0:
            off += 1
            break
        if l & 0xC0 == 0xC0:
            ptr = ((l & 0x3F) << 8) | pkt[off+1]
            off += 2
            # 解引用 (简化: 只取一次)
            while True:
                l2 = pkt[ptr]
                if l2 == 0: break
                if l2 & 0xC0 == 0xC0:
                    ptr = ((l2 & 0x3F) << 8) | pkt[ptr+1]
                    continue
                labels.append(pkt[ptr+1:ptr+1+l2])
                ptr += 1 + l2
            break
        labels.append(pkt[off+1:off+1+l])
        off += 1 + l
    return b'.'.join(labels), off

def make_dns_reply(req):
    """构造 DNS 应答"""
    tid = req[0:2]
    qdcount = struct.unpack('!H', req[4:6])[0]
    if qdcount == 0:
        return None
    off = 12
    qname, off = parse_dns_name(req, off)
    qtype, qclass = struct.unpack('!HH', req[off:off+4])
    off += 4
    ip = DNS_TABLE.get(qname, None)
    if ip is None or qtype != 1:
        # NXDOMAIN
        hdr = tid + struct.pack('!HHHHH', 0x8183, 1, 0, 0, 0)
        return hdr + dns_encode_name(qname) + struct.pack('!HH', 1, 1)
    hdr = tid + struct.pack('!HHHHH', 0x8180, 1, 1, 0, 0)
    body = dns_encode_name(qname) + struct.pack('!HH', 1, 1)
    # 回答: 名称压缩指针 C00C + A 记录
    answer = b'\xC0\x0C' + struct.pack('!HHIH', 1, 1, 300, 4) + struct.pack('!I', ip)
    return hdr + body + answer

def make_tcp_reply(src_ip, dst_ip, sport, dport, seq, ack, flags, payload=b''):
    """构造 TCP 段 (4 字节头长)"""
    offset = 5  # 20 字节头
    tcp = struct.pack('!HHIIBBHHH', sport, dport, seq, ack,
                      (offset << 4) | 0, flags, 65535, 0, 0)
    # IP 头
    total = 20 + 20 + len(payload)
    ip = struct.pack('!BBHHHBBH', 0x45, 0, total, 0, 0, 64, 6, 0)
    ip += struct.pack('!II', src_ip, dst_ip)
    ip_csum = ip_chksum(ip)
    ip = ip[:10] + struct.pack('!H', ip_csum) + ip[12:]
    return ip + tcp + payload

FLAGS_FIN = 0x01
FLAGS_SYN = 0x02
FLAGS_ACK = 0x10
FLAGS_PSH = 0x08
FLAGS_RST = 0x04

def handle_tcp(ip_pkt, src_ip, dst_ip):
    """TCP 状态机 (简化 HTTP 服务)"""
    # ip_pkt 已是 IP 载荷(TCP 头起始), 不能再用 [20:] 偏移
    tcp = ip_pkt
    if len(tcp) < 20:
        return None
    sport, dport, seq, ack = struct.unpack('!HHII', tcp[0:12])
    off_flags = tcp[12]
    flags = tcp[13]
    hdr_len = ((off_flags >> 4) & 0xF) * 4
    payload = tcp[hdr_len:]
    key = (sport, dport)
    state = tcp_conns.get(key, None)

    if dport == 80:  # HTTP
        if flags & FLAGS_SYN and not (flags & FLAGS_ACK):
            # SYN -> SYN+ACK
            tcp_conns[key] = {'state': 'SYN_RCVD', 'sseq': 1000, 'cseq': seq + 1}
            print(f"[net_sim] TCP SYN from {ip_ntoa(src_ip)}:{sport} -> SENDING SYN-ACK", flush=True)
            return make_tcp_reply(dst_ip, src_ip, dport, sport, 1000, seq + 1, FLAGS_SYN | FLAGS_ACK)
        if state and (flags & FLAGS_ACK):
            if state['state'] == 'SYN_RCVD':
                state['state'] = 'ESTAB'
                state['cseq'] = seq
                # 等 HTTP 请求数据
                if payload:
                    return make_http_response(dst_ip, sport, state, payload)
                return None
            if state['state'] == 'ESTAB':
                if payload:
                    return make_http_response(dst_ip, sport, state, payload)
                return None
        if flags & FLAGS_FIN:
            tcp_conns.pop(key, None)
            return make_tcp_reply(dst_ip, src_ip, dport, sport, seq, ack + 1, FLAGS_ACK | FLAGS_FIN)
    return None

def make_http_response(src_ip, sport, state, req_payload):
    body = b"<html><body><h1>HepuOS network test OK</h1><p>Hello from net_sim!</p></body></html>"
    resp = b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body
    sseq = state['sseq']
    state['sseq'] = sseq + len(resp)
    state['cseq'] = state['cseq'] + len(req_payload) if state.get('cseq') else 0
    return make_tcp_reply(src_ip, src_ip, 80, sport, sseq, state['cseq'], FLAGS_PSH | FLAGS_ACK, resp)

def handle_frame(frame):
    """处理收到的以太网帧"""
    if len(frame) < 14:
        return None
    eth_type = struct.unpack('!H', frame[12:14])[0]
    global CLIENT_MAC
    CLIENT_MAC = frame[6:12]  # 记录客户端 MAC

    if eth_type == 0x0806:  # ARP
        a = frame[14:]
        tpa = struct.unpack('!I', a[24:28])[0] if len(a) >= 28 else 0
        print(f"[net_sim] ARP req for {ip_ntoa(tpa)} -> SENDING REPLY", flush=True)
        return make_arp_reply(frame[14:])
    if eth_type != 0x0800:
        return None
    ip = frame[14:]
    if len(ip) < 20:
        return None
    ver_ihl = ip[0]
    ihl = (ver_ihl & 0xF) * 4
    proto = ip[9]
    src_ip = struct.unpack('!I', ip[12:16])[0]
    dst_ip = struct.unpack('!I', ip[16:20])[0]
    if proto == 17:  # UDP
        udp = ip[ihl:]
        if len(udp) < 8:
            return None
        sport, dport = struct.unpack('!HH', udp[0:4])
        udp_len = struct.unpack('!H', udp[4:6])[0]
        payload = udp[8:udp_len]
        if dport == 67:  # DHCP
            mt = payload[242] if len(payload) > 242 else -1
            print(f"[net_sim] DHCP op={payload[0] if payload else '?'} htype={payload[1] if len(payload)>1 else '?'} xid={payload[4:8].hex() if len(payload)>=8 else '?'} mtype={mt} cookie={payload[236:240].hex() if len(payload)>=240 else '?'}", flush=True)
            if len(payload) >= 240:
                xid = payload[4:8]
                # 找 DHCP 消息类型
                msg_type = None
                opts = payload[240:]
                i = 0
                while i < len(opts) - 2:
                    if opts[i] == 255: break
                    if opts[i] == 0: i += 1; continue
                    if opts[i] == 53 and opts[i+1] == 1:
                        msg_type = opts[i+2]; break
                    i += 2 + opts[i+1]
                print(f"[net_sim] opts={opts[:8].hex()} msg_type={msg_type}", flush=True)
                if msg_type == 1:   # DISCOVER -> OFFER
                    reply = make_dhcp_reply(payload, 2, CLIENT_IP, xid)
                    udp_frame = make_udp(67, 68, SIM_IP, 0xFFFFFFFF, reply)
                    full = b'\xff'*6 + SIM_MAC + struct.pack('!H', 0x0800) + udp_frame
                    return make_eth(b'\xff'*6, 0x0800, udp_frame)
                elif msg_type == 3: # REQUEST -> ACK
                    reply = make_dhcp_reply(payload, 5, CLIENT_IP, xid)
                    udp_frame = make_udp(67, 68, SIM_IP, 0xFFFFFFFF, reply)
                    return make_eth(b'\xff'*6, 0x0800, udp_frame)
        elif dport == 53:  # DNS
            reply = make_dns_reply(payload)
            if reply:
                udp_frame = make_udp(53, sport, SIM_IP, src_ip, reply)
                return make_eth(CLIENT_MAC, 0x0800, udp_frame)
    elif proto == 6:  # TCP
        reply = handle_tcp(ip[ihl:], src_ip, dst_ip)
        if reply:
            return make_eth(CLIENT_MAC, 0x0800, reply)
    return None

def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', PORT))
    srv.listen(1)
    print(f"[net_sim] listening on 127.0.0.1:{PORT}, waiting QEMU connect...", flush=True)
    conn, addr = srv.accept()
    print(f"[net_sim] QEMU connected from {addr}", flush=True)

    counts = {'arp': 0, 'dhcp': 0, 'dns': 0, 'tcp': 0}
    # V0.13.65: 阻塞 recv + 累积缓冲, 防短包/流式分片 (QEMU socket backend 按帧发送但 TCP 可能分段)
    buf = b''
    conn.settimeout(2.0)
    while True:
        try:
            data = conn.recv(65536)
            print(f"[net_sim] recv returned {len(data)} bytes: {data[:32].hex()}", flush=True)
            if not data:
                print("[net_sim] connection closed", flush=True)
                break
            buf += data
            # 尽量按以太网帧消费 (14+ 字节, 累积到至少 1 帧)
            while len(buf) >= 14:
                frame = buf[:len(buf)]   # socket backend 一帧 = 一个 recv 单元, 直接整体处理
                buf = b''
                # QEMU socket backend 会在帧前加 4 字节长度前缀(真机 e1000 无此行为), 自适应剥离
                if len(frame) >= 18 and struct.unpack('!H', frame[12:14])[0] not in (0x0800, 0x0806, 0x86DD):
                    f2 = frame[4:]
                    if len(f2) >= 14 and struct.unpack('!H', f2[12:14])[0] in (0x0800, 0x0806, 0x86DD):
                        frame = f2
                if len(frame) < 14:
                    break
                eth_type = struct.unpack('!H', frame[12:14])[0]
                ipd = frame[14:]
                if eth_type == 0x0806:
                    op = struct.unpack('!H', frame[14:16])[0] if len(frame) >= 16 else -1
                    print(f"[net_sim] ARP op={op} len={len(frame)}", flush=True)
                elif len(ipd) >= 20:
                    proto = ipd[9]
                    dip = struct.unpack('!I', ipd[16:20])[0]
                    if proto == 17:
                        udp = ipd[(ipd[0]&0xF)*4:]
                        dp = struct.unpack('!H', udp[2:4])[0] if len(udp) >= 4 else 0
                        print(f"[net_sim] UDP dport={dp} dip=0x{dip:08X} len={len(frame)}", flush=True)
                    elif proto == 6:
                        tcp = ipd[(ipd[0]&0xF)*4:]
                        dp = struct.unpack('!H', tcp[2:4])[0] if len(tcp) >= 4 else 0
                        fl = tcp[13] if len(tcp) >= 14 else 0
                        print(f"[net_sim] TCP dport={dp} flags=0x{fl:02X} len={len(frame)}", flush=True)
                reply = handle_frame(frame)
                if reply:
                    # QEMU socket backend RX 也期望 4 字节大端长度前缀 (net_fill_rstate)
                    conn.sendall(struct.pack('!I', len(reply)) + reply)
                    if eth_type == 0x0806: counts['arp'] += 1
                    elif len(ipd) >= 20 and ipd[9] == 17:
                        udp = ipd[(ipd[0]&0xF)*4:]
                        dport = struct.unpack('!H', udp[2:4])[0] if len(udp) >= 4 else 0
                        if dport == 67: counts['dhcp'] += 1
                        elif dport == 53: counts['dns'] += 1
                    elif len(ipd) >= 20 and ipd[9] == 6:
                        counts['tcp'] += 1
        except socket.timeout:
            continue
        except Exception as e:
            print(f"[net_sim] error: {e}", flush=True)
            break

if __name__ == '__main__':
    main()
