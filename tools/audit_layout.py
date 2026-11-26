#!/usr/bin/env python3
"""
赫普OS 系统层级 / 命名空间布局审计工具（纯 Python，无第三方依赖）。

用途：
  对比 32 位根 Makefile 与 64 位 kernel/arch/x86_64/Makefile 的源文件清单，
  输出下列五类结论（全部来自实际解析出的清单与磁盘事实，不手写猜测）：

    1. 只在 32 位编入的 shared 共用模块（ERROR 级：共用模块单边编译）
    2. 只在 64 位编入的 shared 共用模块（ERROR 级）
    3. 两边都编但同名文件路径归属不一致的模块
       - 一侧 arch/、另一侧 shared/ -> ERROR（归属错位）
       - 两侧均为架构/子系统平行实现 -> INFO（历史平行集合，仅登记）
    4. 64 位 vpath 未覆盖到的、且含 .c 源码的 shared 子目录（ERROR 级）
    5. kernel/shared/**.c 中 #include 解析后落到 arch/ 的可疑反向依赖（ERROR 级）

  附加自检：64 位清单列出但磁盘不存在的文件；32 位 glob 意外收录的 arch/x86_64 文件。

退出码：
  0 = 无 ERROR 级发现（INFO 不影响退出码）
  1 = 存在 ERROR 级发现，便于接入 CI 门禁

报告：
  默认写入 docs/status/evidence-layout-2026-11-26/layout-audit-report.md，
  可用 --out 覆盖；--no-write 仅打印不落盘。

口径与 Makefile 保持一致：
  - 32 位：`find kernel -name "*.c"`，排除
      kernel/sched/scheduler.c、kernel/arch/x86_64/%、kernel/arch/aarch64/%，
    再追加 HP_DESK_DEPS（hpdesk.c / hs_ui.c / hsqml.c / l10n64.c / png64.c）。
  - 64 位：C_SOURCES 显式清单 + vpath %.c 目录列表。
"""

import datetime
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ROOT_MAKEFILE = os.path.join(ROOT, "Makefile")
ARCH64_MAKEFILE = os.path.join(ROOT, "kernel", "arch", "x86_64", "Makefile")
KERNEL_DIR = "kernel"
ARCH64_DIR = "kernel/arch/x86_64"
ARCH32_DIR = "kernel/arch/x86"
ARCH_AARCH64 = "kernel/arch/aarch64"
SHARED_ROOT = "kernel/shared"

# 32 位 -I 搜索路径（根 Makefile CFLAGS）；64 位 -I 路径在解析时并入。
INC32 = [
    os.path.join(KERNEL_DIR, "include"),
    KERNEL_DIR,
    SHARED_ROOT,
]


# ---------------------------------------------------------------------------
# 清单解析
# ---------------------------------------------------------------------------

def _rel(path):
    """归一化为相对 ROOT 的 POSIX 风格路径。"""
    p = os.path.relpath(os.path.abspath(path), ROOT)
    return p.replace(os.sep, "/")


def parse_root_makefile():
    """解析 32 位根 Makefile，返回 (源文件集合, 显式回追的 x86_64 依赖集合, 排除模式)。"""
    text = open(ROOT_MAKEFILE, errors="replace").read()

    # HP_DESK_SHARED / HP_DESK_DEPS：被 32 位显式追加回来的 x86_64 纯软件文件。
    deps = set()
    ms = re.search(r"HP_DESK_SHARED\s*=\s*\$\(KERNEL_DIR\)/(\S+)", text)
    if ms:
        deps.add(KERNEL_DIR + "/" + ms.group(1))
    m = re.search(r"HP_DESK_DEPS\s*=\s*(.*?)(?:\n#|\n\n|\n[A-Z_]+\s*=)", text, re.S)
    if m:
        block = m.group(1).replace("$(HP_DESK_SHARED)", " ".join(deps))
        for tok in re.findall(r"\$\(KERNEL_DIR\)/\S+", block):
            deps.add(tok.replace("$(KERNEL_DIR)", KERNEL_DIR))

    # 排除模式（与 C_SRCS 行 filter-out 对齐）。
    excludes = [
        os.path.join(KERNEL_DIR, "sched", "scheduler.c"),
        ARCH64_DIR + "/%",
        ARCH_AARCH64 + "/%",
    ]

    def excluded(p):
        for pat in excludes:
            if pat.endswith("/%"):
                if p.startswith(pat[:-1]):
                    return True
            elif p == pat:
                return True
        return False

    sources = set()
    for dirpath, _, files in os.walk(os.path.join(ROOT, KERNEL_DIR)):
        for fn in files:
            if fn.endswith(".c"):
                rel = _rel(os.path.join(dirpath, fn))
                if not excluded(rel):
                    sources.add(rel)
    sources |= deps
    return sources, deps, excludes


