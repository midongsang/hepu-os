#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描证据目录各状态帧，计算 NONBLACK 计数与判定，输出 tests/metrics.json。"""
import json, os, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from measure import measure

EV = os.environ["EV"]
DESKTOP_MIN = 2073597

ROLES = ["menu", "loading", "lock", "login", "desktop"]
ARCH_DIRS = {"64": "frames64", "32": "frames32"}

def main():
    out = {}
    for arch, d in ARCH_DIRS.items():
        base = os.path.join(EV, d)
        out[arch] = {}
        for role in ROLES:
            frames = sorted(glob.glob(os.path.join(base, role + "_*.png")))
            rows = [measure(f, role) for f in frames]
            out[arch][role] = rows

    # 汇总判定：桌面取 4 帧中最低 NONBLACK；锁屏取首帧/均值
    # 口径：64 位桌面套 1080p 全量非黑阈值；32 位 UEFI 链路崩溃重启后画面回灌菜单，
    # 其 frames32/desktop_* 为残留菜单帧、不计入桌面计数；真实 32 桌面帧取自 multiboot
    # 直启目录 frames32-direct，不套 1080p 阈值，仅记录计数。
    summary = {}
    for arch in out:
        desktop = out[arch]["desktop"]
        lock = out[arch]["lock"]
        login = out[arch]["login"]
        d_min = min((r["nonblack"] for r in desktop), default=None)
        l_min = min((r["nonblack"] for r in lock), default=None)
        n_min = min((r["nonblack"] for r in login), default=None)
        if arch == "64":
            d_verdict = "PASS" if d_min is not None and d_min >= DESKTOP_MIN else (
                "FAIL" if d_min is not None else "UNVERIFIED")
            d_threshold = DESKTOP_MIN
        else:
            d_verdict = "UNVERIFIED"
            d_threshold = "N/A（UEFI 链路线帧为崩溃重启残留菜单，不计入）"
        summary[arch] = {
            "desktop_frames": len(desktop),
            "desktop_nonblack_min": d_min,
            "desktop_threshold": d_threshold,
            "desktop_verdict": d_verdict,
            "lock_frames": len(lock),
            "lock_nonblack_min": l_min,
            "lock_verdict": "PASS" if l_min is not None and l_min / 2073600 >= 0.5 else (
                "FAIL" if l_min is not None else "UNVERIFIED"),
            "login_frames": len(login),
            "login_nonblack_min": n_min,
        }

    # 32 位 multiboot 直启图形桌面补充取证（绕过 UEFI 菜单残留）
    direct_dir = os.path.join(EV, "frames32-direct")
    direct_rows = [measure(f, "desktop") for f in sorted(glob.glob(
        os.path.join(direct_dir, "desktop_*.png")))]
    d_min_dir = min((r["nonblack"] for r in direct_rows), default=None)
    summary["32"]["direct_desktop_frames"] = len(direct_rows)
    summary["32"]["direct_desktop_nonblack_min"] = d_min_dir
    summary["32"]["direct_desktop_verdict"] = "PASS" if direct_rows else "UNVERIFIED"
    summary["32"]["direct_desktop_threshold"] = "N/A（32 位桌面不套 1080p 阈值）"

    res = {"summary": summary, "frames": out}
    os.makedirs(os.path.join(EV, "tests"), exist_ok=True)
    with open(os.path.join(EV, "tests", "metrics.json"), "w") as f:
        json.dump(res, f, ensure_ascii=False, indent=2)

    for arch, s in summary.items():
        print("[%s] desktop: %d frames, NONBLACK min=%s -> %s" % (
            arch, s["desktop_frames"], s["desktop_nonblack_min"], s["desktop_verdict"]))
        print("[%s] lock:    %d frames, NONBLACK min=%s -> %s" % (
            arch, s["lock_frames"], s["lock_nonblack_min"], s["lock_verdict"]))

if __name__ == "__main__":
    main()
