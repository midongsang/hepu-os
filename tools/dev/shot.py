#!/usr/bin/env python3
"""HMP screendump 截图并转 PNG。

把之前手工做的"连 monitor socket → 吞 banner → screendump → PIL 转 PNG"固化下来,
省掉每次验证都要重写一遍。

用法:
    tools/dev/shot.py                     # -> /tmp/shot.ppm 与 /tmp/shot.png
    tools/dev/shot.py -o out.png -c 0,0,400,300   # 指定输出与裁剪区域
"""
import argparse
import os
import socket
import subprocess
import time

MON = os.environ.get("MON", "/tmp/hepu_mon.sock")


def hmp(command: str, wait: float = 1.5) -> bytes:
    s = socket.socket(socket.AF_UNIX)
    s.connect(MON)
    s.settimeout(4)
    try:
        s.recv(4096)  # 吞掉 QEMU monitor banner
    except socket.timeout:
        pass
    s.sendall((command + "\n").encode())
    time.sleep(wait)
    try:
        return s.recv(8192)
    finally:
        s.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="/tmp/shot.png", help="输出 PNG 路径")
    ap.add_argument("-c", "--crop", default="", help="裁剪区域 x,y,w,h")
    args = ap.parse_args()

    ppm = args.out.rsplit(".", 1)[0] + ".ppm"
    hmp(f"screendump {ppm}")
    if not os.path.exists(ppm):
        print(f"[fail] 未生成 {ppm}（QEMU 是否在运行？MON={MON}）")
        return 1

    crop_code = ""
    if args.crop:
        # -c 取的是 x,y,w,h；PIL crop 要的是 (left, upper, right, lower)
        x, y, w, h = (int(v) for v in args.crop.split(","))
        crop_code = f"im = im.crop(({x}, {y}, {x + w}, {y + h}))\n"
    code = (
        "from PIL import Image\n"
        f"im = Image.open({ppm!r}).convert('RGB')\n"
        + crop_code
        + f"im.save({args.out!r})\nprint('ok')\n"
    )
    r = subprocess.run(["python3", "-c", code], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        return 1
    print(f"[ok] {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
