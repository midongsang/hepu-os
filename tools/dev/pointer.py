#!/usr/bin/env python3
"""把鼠标指针精确挪到屏幕坐标 (x,y)，并读回光标位置。

为什么需要它：
  - HMP mouse_move / QMP abs 在本机 QEMU 6.2 + usb-mouse 上不可用（abs 事件直接报
    "Input handler not found"，需要 tablet 设备）
  - QMP rel 可用，但系统对增量做了缩放（实测 1px 请求 ≈ 3px 位移），单次大步会丢事件
所以做法是"小步移动 + 截图读回光标位置 + 按实测比例校正"，闭环收敛。

光标定位靠纯白像素 (255,255,255) 的包围盒 —— 光标尖就是包围盒左上角。

用法:
    tools/dev/pointer.py 165 75         # 移到 (165,75)
    tools/dev/pointer.py --where         # 只打印当前光标位置
"""
import argparse
import json
import os
import socket
import sys
import time

MON = os.environ.get("MON", "/tmp/hepu_mon.sock")
QMP = os.environ.get("QMP", "/tmp/hepu_qmp.sock")


def hmp(command: str, wait: float = 1.0) -> None:
    s = socket.socket(socket.AF_UNIX)
    s.connect(MON)
    s.settimeout(4)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    s.close()


def screenshot(path: str = "/tmp/_cursor.ppm") -> str:
    hmp(f"screendump {path}")
    return path


def locate(ppm: str = "/tmp/_cursor.ppm", retry: int = 1):
    """返回光标尖端坐标，找不到返回 None（截图可能早于画面刷新，会重试）。"""
    from PIL import Image

    im = Image.open(ppm).convert("RGB")
    W, H = im.size
    px = im.load()
    xs, ys = [], []
    for y in range(H):
        for x in range(W):
            if px[x, y] == (255, 255, 255):
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
        # 光标是 16x16 左右的箭头，纯白像素数量远小于全屏噪点
    if len(xs) > 400:
        if retry > 0:
            time.sleep(0.5)
            return locate(screenshot(), retry - 1)
        return None
    return min(xs), min(ys)


def rel(dx: int, dy: int) -> None:
    s = socket.socket(socket.AF_UNIX)
    s.connect(QMP)
    s.settimeout(3)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall((json.dumps({"execute": "qmp_capabilities"}) + "\n").encode())
    time.sleep(0.2)
    try:
        s.recv(65536)
    except socket.timeout:
        pass
    ev = {"execute": "input-send-event", "arguments": {"events": [
        {"type": "rel", "data": {"axis": "x", "value": int(dx)}},
        {"type": "rel", "data": {"axis": "y", "value": int(dy)}},
    ]}}
    s.sendall((json.dumps(ev) + "\n").encode())
    time.sleep(0.25)
    s.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("x", nargs="?", type=int)
    ap.add_argument("y", nargs="?", type=int)
    ap.add_argument("--where", action="store_true", help="只打印当前光标位置")
    ap.add_argument("--tol", type=int, default=4)
    args = ap.parse_args()

    cur = locate(screenshot())
    if cur is None:
        print("[fail] 未在截图里找到光标")
        return 1
    if args.where or args.x is None:
        print(f"cursor at {cur[0]},{cur[1]}")
        return 0

    # 纯白像素包围盒的左上角并不等于指针热点: default.xcur 的 hotspot 是 (1,0),
    # 箭头尖端那段是深色描边, 白色填充要到图内约 (7,12) 才出现(24px 光标实测)。
    # 因此把检测值换算回热点再算偏差。
    off_x, off_y = 7, 12
    cur = (cur[0] - off_x, cur[1] - off_y)
    print(f"hotspot {cur[0]},{cur[1]} -> target {args.x},{args.y}")
    scale = 3.0  # 系统对增量做了放大，先按实测值起步
    for _ in range(60):
        dx, dy = args.x - cur[0], args.y - cur[1]
        if abs(dx) <= args.tol and abs(dy) <= args.tol:
            print(f"[ok] cursor at {cur[0]},{cur[1]}")
            return 0
        # 单步限幅，避免丢事件；取剩余距离的一半，最多 25
        sx = (1 if dx > 0 else -1) * min(25, max(4, abs(dx) // 2))
        sy = (1 if dy > 0 else -1) * min(25, max(4, abs(dy) // 2))
        if sx == 0 and dx != 0:
            sx = 1 if dx > 0 else -1
        if sy == 0 and dy != 0:
            sy = 1 if dy > 0 else -1
        before = cur
        rel(sx, sy)
        cur = locate(screenshot())
        if cur is None:
            print("[fail] 移动后光标丢失")
            return 1
        moved_x, moved_y = cur[0] - before[0], cur[1] - before[1]
        if sx and abs(moved_x) > 1:
            scale = max(1.0, abs(sx) / max(1, abs(moved_x)) * 1.0)
    print(f"[fail] 未收敛, cursor at {cur[0]},{cur[1]}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
