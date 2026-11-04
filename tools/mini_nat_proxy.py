#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Hepu OS 迷你 NAT 代理 (V2: 会话函数化 + 断开重连)
把 QEMU socket 后端网卡的以太帧桥接到宿主机真实网络。
处理: ARP 应答 / UDP 53 (DNS 转发) / TCP (每连接 NAT 双向转发)。
QEMU 侧: -netdev socket,id=n0,connect=127.0.0.1:4000 -device rtl8139,netdev=n0
"""
import socket
import os, select, struct, time

LISTEN_PORT = 4000
GUEST_IP = "10.0.2.15"
GATEWAY_IP = "10.0.2.2"
GUEST_MAC = bytes.fromhex("525400123456")
DNS_SERVER = ("8.8.8.8", 53)
LOG = True

def log(msg):
    if LOG:
        print("[NAT] %s" % msg, flush=True)
        try:
            with open("/tmp/nat.log", "a") as f:
                f.write("[NAT] %s\n" % msg)
        except Exception:
            pass

def parse_eth(frame):
    if len(frame) < 14:
        return None
    dst, src = frame[0:6], frame[6:12]
    etype = struct.unpack(">H", frame[12:14])[0]
    return dst, src, etype, frame[14:]

def wrap_eth(etype, payload, dst_mac):
    return dst_mac + GUEST_MAC + struct.pack(">H", etype) + payload

def qemu_send(qemu, frame):
    # V0.13.9: QEMU socket 后端帧带 4 字节大端长度前缀
    try:
        qemu.sendall(struct.pack(">I", len(frame)) + frame)
    except Exception:
        pass

def parse_ip(pkt):
    if len(pkt) < 20:
        return None
    ihl = (pkt[0] & 0x0F) * 4
    proto = pkt[9]
    src = socket.inet_ntoa(pkt[12:16])
    dst = socket.inet_ntoa(pkt[16:20])
    return ihl, proto, src, dst, pkt[ihl:]

def wrap_ip(proto, src, dst, payload, ident=0x1234, ttl=64):
    tot_len = 20 + len(payload)
    hdr = struct.pack(">BBHHHBBH4s4s",
                      0x45, 0, tot_len, ident, 0x4000, ttl, proto, 0,
                      socket.inet_aton(src), socket.inet_aton(dst))
    s = 0
    for i in range(0, 20, 2):
        s += (hdr[i] << 8) | hdr[i+1]
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    cs = (~s) & 0xFFFF
    hdr = hdr[:10] + struct.pack(">H", cs) + hdr[12:]
    return hdr + payload

def wrap_tcp(src_port, dst_port, seq, ack, flags, payload=b""):
    hdr = struct.pack(">HHIIBBHHH", src_port, dst_port, seq & 0xFFFFFFFF, ack & 0xFFFFFFFF,
                      5 << 4, flags, 65535, 0, 0)
    s = 0
    for i in range(0, 20, 2):
        s += (hdr[i] << 8) | hdr[i+1]
    s += len(payload)
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    cs = (~s) & 0xFFFF
    hdr = hdr[:16] + struct.pack(">H", cs) + hdr[18:]
    return hdr + payload

def wrap_udp(src_port, dst_port, payload):
    ln = 8 + len(payload)
    hdr = struct.pack(">HHHH", src_port, dst_port, ln, 0)
    return hdr + payload

def parse_tcp(seg):
    if len(seg) < 20:
        return None
    src_port, dst_port = struct.unpack(">HH", seg[0:4])
    seq, ack = struct.unpack(">II", seg[4:12])
    doff = (seg[12] >> 4) * 4
    flags = seg[13]
    return src_port, dst_port, seq, ack, flags, seg[doff:]

FLAG_FIN = 0x01
FLAG_SYN = 0x02
FLAG_RST = 0x04
FLAG_ACK = 0x10

class TcpConn:
    def __init__(self, guest_ip, guest_port, host_sock, my_isn, host_ip=None):
        self.guest_ip = guest_ip
        self.guest_port = guest_port
        self.host_ip = host_ip
        self.host_sock = host_sock
        self.client_isn = None
        self.next_client_seq = None
        self.my_seq = my_isn
        self.closed = False

def handle_session(qemu):
    try:
        _handle_session(qemu)
    except Exception:
        import traceback
        traceback.print_exc()
        sys.stderr.flush()
        os._exit(1)

def _handle_session(qemu):
    qemu.setblocking(False)
    conns = {}
    isn_counter = [1000]
    rxbuf = b""   # V0.13.9: QEMU socket 流 (4 字节长度前缀 + 帧)

    def process_frame(frame):
        pkt = parse_eth(frame)
        if not pkt:
            return None
        dst_mac, src_mac, etype, payload = pkt
        if etype == 0x0806:   # ARP
            if len(payload) >= 28:
                htype, ptype, hlen, plen, oper = struct.unpack(">HHBBH", payload[0:8])
                spa = socket.inet_ntoa(payload[14:18])
                tpa = socket.inet_ntoa(payload[24:28])
                if oper == 1 and tpa in (GUEST_IP, "10.0.2.2"):
                    # 应答者=网关(10.0.2.2), 让内核学到网关 MAC
                    reply = struct.pack(">HHBBH", 1, 0x0800, 6, 4, 2)
                    reply += GUEST_MAC + socket.inet_aton(GATEWAY_IP)
                    reply += src_mac + socket.inet_aton(spa)
                    qemu_send(qemu, wrap_eth(0x0806, reply, src_mac))
                    log("ARP reply for %s" % GUEST_IP)
                    return None
        return (dst_mac, src_mac, etype, payload)

    while True:
        rlist, _, _ = select.select([qemu] + [c.host_sock for c in conns.values() if not c.closed], [], [], 0.5)
        if rlist:
            log("SELECT ret %d socks" % len(rlist))
        for sock in rlist:
            if sock is qemu:
                try:
                    data = qemu.recv(65536)
                except Exception:
                    data = b""
                if not data:
                    log("QEMU disconnected")
                    return
                log("recv %d bytes: %s" % (len(data), data.hex()))
                rxbuf += data
                while len(rxbuf) >= 4:
                    flen = struct.unpack(">I", rxbuf[0:4])[0]
                    if flen > 1600 or flen < 14:
                        rxbuf = b""   # 协议错乱, 丢弃
                        break
                    if len(rxbuf) < 4 + flen:
                        break
                    frame = rxbuf[4:4+flen]
                    rxbuf = rxbuf[4+flen:]
                    pr = process_frame(frame)
                    if pr is None:
                        continue
                    dst_mac, src_mac, etype, payload = pr
                    if etype == 0x0800:  # IPv4
                        ip = parse_ip(payload)
                        if not ip:
                            continue
                        ihl, proto, src, dst, seg = ip
                        if proto == 17:    # UDP
                            if len(seg) >= 8:
                                sport, dport = struct.unpack(">HH", seg[0:4])
                                ulen = struct.unpack(">H", seg[4:6])[0]
                                if ulen > len(seg):
                                    continue
                                udata = seg[8:ulen]
                                if dport == 53:
                                    try:
                                        dns = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                                        dns.settimeout(3.0)
                                        dns.sendto(udata, DNS_SERVER)
                                        resp, _ = dns.recvfrom(2048)
                                        dns.close()
                                        frame_out = wrap_eth(0x0800,
                                            wrap_ip(17, dst, src,
                                                wrap_udp(53, sport, resp)), src_mac)
                                        qemu_send(qemu, frame_out)
                                        log("DNS query forwarded (%d bytes), reply %d sent" % (len(resp), len(frame_out)))
                                    except Exception as e:
                                        log("DNS fail: %s" % e)
                        elif proto == 6:    # TCP
                            tcp = parse_tcp(seg)
                            if not tcp:
                                continue
                            sport, dport, seq, ack, flags, data = tcp
                            key = (src, sport)
                            c = conns.get(key)
                            if flags & FLAG_SYN and not (flags & FLAG_ACK):
                                try:
                                    hs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                                    hs.settimeout(5.0)
                                    hs.connect((dst, dport))
                                    hs.setblocking(False)
                                    isn_counter[0] += 1
                                    c = TcpConn(src, sport, hs, isn_counter[0], dst)
                                    c.client_isn = seq
                                    c.next_client_seq = (seq + 1) & 0xFFFFFFFF
                                    conns[key] = c
                                    log("host connected to %s:%d" % (dst, dport))
                                    qemu_send(qemu, wrap_eth(0x0800,
                                        wrap_ip(6, dst, src,
                                            wrap_tcp(dport, sport, c.my_seq, c.next_client_seq,
                                                     FLAG_SYN | FLAG_ACK)), src_mac))
                                    log("TCP %s:%d -> %s:%d (SYN-ACK)" % (src, sport, dst, dport))
                                except Exception as e:
                                    log("TCP connect fail: %s" % e)
                                    qemu_send(qemu, wrap_eth(0x0800,
                                        wrap_ip(6, dst, src,
                                            wrap_tcp(dport, sport, 0, seq + 1, FLAG_RST | FLAG_ACK)), src_mac))
                            elif c and not c.closed:
                                if flags & FLAG_FIN:
                                    c.host_sock.close()
                                    c.closed = True
                                    del conns[key]
                                    log("client FIN close")
                                    continue
                                if data:
                                    c.next_client_seq = (seq + len(data)) & 0xFFFFFFFF
                                    log("guest data -> host %d bytes" % len(data))
                                    try:
                                        c.host_sock.sendall(data)
                                    except Exception:
                                        c.closed = True
                                        del conns[key]
                                        continue
            else:
                key2 = None
                for k2, c2 in conns.items():
                    if c2.host_sock is sock:
                        key2 = k2
                        c2 = c2
                        break
                if key2 is None:
                    continue
                c = conns[key2]
                try:
                    data = sock.recv(65536)
                except Exception:
                    data = b""
                log("host recv %d bytes (sock=%r)" % (len(data), sock))
                if not data:
                    c.host_sock.close()
                    if c in conns.values():
                        del conns[key2]
                    qemu_send(qemu, wrap_eth(0x0800,
                        wrap_ip(6, c.host_ip, GUEST_IP,
                            wrap_tcp(80, c.guest_port, c.my_seq, c.next_client_seq,
                                     FLAG_FIN | FLAG_ACK)), GUEST_MAC))
                    log("host close, FIN sent")
                else:
                    qemu_send(qemu, wrap_eth(0x0800,
                        wrap_ip(6, c.host_ip, GUEST_IP,
                            wrap_tcp(80, c.guest_port, c.my_seq, c.next_client_seq,
                                     FLAG_ACK, data)), GUEST_MAC))
                    c.my_seq = (c.my_seq + len(data)) & 0xFFFFFFFF
                    log("host data -> guest %d bytes" % len(data))

def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", LISTEN_PORT))
    srv.listen(5)
    log("listening on %d, waiting for QEMU..." % LISTEN_PORT)
    while True:
        qemu, addr = srv.accept()
        log("QEMU connected from %s" % str(addr))
        pid = os.fork()
        if pid == 0:
            # 子进程: 处理本连接 (多 QEMU 实例/重连互不阻塞)
            try:
                handle_session(qemu)
            finally:
                os._exit(0)
        qemu.close()

if __name__ == "__main__":
    main()
