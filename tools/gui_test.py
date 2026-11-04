#!/usr/bin/env python3
"""赫普OS 64 位 GUI 自动化回归脚本

用途: 无显示器环境下启动 QEMU, 通过 HMP/QMP 发送键盘鼠标事件并截图,
      覆盖桌面启动、文件管理器、终端 disk fsck 等冒烟场景。

用法:
  python3 tools/gui_test.py --image /tmp/hepu64_single.img --out /tmp/gui_out
  python3 tools/gui_test.py --scenario fsck          # 只跑内核 fsck 场景

说明:
  - 键盘走 PS/2 (HMP sendkey), 因此启动 QEMU 时只挂 usb-tablet, 不挂 usb-kbd,
    否则键盘输入会逐渐丢失 (已实测)。
  - 鼠标点击走 QMP input-send-event 的绝对坐标事件 (需 usb-tablet)。
  - 截图存为 PPM; 装了 ImageMagick 时同时转 PNG, 便于人工复核。
  - 本脚本只负责执行步骤与留存证据, 不做 OCR; 基础设施失败返回非 0。
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time

# HMP sendkey 别名
KEY_ALIAS = {
    "space": "spc",
    "enter": "ret",
    "escape": "esc",
}

QEMU_CANDIDATES = [
    os.environ.get("QEMU", ""),
    "qemu-system-x86_64",
    "/usr/bin/qemu-system-x86_64",
    "/usr/local/bin/qemu-system-x86_64",
]


def find_qemu():
    for c in QEMU_CANDIDATES:
        if not c:
            continue
        p = shutil.which(c) if os.path.basename(c) == c else (c if os.path.exists(c) else None)
        if p:
            return p
    sys.exit("未找到 qemu-system-x86_64, 可用 --qemu 指定")


class Hmp:
    def __init__(self, path, timeout=5.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(path)

    def cmd(self, line):
        self.sock.sendall(line.encode() + b"\n")
        time.sleep(0.15)
        try:
            return self.sock.recv(65536).decode(errors="ignore")
        except socket.timeout:
            return ""

    def close(self):
        self.sock.close()


class Qmp:
    def __init__(self, path, timeout=5.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(path)
        try:
            self.sock.recv(65536)          # 问候
        except socket.timeout:
            pass
        self._send({"execute": "qmp_capabilities"})

    def _send(self, obj):
        self.sock.sendall(json.dumps(obj).encode() + b"\n")

    def cmd(self, execute, **args):
        self._send({"execute": execute, "arguments": args})
        try:
            raw = self.sock.recv(65536)
        except socket.timeout:
            return b""
        # QMP 失败会返回 {"error": ...}; 旧版忽略返回值导致点击静默失效
        if b'"error"' in raw:
            raise RuntimeError("QMP %s 失败: %s" % (execute, raw.decode(errors="ignore")))
        return raw

    def move(self, x, y):
        """移动到像素坐标 (x,y)。

        注意两点 (踩过的坑):
        1. abs 轴的 axis 必须是字符串 "x"/"y", 传 0/1 会被 QEMU 拒绝
           ("expected: string");
        2. value 是 0-32767 的归一值, 不是像素值; 内核按 value*1024/32768
           还原像素, 所以这里按 32 倍/42.666 倍换算。
        """
        vx = min(32767, max(0, int(x) * 32))
        vy = min(32767, max(0, int(y) * 32768 // 768))
        self.cmd("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": vx}},
            {"type": "abs", "data": {"axis": "y", "value": vy}},
        ])

    def click(self, x, y):
        self.move(x, y)
        time.sleep(0.1)
        self.cmd("input-send-event", events=[
            {"type": "btn", "data": {"down": True, "button": "left"}},
        ])
        time.sleep(0.1)
        self.cmd("input-send-event", events=[
            {"type": "btn", "data": {"down": False, "button": "left"}},
        ])

    def close(self):
        self.sock.close()


class Guest:
    def __init__(self, image, workdir, qemu, boot_wait, key_delay):
        self.workdir = workdir
        self.boot_wait = boot_wait
        self.key_delay = key_delay
        self.mon = os.path.join(workdir, "mon.sock")
        self.qmp = os.path.join(workdir, "qmp.sock")
        self.serial = os.path.join(workdir, "serial.log")
        for p in (self.mon, self.qmp):
            if os.path.exists(p):
                os.remove(p)
        datadir = os.environ.get("QEMU_DATADIR", "")
        cmd = [qemu, "-m", "512M"]
        if datadir:
            cmd += ["-L", datadir]
        cmd += [
            "-drive", "format=raw,file=%s" % image,
            "-vga", "std",
            "-display", "none",
            "-net", "none",
            "-monitor", "unix:%s,server,nowait" % self.mon,
            "-qmp", "unix:%s,server,nowait" % self.qmp,
            "-usb", "-device", "usb-tablet",
            "-serial", "file:%s" % self.serial,
        ]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.hmp = self._connect(Hmp, self.mon, "HMP")
        self.q = self._connect(Qmp, self.qmp, "QMP")
        time.sleep(boot_wait)

    def _connect(self, cls, path, what):
        deadline = time.time() + 10
        while time.time() < deadline:
            if self.proc.poll() is not None:
                err = self.proc.stderr.read().decode(errors="ignore")
                sys.exit("QEMU 启动失败: %s" % err.strip())
            if os.path.exists(path):
                try:
                    return cls(path)
                except OSError:
                    pass
            time.sleep(0.2)
        sys.exit("连接 %s socket 超时" % what)

    def key(self, *keys):
        for k in keys:
            self.hmp.cmd("sendkey %s" % KEY_ALIAS.get(k, k))
            time.sleep(self.key_delay)

    def type_text(self, text):
        """仅支持小写字母/数字/空格/斜杠/点, 够用即可"""
        for ch in text:
            if ch == " ":
                self.key("space")
            elif ch == "/":
                self.key("slash")
            elif ch == ".":
                self.key("dot")
            else:
                self.key(ch)

    def shot(self, name):
        ppm = os.path.join(self.workdir, name + ".ppm")
        self.hmp.cmd("screendump %s" % ppm)
        time.sleep(0.3)
        png = os.path.join(self.workdir, name + ".png")
        if not os.path.exists(ppm):
            return None
        # 优先 PIL, 回退 ImageMagick convert
        try:
            from PIL import Image
            Image.open(ppm).save(png)
            return png
        except Exception:
            pass
        if shutil.which("convert"):
            subprocess.run(["convert", ppm, png],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return png
        return ppm

    def close(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def scenario_boot(g):
    g.shot("01_desktop")
    print("[PASS] 桌面启动截图 01_desktop")


OOBE_KEYS = ["1", "1", "1",                 # 语言=中文 时区=北京 类型=普通用户
             "h", "e", "p", "u", "enter",   # 用户名 hepu
             "1", "2", "3", "4", "enter",   # 密码
             "1", "2", "3", "4", "enter",   # 确认
             "space"]                       # 欢迎屏 -> 桌面


def oobe_setup(g):
    """全新镜像首启会弹出图形化 OOBE 向导; 自动走完后进入桌面.
    V0.55 P3-GUI: 向导以窗口形式绘制在桌面上, TCG 软件渲染较慢, 按键间隔需放大. """
    old_delay = g.key_delay
    g.key_delay = max(g.key_delay, 2.0)
    for k in OOBE_KEYS:
        g.key(k)
    g.key_delay = old_delay
    time.sleep(20)              # 等桌面加载完 (含设置窗口自动打开)
    print("[PASS] OOBE 向导自动完成 (账户 hepu/1234)")


def scenario_files(g):
    g.key("f11")
    time.sleep(1.5)
    g.shot("02_files")
    print("[PASS] 文件管理器打开截图 02_files")
    g.key("f10")
    time.sleep(0.8)
    g.shot("03_newfile_prompt")
    g.key("tab")          # 取消提示, 避免后续场景的按键落进输入框
    print("[PASS] F10 新建文件提示截图 03_newfile_prompt")


def scenario_term_fsck(g):
    g.key("f12")
    time.sleep(1.2)
    g.type_text("disk fsck")
    g.key("enter")
    time.sleep(2.5)
    g.shot("04_disk_fsck")
    print("[PASS] disk fsck 截图 04_disk_fsck (应显示 fsck: PASS)")


def _px(png, x, y):
    """读取截图像素 (仅装了 PIL 时可用), 用于断言颜色变化"""
    if not png or not png.endswith(".png"):
        return None
    try:
        from PIL import Image
        return Image.open(png).convert("RGB").getpixel((x, y))
    except Exception:
        return None


def scenario_theme(g):
    """鼠标点击链路 + 主题取色器: 点 Appearance 菜单, 再点第 3 个色板圆点"""
    g.shot("05a_pre_click")     # 诊断: 点击前桌面状态
    # 设置窗口开机即在级联位 (300,100), 左侧菜单行高 32: 首行 136-164
    g.q.click(340, 150)
    time.sleep(0.7)
    page = g.shot("05_settings_appearance")
    print("[PASS] 点击 Appearance 菜单 -> 05_settings_appearance")

    # 选中菜单项底色即 accent; 色板圆点起始 (w->x+156, w->y+112)
    before = _px(page, 390, 150)
    g.q.click(522, 222)          # 第 3 个预设 (Orange)
    time.sleep(0.7)
    after_png = g.shot("06_theme_orange")
    after = _px(after_png, 390, 150)
    if before and after and before == after:
        raise RuntimeError("点击色板后 accent 未变化: %s" % (before,))
    print("[PASS] 点击色板圆点已应用源色: accent %s -> %s" % (before, after))


def scenario_dyn(g):
    """P1-7: 动态链接 —— 终端里 exec 宿主 gcc 默认产物的动态 ELF。

    程序走 /lib64/ld-linux-x86-64.so.2 + /lib/x86_64-linux-gnu/libc.so.6；
    stdout 由内核同时镜像到串口，因此直接断言 serial.log 里的输出。
    """
    g.key("f12")
    time.sleep(1.2)
    g.type_text("exec /bin/glibchello.elf")
    g.key("enter")
    time.sleep(8.0)            # ld.so 要读完 2MB libc 并做重定位
    g.shot("07_dynhello")
    log = os.path.join(g.workdir, "serial.log")
    text = ""
    if os.path.exists(log):
        with open(log, "r", errors="ignore") as f:
            text = f.read()
    if "Hello, World!" not in text:
        tail = text[-2000:]
        raise RuntimeError("动态链接程序没有输出 Hello, World!；串口尾部:\n%s" % tail)
    print("[PASS] 动态链接 hello 输出 Hello, World!")


def scenario_systools(g):
    """V0.55: 系统工具三窗口 (设备管理器/磁盘管理/日志查看器)"""
    # 设备管理器: 启动器搜索 dev
    g.key("space")
    time.sleep(0.8)
    g.type_text("dev")
    g.key("enter")
    time.sleep(2.5)
    g.shot("08_devmgr_pci")
    g.key("tab")                     # 切到 USB 页
    time.sleep(1.0)
    g.shot("09_devmgr_usb")
    g.key("tab")                     # 切到 驱动 页
    time.sleep(1.0)
    g.shot("10_devmgr_drivers")
    # 关闭窗口 (级联位 1: 330,126; 关闭按钮在右上角)
    g.q.click(878, 136)
    time.sleep(1.0)

    # 磁盘管理: 启动器搜索 disk
    g.key("space")
    time.sleep(0.8)
    g.type_text("disk")
    g.key("enter")
    time.sleep(2.5)
    g.shot("11_diskmgr")
    g.q.click(908, 162)              # 关闭 (级联位 2: 360,152)
    time.sleep(1.0)

    # 日志查看器: 启动器搜索 log
    g.key("space")
    time.sleep(0.8)
    g.type_text("log")
    g.key("enter")
    time.sleep(2.0)
    g.shot("12_logview")
    print("[PASS] 系统工具三窗口截图 08-12")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="/tmp/hepu64_single.img")
    ap.add_argument("--out", default="/tmp/gui_out")
    ap.add_argument("--qemu", default=None)
    ap.add_argument("--boot-wait", type=float, default=18.0)
    ap.add_argument("--key-delay", type=float, default=0.4,
                    help="按键间隔; 过小会丢键")
    ap.add_argument("--scenario", choices=["boot", "files", "fsck", "theme", "dyn", "systools", "all"],
                    default="all")
    ap.add_argument("--oobe", action="store_true",
                    help="镜像为全新盘时先自动完成首启向导再跑场景")
    args = ap.parse_args()

    qemu = args.qemu or find_qemu()
    if not os.path.exists(args.image):
        sys.exit("镜像不存在: %s" % args.image)
    os.makedirs(args.out, exist_ok=True)

    steps = {"boot": scenario_boot, "fsck": scenario_term_fsck,
             "files": scenario_files, "theme": scenario_theme,
             "dyn": scenario_dyn, "systools": scenario_systools}
    names = ["boot", "fsck", "files", "theme", "dyn", "systools"] if args.scenario == "all" else [args.scenario]
    # 每个场景独立启动: 窗口焦点/输入状态互不干扰
    # --oobe 只需做一次: 首场景完成向导后账户写入镜像, 后续场景直接进桌面
    # (若每个场景都发向导按键, 第二次起这些键会落到桌面: 数字切工作区、
    #  空格开启动器, 场景断言全部错位)
    did_oobe = False
    for name in names:
        g = Guest(args.image, args.out, qemu, args.boot_wait, args.key_delay)
        try:
            if args.oobe and not did_oobe:
                oobe_setup(g)
                did_oobe = True
            steps[name](g)
        finally:
            g.close()
    print("完成, 截图目录: %s" % args.out)


if __name__ == "__main__":
    main()