def parse_arch64_makefile():
    """解析 64 位 Makefile，返回 (C_SOURCES 集合, vpath 目录列表, -I 目录列表)。"""
    text = open(ARCH64_MAKEFILE, errors="replace").read()

    # C_SOURCES 块：从 `C_SOURCES = \` 到 `ASM_SOURCES` 之前。
    m = re.search(r"C_SOURCES\s*=\s*\\(.*?)ASM_SOURCES", text, re.S)
    sources = set()
    if m:
        for line in m.group(1).splitlines():
            line = line.strip().rstrip("\\").strip()
            if not line:
                continue
            line = line.replace("$(KERNEL_DIR)", KERNEL_DIR)
            line = line.replace("$(ARCH_DIR)", ARCH64_DIR)
            sources.add(line)

    # vpath %.c 目录列表。
    vpath_dirs = []
    mv = re.search(r"vpath\s+%\.c\s+(.*)", text)
    if mv:
        vpath_dirs = [
            t.replace("$(KERNEL_DIR)", KERNEL_DIR).replace("$(ARCH_DIR)", ARCH64_DIR)
            for t in mv.group(1).split()
        ]

    # -I 目录（用于反向依赖解析）。
    inc_dirs = []
    for mi in re.finditer(r"-I\$?\(?([^\s)]+)\)?", text):
        d = mi.group(1).replace("$(KERNEL_DIR)", KERNEL_DIR).replace(
            "$(ARCH_DIR)", ARCH64_DIR
        )
        inc_dirs.append(d)

    return sources, vpath_dirs, inc_dirs


# ---------------------------------------------------------------------------
# 检查项
# ---------------------------------------------------------------------------

def shared_files(src_set):
    return sorted(p for p in src_set if p.startswith(SHARED_ROOT + "/"))


def check_shared_asymmetry(s32, s64):
    """1/2. shared 共用模块单边编译。"""
    sh32, sh64 = set(shared_files(s32)), set(shared_files(s64))
    only32 = sorted(sh32 - sh64)
    only64 = sorted(sh64 - sh32)
    return only32, only64


def check_path_mismatch(s32, s64):
    """3. 同名文件两边路径归属不一致。返回 (errors, infos)。"""
    by32 = {os.path.basename(p): p for p in s32}
    by64 = {os.path.basename(p): p for p in s64}
    errors, infos = [], []
    for base in sorted(set(by32) & set(by64)):
        a, b = by32[base], by64[base]
        if a == b:
            continue
        a_shared, b_shared = a.startswith(SHARED_ROOT + "/"), b.startswith(SHARED_ROOT + "/")
        a_arch, b_arch = "/arch/" in a, "/arch/" in b
        if (a_shared and b_arch) or (b_shared and a_arch):
            errors.append((a, b, "shared/arch 归属错位"))
        else:
            infos.append((a, b, "历史平行实现（两侧各自适配，非归属错位）"))
    return errors, infos


def check_vpath_coverage(vpath_dirs):
    """4. 含 .c 的 shared 子目录是否都在 64 位 vpath 列表里。"""
    vpath_norm = {d.rstrip("/") for d in vpath_dirs}
    missing = []
    shared_root_abs = os.path.join(ROOT, SHARED_ROOT)
    for sub in sorted(os.listdir(shared_root_abs)):
        sub_abs = os.path.join(shared_root_abs, sub)
        if not os.path.isdir(sub_abs):
            continue
        has_c = any(fn.endswith(".c") for fn in os.listdir(sub_abs))
        rel_dir = SHARED_ROOT + "/" + sub
        if has_c and rel_dir not in vpath_norm:
            missing.append(rel_dir)
    return missing


