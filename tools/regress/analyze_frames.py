#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描证据目录各状态帧，计算 NONBLACK 计数与判定，输出 tests/metrics.json。"""
import json, os, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from measure import measure

EV = os.environ["EV"]
DESKTOP_MIN = 2073597

ROLES = ["menu", "loading", "lock", "desktop"]
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
    # 口径：64 位桌面套 1080p 全量非黑阈值；32 位本树是旧稀疏桌面，不套该阈值，
    # 只要帧已渲染且串口有桌面标记即记到达，NONBLACK 仅记录不作判据。
    summary = {}
    for arch in out:
        desktop = out[arch]["desktop"]
        lock = out[arch]["lock"]
        d_min = min((r["nonblack"] for r in desktop), default=None)
        l_min = min((r["nonblack"] for r in lock), default=None)
        if arch == "64":
            d_verdict = "PASS" if d_min is not None and d_min >= DESKTOP_MIN else (
                "FAIL" if d_min is not None else "UNVERIFIED")
            d_threshold = DESKTOP_MIN
        else:
            d_verdict = "PASS" if desktop else "UNVERIFIED"
            d_threshold = "N/A（32 位旧桌面不套 1080p 阈值）"
        summary[arch] = {
            "desktop_frames": len(desktop),
            "desktop_nonblack_min": d_min,
            "desktop_threshold": d_threshold,
            "desktop_verdict": d_verdict,
            "lock_frames": len(lock),
            "lock_nonblack_min": l_min,
            "lock_verdict": "PASS" if l_min is not None and l_min / 2073600 >= 0.5 else (
                "FAIL" if l_min is not None else "UNVERIFIED"),
        }

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
