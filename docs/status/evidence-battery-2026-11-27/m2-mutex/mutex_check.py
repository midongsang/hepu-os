#!/usr/bin/env python3
"""真实 DSDT 兼容夹具：Mutex + Acquire/Release + _OSI；_BST 仍须读出 57%。"""
import os
ROOT = os.path.dirname(os.path.abspath(__file__))
FAILS = []


def check(name, cond, detail=""):
    print("[%s] %s%s" % ("PASS" if cond else "FAIL", name, (" — " + detail) if detail else ""))
    if not cond:
        FAILS.append(name)


def main():
    with open(os.path.join(ROOT, "desk-lines.txt"), "rb") as f:
        ser = f.read().decode("utf-8", "replace")
    check("M1 命名空间建表无降级", "[AML] tables=0x3" in ser and "degraded=0x0" in ser)
    check("M2 找到并选中 MTX0", "[BAT] aml dev=selected MTX0" in ser)
    ok = [l for l in ser.splitlines() if l.startswith("[BAT] aml sta=") and "pct=57" in l]
    check("M3 Acquire/Release/_OSI 后仍读出 57%", len(ok) >= 3, "matched=%d" % len(ok))
    check("M4 没有 bst failed / untrusted", "bst failed" not in ser and "untrusted" not in ser)
    check("M5 AC 仍来自 _PSR=1", "[BAT] ac psr=1" in ser)
    print("\nRESULT: %s (%d checks, %d failed)" % ("PASS" if not FAILS else "FAIL",
                                                  5, len(FAILS)))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    raise SystemExit(main())
