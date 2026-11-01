#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""赫普OS 系统化压力回归套件 (V3.87-E12)
启动 QEMU → 自动跑兼容层 auto-test → 逐条执行内核自测命令 →
检查 VGA/serial 预期输出与崩溃特征 → 输出 PASS/FAIL 报告。

用法:  python3 tools/regression.py
退出码: 0=全 PASS  1=有 FAIL
"""
import os, re, socket, subprocess, sys, time

HEPU = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEMU = os.path.join(HEPU, "tools/qemu/x/usr/bin/qemu-system-x86_64")
QLIB = os.path.join(HEPU, "tools/qemu/x/usr/share/qemu")
SOCK = "/tmp/hmon.sock"
SERLOG = "/tmp/hserial.log"
MON_SOCK = "/tmp/hmon.sock"

# 兼容层 auto-test 期望(启动即跑, 输出在 VGA 屏幕; 25 行屏只留尾部几行)
AUTO_EXPECT = ["auto-test", "macho_demo -> rc=0"]

# 内核自测命令 → VGA 期望特征(任一出现即 PASS)
TESTS = [
    ("perf",        ["kheap", "slab"]),
    ("locktest",    ["done", "[LOCKTEST]"]),
    ("memtest",     ["charge2=-1"]),
    ("ipcstress",   ["timeout-path=-3"]),
    ("setappcpu 30",["[SETAPPCU]"]),
    ("run loop",    ["cpu-limit kill"]),
    ("fs",          ["usage:", "fs mount"]),
    ("ls",          ["loop.elf"]),
    ("version",     ["1.0.2"]),
    ("ps",          ["PID"]),
    ("hdpk",        ["H-DPK community", "templates=5"]),  # V3.87-E15
    ("leakcheck",   ["[LEAK] live allocs"]),               # V3.88-E16
    ("run crashdemo", ["killing", "rc=0"]),              # V3.88-E18 异常类型见 serial [UEXC]
    ("run crashud",  ["killing", "rc=0"]),               # V3.88-E18 异常类型见 serial [UEXC]
    ("help fs",     ["data-disk ext2 operations"]),       # V3.88-E17
    ("help",        ["command manual"]),                  # V3.88-E17 (放最后: help all 长输出)
]

CRASH = re.compile(r"panic|#GP|oops|CRIT|ASSERT|HALT|TRIPLE", re.I)

def mon(cmd, wait=0.3):
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(0.4)
        s.connect(MON_SOCK)
        s.sendall(cmd.encode())
        time.sleep(wait)
        data = b""
        while True:
            try:
                d = s.recv(65536)
            except socket.timeout:
                break
            if not d:
                break
            data += d
        s.close()
        return data.decode(errors="replace")
    except Exception:
        return ""

def vga_text():
    # 分段读 25 行(每行 160 字节), 避免 QEMU monitor x/ 单次输出截断
    import re as _re
    lines = []
    for r in range(0, 28, 4):   # 每批 4 行(x/640ub 无截断), 覆盖 25 行(含第 24 行提示符)
        raw = mon("x/640ub 0x%x\n" % (0xb8000 + r * 160))
        segs = [s for s in raw.splitlines()
                if _re.match(r"^[0-9a-f]{6,}:", s)]
        vals = []
        for seg in segs:
            for v in _re.findall(r"\b(\d{1,3})\b", seg.split(":", 1)[1]):
                vals.append(int(v))
        # vals 是 4 行×160 字节(属性交错), 每行 80 单元=160 字节
        for row in range(4):
            base = row * 160
            chunk = vals[base:base+160]
            line = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk[::2])
            line = line.rstrip(".")
            if line.strip():
                lines.append(line)
    return lines

def sendkey(keys):
    # 逐字符编码: 空格→spc, 其余字符原样, 用 '-' 连接
    seq = "-".join("spc" if c == " " else c for c in keys)
    mon("sendkey %s-ret\n" % seq, 0.12)

def wait_shell(timeout=90):
    for _ in range(timeout):
        if any("hepu@os" in l for l in vga_text()):
            return True
        time.sleep(1)
    return False

def main():
    # 使用外部 QEMU(调用方已启动, monitor 在 SOCK)。不 pkill/自启,
    # 避免回归进程退出时连带杀掉 QEMU。
    # 外部 QEMU 已就绪: 不删除 SOCK(否则 unix socket 路径失效),
    # 不删除 SERLOG(serial 由 QEMU fd 写入, 删除后读不到)。
    results = []
    try:
        if not wait_shell():
            results.append(("shell ready", False, "timeout waiting hepu@os"))
        else:
            # 0) 禁用自动睡眠(300s idle 会挂死自动化; 键盘唤醒已修但回归不宜久等)
            sendkey("pm set_timeout 0")
            time.sleep(0.8)
            # 1) auto-test (启动即跑, 查 VGA 屏幕残留)
            time.sleep(0.5)
            txt0 = "\n".join(vga_text())
            ok = all(a in txt0 for a in AUTO_EXPECT)
            results.append(("auto-test(compat)", ok,
                            "ok" if ok else "screen lacks " + repr(AUTO_EXPECT)))

            # 2) 内核自测命令
            for name, expect in TESTS:
                if name in ("run crashdemo", "run crashud"):
                    sendkey("clear")          # 清屏避免 help all 长输出挤出崩溃详情
                    time.sleep(1.5)
                sendkey(name)
                time.sleep(3.0 if name in ("run crashdemo", "run crashud") else
                          (1.2 if name == "run loop" else 0.45))
                txt = "\n".join(vga_text())
                ok = any(e in txt for e in expect)
                results.append((name, ok, expect if ok else "screen lacks " + repr(expect)))
                if not ok and name in ("run crashdemo", "run crashud"):
                    print("  DEBUG", name, "VGA:", repr(txt[:300]))

            # 3) serial 崩溃特征
            with open(SERLOG, encoding="utf-8", errors="replace") as f:
                serial = f.read()
            m = CRASH.search(serial)
            results.append(("no-crash(serial)", m is None,
                            "clean" if m is None else "found " + m.group(0)))
    finally:
        pass  # 外部 QEMU 由调用方管理, 回归结束不杀

    # 报告
    print("=" * 56)
    print("  HEPU-OS 回归报告  (v1.02 / DESIGN V3.88 / E18)")
    print("=" * 56)
    fails = 0
    for name, ok, detail in results:
        print("  [%s] %-22s %s" % ("PASS" if ok else "FAIL", name, detail))
        if not ok: fails += 1
    print("-" * 56)
    print("  结果: %d/%d 通过" % (len(results) - fails, len(results)))
    return 0 if fails == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
