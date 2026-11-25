#!/usr/bin/env python3
# ============================================================================
# 串口中继：把 QEMU -serial unix socket 接到日志文件，并把 FIFO 里的命令行
# 转发进客户机 COM1。
#
# 为什么需要它：QEMU unix serial 后端只允许一个客户端；驱动脚本通过本中继
# 独占该连接：一路把 guest TX 落盘（证据日志），一路从命令 FIFO 读行写入
# guest RX（等价于在串口 shell 里敲命令）。
#
# 用法: serial_relay.py <serial_sock> <serial_log> <cmd_fifo>
#   - serial_sock: QEMU -serial unix:... 路径
#   - serial_log:  串口输出落盘文件（追加二进制）
#   - cmd_fifo:    命令 FIFO；每行一条命令，末尾自动补 \r
# ============================================================================
import os
import socket
import sys
import threading
import time

def main():
    sock_path, log_path, fifo_path = sys.argv[1], sys.argv[2], sys.argv[3]

    # FIFO 预先由调用方 mkfifo；这里以读写方式打开，避免读到 EOF
    fifo_fd = os.open(fifo_path, os.O_RDWR | os.O_NONBLOCK)

    # 等待 QEMU 监听 socket 就绪
    s = None
    for _ in range(120):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(sock_path)
            break
        except OSError:
            time.sleep(0.5)
    if s is None:
        print("[relay] 无法连接串口 socket: %s" % sock_path, file=sys.stderr)
        sys.exit(1)
    s.setblocking(True)
    print("[relay] connected to %s" % sock_path, flush=True)

    log_fd = open(log_path, "ab", buffering=0)

    def reader():
        while True:
            try:
                data = s.recv(4096)
            except OSError:
                break
            if not data:
                break
            log_fd.write(data)
        log_fd.flush()
        os._exit(2)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    # 命令转发循环：FIFO 每读到一行，去掉换行后补 \r 送入串口
    buf = b""
    while True:
        try:
            chunk = os.read(fifo_fd, 128)
        except OSError:
            chunk = b""
        if not chunk:
            time.sleep(0.1)
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.rstrip(b"\r")
            if line:
                s.sendall(line + b"\r")
            else:
                # 空行也回车，充当触发/喂提示符
                s.sendall(b"\r")

if __name__ == "__main__":
    main()
