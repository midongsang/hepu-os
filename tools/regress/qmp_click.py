#!/usr/bin/env python3
"""QMP 绝对坐标点击（usb-tablet 在 UHCI 上时用）。

用法:
  qmp_click.py <qmp_sock> <x> <y> <screen_w> <screen_h> [move|click]

只做一件事: 把 (x,y) 换算成 0..32767 的绝对轴值发给 QEMU，
必要时再补一对左键按下/抬起。桌面侧由 hsinput 的 tablet 路径
（usb64_tablet_get）接收，坐标是绝对的，不需要累加。
"""
import json
import socket
import sys
import time

ABS_MAX = 0x7FFF


class Qmp:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.s.settimeout(3)
        self._read()                      # greeting
        self._cmd({"execute": "qmp_capabilities"})

    def _read(self):
        buf = b""
        while True:
            try:
                chunk = self.s.recv(4096)
            except socket.timeout:
                return None
            if not chunk:
                return None
            buf += chunk
            for line in buf.split(b"\r\n"):
                if not line.strip():
                    continue
                try:
                    return json.loads(line)
                except json.JSONDecodeError:
                    continue

    def _cmd(self, obj):
        self.s.sendall((json.dumps(obj) + "\r\n").encode())
        for _ in range(8):
            r = self._read()
            if r is not None and ("return" in r or "error" in r):
                return r
        return None

    def events(self, evs):
        return self._cmd({"execute": "input-send-event", "arguments": {"events": evs}})

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def abs_ev(axis, px, span):
    v = int(round(px * ABS_MAX / (span - 1))) if span > 1 else 0
    v = max(0, min(ABS_MAX, v))
    return {"type": "abs", "data": {"axis": axis, "value": v}}


def main():
    sock, x, y, sw, sh = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
    mode = sys.argv[6] if len(sys.argv) > 6 else "click"

    q = Qmp(sock)
    q.events([abs_ev("x", x, sw), abs_ev("y", y, sh)])
    time.sleep(0.6)
    if mode == "click":
        q.events([{"type": "btn", "data": {"down": True, "button": "left"}}])
        time.sleep(0.25)
        q.events([{"type": "btn", "data": {"down": False, "button": "left"}}])
        time.sleep(0.25)
    q.close()
    print("qmp %s (%d,%d) on %dx%d" % (mode, x, y, sw, sh))


if __name__ == "__main__":
    main()
