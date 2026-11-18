#!/usr/bin/env python3
"""HepuOS 基准日志分析：把串口日志里的 A-1/A-2/A-3 与 ARM64 [M2S] 变尺寸扫描
解析成表，并对 [M2S] 做「固定开销 + 每像素成本」两参数最小二乘拟合。

用法:
  python3 tools/perf_fit.py <log1> [log2 ...]          # 全部汇总
  python3 tools/perf_fit.py <log> --md                # 输出 Markdown 表
  python3 tools/perf_fit.py <log> --m2s               # 只做 M2S 拟合

口径说明: 所有时间单位都是 TSC 周期(QEMU 下不代表真机), M2S 除外(微秒)。
"""
import sys, re, os

PAT_BOOT = re.compile(r'\[BOOT\] (\S+) cycles=([0-9A-Fa-f]+)')
PAT_PERF = re.compile(r'\[PERF\] intervalK=([0-9A-Fa-f]+) renderK=([0-9A-Fa-f]+) presentK=([0-9A-Fa-f]+) ratio=([0-9A-Fa-f]+)(?: p99K=([0-9A-Fa-f]+) maxK=([0-9A-Fa-f]+))?')
PAT_A3   = re.compile(r'\[A3\] sys=([0-9A-Fa-f]+) calls=([0-9A-Fa-f]+) avgK=([0-9A-Fa-f]+)')
PAT_M2S  = re.compile(r'\[M2S\] size=(\d+)x(\d+) px=(\d+) full_us=([0-9.]+) dirty_us=([0-9.]+)')

def parse(path):
    boot, perf, a3, m2s = [], [], [], []
    try:
        with open(path, 'r', errors='ignore') as f:
            for line in f:
                m = PAT_BOOT.search(line)
                if m: boot.append((m.group(1), int(m.group(2), 16))); continue
                m = PAT_PERF.search(line)
                if m:
                    perf.append(dict(intervalK=int(m.group(1),16), renderK=int(m.group(2),16),
                                     presentK=int(m.group(3),16), ratio=int(m.group(4),16),
                                     p99K=int(m.group(5),16) if m.group(5) else None,
                                     maxK=int(m.group(6),16) if m.group(6) else None)); continue
                m = PAT_A3.search(line)
                if m: a3.append((int(m.group(1),16), int(m.group(2),16), int(m.group(3),16))); continue
                m = PAT_M2S.search(line)
                if m:
                    w,h,px,fu,du = int(m.group(1)),int(m.group(2)),int(m.group(3)),float(m.group(4)),float(m.group(5))
                    m2s.append(dict(w=w,h=h,px=px,full_us=fu,dirty_us=du))
    except FileNotFoundError:
        print(f'[skip] {path} 不存在')
    return boot, perf, a3, m2s

def fit_m2s(points):
    """dirty_us = a + b*px 的最小二乘。返回 (a,b,r2)"""
    n = len(points)
    if n < 2: return None
    xs = [p['px'] for p in points]; ys = [p['dirty_us'] for p in points]
    mx = sum(xs)/n; my = sum(ys)/n
    sxx = sum((x-mx)**2 for x in xs); sxy = sum((x-mx)*(y-my) for x,y in zip(xs,ys))
    if sxx == 0: return None
    b = sxy/sxx; a = my - b*mx
    ss_tot = sum((y-my)**2 for y in ys)
    ss_res = sum((y-(a+b*x))**2 for x,y in zip(xs,ys))
    r2 = 1 - ss_res/ss_tot if ss_tot else 1.0
    return a, b, r2

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    md = '--md' in sys.argv
    only_m2s = '--m2s' in sys.argv
    if not args:
        print(__doc__); return 1
    all_boot, all_perf, all_a3, all_m2s = [], [], [], []
    for p in args:
        b, pf, a3, m2 = parse(p)
        all_boot += b; all_perf += pf; all_a3 += a3; all_m2s += m2

    if only_m2s or all_m2s:
        print('\n## ARM64 变尺寸扫描 [M2S]\n')
        print('| size | px | full_us | dirty_us | ns/px | full/dirty |')
        print('|---|---|---|---|---|---|')
        for p in sorted(all_m2s, key=lambda q: q['px']):
            ns = (p['dirty_us'] / p['px']) * 1000 if p['px'] else 0
            print(f"| {p['w']}x{p['h']} | {p['px']} | {p['full_us']:.0f} | {p['dirty_us']:.1f} | {ns:.2f} | {p['full_us']/p['dirty_us']:.1f}x |")
        f = fit_m2s(all_m2s)
        if f:
            a, b, r2 = f
            print(f"\n拟合: dirty_us = {a:.2f} µs + {b:.5f} µs/px   (R² = {r2:.4f})")
            print(f"→ per-call 固定开销 ≈ {a:.2f} µs，每像素 ≈ {b:.3f} ns/px")
            if b > 0:
                # 脏区"失去意义"的阈值: dirty_us 达到 full_us 的 90%
                full = sorted(all_m2s, key=lambda q: -q['full_us'])[0]['full_us']
                thr = (0.9*full - a)/b
                print(f"→ 阈值(脏区耗时达整屏 90% 时的像素数) ≈ {thr:.0f} px"
                      f"（整屏 {full:.0f} µs 的 90% = {0.9*full:.0f} µs）")
        return 0

    if all_boot:
        print('\n## A-1 启动基准（TSC 周期）\n')
        seen = {}
        for tag, c in all_boot:
            seen.setdefault(tag, []).append(c)
        print('| phase | n | min | max | 波动 |')
        print('|---|---|---|---|---|')
        for tag, cs in seen.items():
            print(f'| {tag} | {len(cs)} | {min(cs)} | {max(cs)} | {(max(cs)-min(cs))/max(1,min(cs))*100:.1f}% |')

    if all_perf:
        print('\n## A-2 帧率（TSC 千周期）\n')
        rows = [p for p in all_perf if p['p99K'] is not None]
        if rows:
            print('| intervalK | renderK | presentK | ratio% | p99K | maxK | p99/avg |')
            print('|---|---|---|---|---|---|---|')
            for p in rows[-10:]:
                pa = (p['p99K'] / p['intervalK'] * 100) if p['intervalK'] else 0
                print(f"| {p['intervalK']} | {p['renderK']} | {p['presentK']} | {p['ratio']}% | {p['p99K']} | {p['maxK']} | {pa:.1f}% |")
        else:
            print('(日志里还没有带 p99K 的 PERF 行 —— 需要这版内核的实测日志)')

    if all_a3:
        print('\n## A-3 syscall（按号）\n')
        print('| sys | calls | avgK(千周期) |')
        print('|---|---|---|')
        for num, calls, avg in sorted(all_a3, key=lambda q: -q[1])[:20]:
            print(f'| {num} | {calls} | {avg} |')
    return 0

if __name__ == '__main__':
    sys.exit(main())
