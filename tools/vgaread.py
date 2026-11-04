#!/usr/bin/env python3
"""QEMU monitor 读 VGA 文本(0xb8000) → 可读文本。用法: python3 vgaread.py <monitor_cmd>"""
import socket, sys, time

def mon(cmd, sock):
    sock.sendall((cmd + '\n').encode())
    time.sleep(0.15)
    sock.settimeout(1.0)
    data = b''
    try:
        while True:
            d = sock.recv(65536)
            if not d: break
            data += d
    except socket.timeout:
        pass
    return data.decode(errors='replace')

def main():
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('/tmp/hmon.sock')
    mon('', sock)  # flush
    raw = mon('x/4000bx 0xb8000', sock)
    # 解析 hex 字节
    import re
    hexes = re.findall(r'0x([0-9a-fA-F]{2})', raw)
    # x 输出格式: 每行 16 字节, 行首地址。过滤地址行, 收集字节值
    vals = []
    for m in re.finditer(r'0x([0-9a-fA-F]{2})', raw):
        vals.append(int(m.group(1), 16))
    if len(vals) < 8000:
        # x/4000bx 可能输出 4000 字节, 每 16 位单元 = 低字节字符 + 高字节属性
        pass
    # 4000bx = 4000 字节 = 2000 单元? 实际需要 8000 字节(4000 单元)。
    # 若不足, 改用 x/8000bx
    if len(vals) < 8000:
        raw = mon('x/8000bx 0xb8000', sock)
        vals = []
        for m in re.finditer(r'0x([0-9a-fA-F]{2})', raw):
            vals.append(int(m.group(1), 16))
    # 字符 = 偶数索引 (低字节), 属性 = 奇数索引
    cells = []
    n = len(vals) if len(vals) < 8000 else 8000
    for i in range(0, n - 1, 2):
        ch = vals[i]
        cells.append(chr(ch) if 32 <= ch < 127 else ('#' if 0x80 <= ch <= 0xDF else '.'))
    # 每 80 字符一行
    lines = []
    for r in range(0, len(cells) - 79, 80):
        line = ''.join(cells[r:r+80]).rstrip('.')
        if line.strip():
            lines.append(line)
    sock.close()
    print('\n'.join(lines[-60:]))

if __name__ == '__main__':
    main()