def _resolve_include(src_abs, header, inc_dirs):
    """按同目录 -> -I 顺序解析引号包含头；返回存在的绝对路径或 None。"""
    candidates = [os.path.dirname(src_abs)] + inc_dirs
    for d in candidates:
        cand = os.path.join(ROOT, d, header) if not os.path.isabs(d) else os.path.join(d, header)
        if os.path.exists(cand):
            return cand
    return None


def check_reverse_dependency(s64_inc_dirs):
    """5. shared 源码反向 include 到 arch/。"""
    inc_dirs = list(dict.fromkeys(INC32 + s64_inc_dirs))
    findings = []
    shared_root_abs = os.path.join(ROOT, SHARED_ROOT)
    for dirpath, _, files in os.walk(shared_root_abs):
        for fn in files:
            if not fn.endswith(".c"):
                continue
            src_abs = os.path.join(dirpath, fn)
            src_rel = _rel(src_abs)
            for line in open(src_abs, errors="replace"):
                mi = re.match(r'\s*#include\s*"([^"]+)"', line)
                if not mi:
                    continue
                header = mi.group(1)
                resolved = _resolve_include(src_abs, header, inc_dirs)
                if resolved is None:
                    continue
                if "/arch/" in _rel(resolved):
                    findings.append((src_rel, header, _rel(resolved)))
    return findings


def check_disk_existence(s64, s32, deps):
    """附加自检：64 清单磁盘缺失；32 位意外收录 arch/x86_64 文件。"""
    missing = sorted(p for p in s64 if not os.path.exists(os.path.join(ROOT, p)))
    leaked = sorted(
        p for p in s32 if p.startswith(ARCH64_DIR + "/") and p not in deps
    )
    return missing, leaked


# ---------------------------------------------------------------------------
# 报告输出
# ---------------------------------------------------------------------------

def render_report(baseline, counts, only32_shared, only64_shared, mismatch_errors,
                  mismatch_infos, vpath_missing, reverse_deps, missing_on_disk,
                  leaked_x86_64, errors_total):
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = []
    lines.append("# 系统层级 / 命名空间布局审计报告（2026-11-26）")
    lines.append("")
    lines.append(f"- 生成时间：{now}")
    lines.append(f"- 基线提交：`{baseline}`")
    lines.append(f"- 审计工具：`tools/audit_layout.py`（纯 Python，无第三方依赖）")
    lines.append(f"- 32 位源文件数：{counts['s32']}；64 位源文件数：{counts['s64']}；"
                 f"shared 源码：32 位 {counts['sh32']} / 64 位 {counts['sh64']}")
    lines.append("")
    lines.append("## 1. 只在 32 位编入的 shared 共用模块")
    lines.append("")
    if only32_shared:
        lines += ["- `" + p + "`" for p in only32_shared]
    else:
        lines.append("无。")
    lines.append("")
    lines.append("## 2. 只在 64 位编入的 shared 共用模块")
    lines.append("")
    if only64_shared:
        lines += ["- `" + p + "`" for p in only64_shared]
    else:
        lines.append("无。")
    lines.append("")
    lines.append("## 3. 两边都编、同名不同路径的模块")
    lines.append("")
    lines.append("### 3.1 归属错位（ERROR）")
    lines.append("")
    if mismatch_errors:
        for a, b, why in mismatch_errors:
            lines.append(f"- 32 位 `{a}` ↔ 64 位 `{b}`：{why}")
    else:
        lines.append("无。")
    lines.append("")
    lines.append("### 3.2 历史平行实现（INFO，仅登记）")
    lines.append("")
    if mismatch_infos:
        for a, b, why in mismatch_infos:
            lines.append(f"- 32 位 `{a}` ↔ 64 位 `{b}`：{why}")
    else:
        lines.append("无。")
    lines.append("")
    lines.append("## 4. 64 位 vpath 未覆盖的 shared 源码子目录")
    lines.append("")
    if vpath_missing:
        lines += ["- `" + p + "`" for p in vpath_missing]
    else:
        lines.append("无：所有含 `.c` 的 shared 子目录均已在 `vpath %.c` 列表中。")
    lines.append("")
    lines.append("## 5. shared → arch/ 反向依赖")
    lines.append("")
    if reverse_deps:
        for src, header, resolved in reverse_deps:
            lines.append(f"- `{src}` 包含 `{header}`，解析到 `{resolved}`")
    else:
        lines.append("无：`kernel/shared/` 源码未反向依赖 `kernel/arch/`。")
    lines.append("")
    lines.append("## 6. 附加自检")
    lines.append("")
    lines.append(f"- 64 位清单列出但磁盘不存在："
                 + ("无。" if not missing_on_disk else ""))
    for p in missing_on_disk:
        lines.append(f"  - `{p}`")
    lines.append(f"- 32 位 glob 意外收录的 arch/x86_64 文件："
                 + ("无。" if not leaked_x86_64 else ""))
    for p in leaked_x86_64:
        lines.append(f"  - `{p}`")
    lines.append("")
    lines.append(f"## 结论：ERROR {errors_total} 项")
    lines.append("")
    if errors_total == 0:
        lines.append("本轮未发现缺陷级布局分歧；按「先设计后最小迁移」原则，本轮不搬目录，"
                     "迁移路线见 `docs/design/SYSTEM_LAYOUT_AND_NAMESPACES_2026-11-26.md`。")
    else:
        lines.append("存在缺陷级分歧，需按上表逐项处理后再入 CI。")
    lines.append("")
    return "\n".join(lines)


def main():
    out_path = os.path.join(
        ROOT, "docs", "status", "evidence-layout-2026-11-26",
        "layout-audit-report.md",
    )
    no_write = False
    args = sys.argv[1:]
    if "--no-write" in args:
        no_write = True
    if "--out" in args:
        i = args.index("--out")
        out_path = os.path.abspath(args[i + 1])

    if not os.path.exists(ROOT_MAKEFILE) or not os.path.exists(ARCH64_MAKEFILE):
        print("ERROR: 找不到 32/64 位 Makefile，请在 worktree 根目录运行。", file=sys.stderr)
        return 2

    s32, deps, _ = parse_root_makefile()
    s64, vpath_dirs, inc64 = parse_arch64_makefile()

    only32_shared, only64_shared = check_shared_asymmetry(s32, s64)
    mismatch_errors, mismatch_infos = check_path_mismatch(s32, s64)
    vpath_missing = check_vpath_coverage(vpath_dirs)
    reverse_deps = check_reverse_dependency(inc64)
    missing_on_disk, leaked = check_disk_existence(s64, s32, deps)

    errors = []
    errors += [("shared-only-32", p) for p in only32_shared]
    errors += [("shared-only-64", p) for p in only64_shared]
    errors += [("mismatch", f"{a} <> {b}") for a, b, _ in mismatch_errors]
    errors += [("vpath-gap", p) for p in vpath_missing]
    errors += [("reverse-dep", f"{src} -> {resolved}") for src, _, resolved in reverse_deps]
    errors += [("missing-on-disk", p) for p in missing_on_disk]
    errors += [("leaked-x86_64", p) for p in leaked]

    try:
        baseline = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=ROOT, capture_output=True, text=True, timeout=5,
        ).stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        baseline = "unknown"

    counts = {
        "s32": len(s32), "s64": len(s64),
        "sh32": len(shared_files(s32)), "sh64": len(shared_files(s64)),
    }

    report = render_report(
        baseline, counts, only32_shared, only64_shared, mismatch_errors,
        mismatch_infos, vpath_missing, reverse_deps, missing_on_disk,
        leaked, len(errors),
    )

    print("=== 布局审计（audit_layout）===")
    print(f"32 位源文件: {counts['s32']}  64 位源文件: {counts['s64']}  "
          f"shared: {counts['sh32']}/{counts['sh64']}")
    print(f"shared 仅 32 位: {len(only32_shared)}  仅 64 位: {len(only64_shared)}")
    print(f"归属错位: {len(mismatch_errors)}  平行实现 INFO: {len(mismatch_infos)}")
    print(f"vpath 缺口: {len(vpath_missing)}  反向依赖: {len(reverse_deps)}")
    print(f"磁盘缺失: {len(missing_on_disk)}  32 位泄漏 x86_64: {len(leaked)}")
    for kind, msg in errors:
        print(f"  ERROR [{kind}] {msg}")
    if mismatch_infos:
        for a, b, why in mismatch_infos:
            print(f"  INFO  平行: {a} <> {b}")
    print(f"errors={len(errors)}")

    if not no_write:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", errors="replace") as f:
            f.write(report)
        print(f"报告已写入: {os.path.relpath(out_path, ROOT)}")

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
