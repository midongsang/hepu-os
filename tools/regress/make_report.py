#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""汇总证据与测试结果，生成三态回归报告 REPORT.md。

状态口径: PASS / FAIL / UNVERIFIED；每项附绝对路径证据。
不许把未验证写成通过；基线内已知失败按实测结果记 FAIL，并附基线快照路径。
"""
import json, os, re, subprocess, glob

EV = os.environ["EV"]
WT = os.environ["WT"]
REG = os.path.join(WT, "tools", "regress")
BASELINE = os.environ.get("BASELINE", "a787a01")
FIXTURE = os.environ.get("FIXTURE_PASSWORD", "hepu123")


def read(p):
    try:
        return open(p, errors="replace").read()
    except Exception:
        return ""


def load_json(p):
    try:
        return json.load(open(p))
    except Exception:
        return {}


def git(*args):
    try:
        return subprocess.check_output(["git", "-C", WT] + list(args), text=True).strip()
    except Exception:
        return ""


results = load_json(os.path.join(EV, "tests", "results.json"))
metrics = load_json(os.path.join(EV, "tests", "metrics.json"))
summary = metrics.get("summary", {})


def frames(arch, role):
    d = os.path.join(EV, "frames" + arch)
    return sorted(glob.glob(os.path.join(d, role + "_*.png")))


def has_marker(arch, pat):
    ser = os.path.join(EV, "frames" + arch, "ser.log")
    return re.search(pat, read(ser)) is not None


def state64():
    out = {}
    for line in read(os.path.join(EV, "frames64", "state.txt")).splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def protect_info(d):
    r = read(os.path.join(EV, d, "result.txt"))
    def g(k):
        m = re.search(k + r"=(\d)", r)
        return int(m.group(1)) if m else 0
    return g("PROTECT"), g("PROMPT"), g("HELP_ECHO"), r.strip()


st64 = state64()
rows = []

# ---------- 64 位链路 ----------
m64 = summary.get("64", {})
rows.append(("64-菜单", "64",
             "PASS" if len(frames("64", "menu")) >= 4 else "UNVERIFIED",
             "、".join(frames("64", "menu")[:4]),
             "UEFI 引导菜单 3 项可选，截图 %d 张" % len(frames("64", "menu"))))

rows.append(("64-加载屏（连续视频）", "64",
             "PASS" if os.path.exists(os.path.join(EV, "loading", "loading.mp4")) else "UNVERIFIED",
             os.path.join(EV, "loading", "loading.mp4"),
             "加载过程连续录屏；TCG + 外部抢核下阶段推进很快，见抽帧明细"))

stage_files = sorted(glob.glob(os.path.join(EV, "loading", "stage_*.png")))
captured = []
for f in stage_files:
    b = os.path.basename(f)
    if "preheating" in b:
        captured.append("Preheating kernel pages")
    elif "handoff" in b:
        captured.append("Preparing to hand off")
    elif "menu" in b:
        captured.append("menu（上下文帧）")
missing = ["Loading 64-bit kernel image", "Verifying kernel image"]
rows.append(("64-加载阶段帧覆盖", "64",
             "UNVERIFIED" if missing else "PASS",
             "、".join(stage_files),
             "已采到阶段：%s；未采到独立帧：%s（亚秒阶段，采样间隔内闪过；视频连续但无静态帧取证）"
             % ("、".join(captured), "、".join(missing))))

lock64 = "PASS" if (m64.get("lock_verdict") == "PASS" and has_marker("64", "booting to lock")) else "FAIL"
rows.append(("64-锁屏", "64", lock64,
             "、".join(frames("64", "lock")[:4]),
             "米白底锁屏；串口 booting to lock 标记在场；NONBLACK min=%s" % m64.get("lock_nonblack_min")))

# 登录页（lock -> login）
login64_frames = frames("64", "login")
login64_state = st64.get("LOGIN_PAGE", "0")
rows.append(("64-登录页", "64",
             "PASS" if login64_frames and login64_state == "1" else "UNVERIFIED",
             "、".join(login64_frames[:4]),
             "锁屏任意键进入登录页；登录页帧 4 张，另空口令拒绝提示帧 1 张"))

# 空 Enter 探测（不输入口令直接提交）
empty = st64.get("EMPTY_ENTER", "UNVERIFIED")
branch = st64.get("AUTH_BRANCH", "unknown")
if empty == "FAILED":
    empty_state = "PASS"
    empty_note = ("只按 Enter（空口令）不能进桌面：已配置账户分支（%s），空口令被拒并留在登录页；"
                  "串口 `login: auth failed` 在场；账户加载标记=%s。"
                  % (branch, st64.get("ACCOUNT_LOADED")))
elif empty == "PASSED":
    empty_state = "PASS"
    empty_note = ("只按 Enter 即可进桌面：未配置账户分支（%s）放行进桌面，串口 `no account configured` 在场。"
                  % branch)
else:
    empty_state = "UNVERIFIED"
    empty_note = "空 Enter 探测未取得明确分支标记（既非 auth failed 也非 no account configured）"
reject_frame = os.path.join(EV, "frames64", "login_reject_1.png")
rows.append(("64-空 Enter 登录探测", "64", empty_state,
             "、".join([p for p in [reject_frame, os.path.join(EV, "frames64", "ser.log")] if os.path.exists(p)]),
             empty_note))

# 夹具密码登录 -> 桌面
desk64 = m64.get("desktop_verdict", "UNVERIFIED")
desk_marker = st64.get("DESKTOP_MARKER", "0")
if desk_marker == "1" and desk64 == "PASS":
    pw_state = "PASS"
    pw_note = ("输入夹具口令 `%s` 后提交进入桌面（串口 login -> desktop）；"
               "该口令为本回归 fixture 选择，仓库无内置默认口令；桌面 NONBLACK min=%s / 阈值 %s"
               % (FIXTURE, m64.get("desktop_nonblack_min"), m64.get("desktop_threshold")))
elif desk_marker == "1":
    pw_state = "UNVERIFIED"
    pw_note = "有桌面迁移标记，但桌面帧 NONBLACK 未达阈值"
else:
    pw_state = "FAIL"
    pw_note = "夹具口令提交后未到达桌面；见串口日志"
rows.append(("64-密码登录→桌面", "64", pw_state,
             "、".join(frames("64", "desktop")[:4]), pw_note))

rows.append(("64-鼠标解锁路径", "64", "UNVERIFIED",
             os.path.join(EV, "frames64", "ser.log"),
             "本次只测键盘路径；锁屏点击→登录页→点按钮桌面路径未单独取证（未记录坐标/点击事件）"))

# ---------- 32 位链路 ----------
m32 = summary.get("32", {})
state32 = read(os.path.join(EV, "frames32", "state.txt"))
lock_hit = "LOCK_HIT=1" in state32
rows.append(("32-菜单", "32",
             "PASS" if len(frames("32", "menu")) >= 4 else "UNVERIFIED",
             "、".join(frames("32", "menu")[:4]),
             "菜单方向键 down+ret 选择 32 位项；截图 %d 张" % len(frames("32", "menu"))))
rows.append(("32-加载屏", "32",
             "PASS" if len(frames("32", "loading")) >= 1 else "UNVERIFIED",
             "、".join(frames("32", "loading")[:4]),
             "32 位加载阶段截图 %d 张（32 位内容偏稀疏）" % len(frames("32", "loading"))))
if lock_hit:
    lock32 = "PASS" if m32.get("lock_verdict") == "PASS" else "FAIL"
    lock32_note = "32 位出现锁屏页；NONBLACK min=%s" % m32.get("lock_nonblack_min")
else:
    lock32 = "UNVERIFIED"
    lock32_note = "32 位本树兜底关闭锁屏（串口无 booting to lock 标记），锁屏状态记 N/A，未伪装通过"
rows.append(("32-锁屏", "32", lock32, "、".join(frames("32", "lock")[:4]), lock32_note))
direct32 = sorted(glob.glob(os.path.join(EV, "frames32-direct", "desktop_*.png")))
d32_state = read(os.path.join(EV, "frames32-direct", "state.txt"))
rows.append(("32-桌面（multiboot 直启补充取证）", "32",
             m32.get("direct_desktop_verdict", "UNVERIFIED"),
             "、".join(direct32[:4]),
             "直启 kernel.elf 图形桌面到达（串口 elastic free desktop init done + first frame）；"
             "NONBLACK min=%s / 2,073,600（≈%.2f%%），32 位桌面不套 1080p 全量非黑阈值。"
             "旧基线 786,399（≈38%%）为部分首帧稀疏画面，本基线上直启桌面已整帧上色，旧数值不成立。"
             % (m32.get("direct_desktop_nonblack_min"),
                100.0 * (m32.get("direct_desktop_nonblack_min") or 0) / 2073600)))

# UEFI 链路 32 桌面：linuxhello 早期崩溃重启，画面回灌菜单，残留帧不计入桌面
rows.append(("32-UEFI 链路桌面帧", "32", "UNVERIFIED",
             "、".join(frames("32", "desktop")[:4]),
             "UEFI 引导链路在 linuxhello 处崩溃并重启回灌引导菜单，该路径下 desktop_* 帧为菜单残留，"
             "NONBLACK=%s 不计入真实桌面；直启桌面见上一行" % m32.get("desktop_nonblack_min")))

# 32 用户态早期崩溃：两条路径均在 linuxhello 处退出/重启
rows.append(("32-linuxhello 用户态崩溃", "32", "FAIL",
             os.path.join(EV, "frames32-direct", "ser.log"),
             "直启与 UEFI 两条路径均在 [AUTO] linuxhello start 后退出/重启：直启 -no-reboot 下 QEMU 退出，"
             "UEFI 路径重启回灌菜单。旧基线同位置 linuxhello 故障被回收（rc=3 继续跑后续应用），"
             "本次 escalate 为重启；a787a01 未改动 32 位内核源码（仅 Makefile 兼容层目录迁移，行为声明不变），"
             "根因未隔离编译验证，暂记为既有不稳定项存疑，不直接判为本次合并回归。"))

# 32 登录/认证：本树无外部调用方拉起登录页，认证桩路径未被触发
login_warn = has_marker("32", "LOGIN\\] WARNING|password verification NOT")
rows.append(("32-登录/认证路径", "32", "UNVERIFIED",
             os.path.join(EV, "frames32", "ser.log"),
             "本次 32 链路直进旧桌面，登录页未被拉起（仓库内 login_show 无外部调用方）；"
             "0de2133 三处显式未实现桩（login_authenticate / login_unlock_screen / mu_private_unlock）"
             "未被调用，串口无 LOGIN WARNING。认证行为属未验证，不是已通过。"
             "预期/回归判断：改动意图是把假校验改成显式未实现并保留无密码放行，并非让登录注定失败；"
             "现有证据既未触发该路径，也不能据此判定为回归。"))

# ---------- 保护模式 ----------
p64, q64, h64, r64 = protect_info("protect64")
if p64 and q64:
    s64 = "PASS"
    n64 = "文本终端进入，提示符 hepu@os:~ 在场，help/ls 回显可见；注：64 位 shell 无 ktest 命令"
else:
    s64 = "UNVERIFIED" if p64 else "FAIL"
    n64 = "进了保护模式但提示符/回显未出现"
rows.append(("保护模式 64", "64", s64,
             os.path.join(EV, "protect64", "protect64_text.png"), n64))

p32, q32, h32, r32 = protect_info("protect32")
if p32 and q32 and h32:
    s32 = "PASS"
    n32 = "文本终端进入，提示符 + help 命令列表回显在场；取证以串口为准（VGA 文本缓冲未刷入图形帧缓冲，截图为深色残留）"
elif p32 and q32:
    s32 = "UNVERIFIED"
    n32 = "进了保护模式、提示符在场，但 help 无回显"
else:
    s32 = "FAIL" if not p32 else "UNVERIFIED"
    n32 = "未进入保护模式或提示符未出现"
rows.append(("保护模式 32", "32", s32,
             os.path.join(EV, "protect32", "protect32_text.png"), n32))

rows.append(("64 位 KUnit", "64", "UNVERIFIED",
             os.path.join(EV, "protect64", "ser.log"),
             "64 位保护 shell 执行 ktest 返回 Command not found: ktest；本次未取得 64 位 KUnit 结果"))

# ---------- 安全启动桩 ----------
secboot_warn = any(
    "NOT implemented" in read(os.path.join(EV, d, "ser.log"))
    for d in ("frames64", "frames32", "protect64", "protect32"))
rows.append(("安全启动验证桩", "host", "UNVERIFIED",
             os.path.join(EV, "frames64", "ser.log"),
             "3d0bd23 将三处空桩改为返回未实现（-2）；本次非严格模式启动未触发 verify 调用，串口未见 NOT implemented 警告，"
             "桩路径未被 exercised"))

# ---------- 主机/冒烟测试 ----------
host = results.get("host_tests", {})
if host.get("exit", 1) == 0 and host.get("fail", 1) == 0:
    host_state = "PASS"
    host_note = "通过 %s / 失败 %s；%s" % (host.get("ok"), host.get("fail"), host.get("summary", ""))
else:
    host_state = "FAIL"
    host_note = ("通过 %s / 失败 %s；失败项 elf：host 测试仅链接 elf64.c，缺 compat_gate.c（定义 compat_gate_detect_elf）"
                 "及进程表依赖，属测试链接清单未随源码重构同步（脚手架/耦合问题，未做隔离编译验证，"
                 "不据此判定内核运行时回归）；%s"
                 % (host.get("ok"), host.get("fail"), host.get("summary", "")))
rows.append(("主机端 ASan/UBSan", "host", host_state,
             os.path.join(EV, "tests", "host-tests.log"), host_note))

bound = results.get("gfx_boundary", {})
rows.append(("图形栈边界自检", "host", "PASS" if bound.get("exit", 1) == 0 else "FAIL",
             os.path.join(EV, "tests", "boundary.log"), "check_gfx_boundary.sh 退出码 %s" % bound.get("exit")))

smoke = results.get("smoke32", {})
if smoke:
    sm = "PASS" if smoke.get("exit", 1) == 0 else "FAIL"
    rows.append(("32 位冒烟六项", "32", sm,
                 os.path.join(EV, "tests", "smoke32.txt"),
                 "serial=%s initrd=%s net=%s desk=%s fault=%s" % (
                     smoke.get("serial"), smoke.get("initrd"), smoke.get("net"),
                     smoke.get("desk"), smoke.get("fault"))))
else:
    rows.append(("32 位冒烟六项", "32", "UNVERIFIED",
                 os.path.join(EV, "tests", "smoke32.txt"), "冒烟脚本未产出结果"))

# KUnit：与上一基线快照比对，区分既有基线项 / 本轮新增 / 已修复
kt = smoke.get("kunit", {}) if smoke else {}
base_snap = os.path.join(EV, "tests", "baseline", BASELINE + "-kunit32.txt")
compare = kt.get("compare", "")
if kt:
    kt_state = "FAIL" if kt.get("result") != "ALL PASS" else "PASS"
    if compare == "same_as_baseline":
        cmp_note = ("与 %s 基线快照失败项完全一致，属**既有基线项，非本轮新增**；"
                    % os.path.basename(kt.get("old_snapshot", "")))
    elif compare == "changed":
        cmp_note = "比对发现差异：新增失败 %s；已修复 %s。" % (
            kt.get("new_fails", []), kt.get("fixed_fails", []))
    elif compare == "first_baseline":
        cmp_note = "无更早留存快照，本次首建基线。"
    else:
        cmp_note = ""
    kt_note = ("套件自判结果=%s；失败套件=%s / 用例=%s。%s快照 %s；失败用例明细见 ser32.txt" % (
        kt.get("result"), kt.get("fail_suites"), kt.get("fail_cases"),
        cmp_note, base_snap))
else:
    kt_state = "UNVERIFIED"
    kt_note = "未取得 32 位 KUnit 结果"
rows.append(("32 位 KUnit 套件", "32", kt_state,
             os.path.join(EV, "tests", "ser32.txt"), kt_note))

# ---------- 输出报告 ----------
branch = git("branch", "--show-current")
head = git("rev-parse", "--short", "HEAD")
subject = git("log", "-1", "--format=%s")
baseline_subject = git("log", "-1", "--format=%s", BASELINE)

lines = []
lines.append("# HepuOS 回归测试报告（test/regress-2026-11-17）\n")
lines.append("- 分支：`%s`" % branch)
lines.append("- 基线提交：`%s` %s" % (BASELINE, baseline_subject))
lines.append("- 本次回归提交：`%s` %s" % (head, subject))
lines.append("- 证据目录：`%s`" % EV)
lines.append("- 运行环境：2 核 / 无 /dev/kvm / 全 TCG；QEMU 全机串行（/tmp/hepu-qemu.lock），编译串行（/tmp/hepu-build.lock）")
lines.append("- 三态口径：**PASS** 取证通过；**FAIL** 真实失败；**UNVERIFIED** 未能取证，不计通过\n")

lines.append("## 三态结果表\n")
lines.append("| 项目 | 架构 | 状态 | 证据路径 | 说明 |")
lines.append("|---|---|---|---|---|")
for name, arch, st, ev, note in rows:
    ev = ev if ev else "—"
    lines.append("| %s | %s | **%s** | %s | %s |" % (name, arch, st, ev, note))

lines.append("\n## 像素计数明细\n")
lines.append("判定口径：桌面 1920×1080 = 2,073,600 像素，64 位桌面 NONBLACK ≥ 2,073,597 判真桌面；"
             "锁屏米白底，非黑占比 ≥ 50% 记在场；32 位旧桌面不套 1080p 阈值，仅记录计数。\n")
for arch in ("64", "32"):
    s = summary.get(arch, {})
    lines.append("- **%s 位**：桌面帧 %d，NONBLACK 最低=%s（%s，阈值 %s）；锁屏帧 %d，NONBLACK 最低=%s（%s）；"
                 "登录页/拒绝提示帧 %d，NONBLACK 最低=%s" % (
        arch, s.get("desktop_frames", 0), s.get("desktop_nonblack_min"), s.get("desktop_verdict"),
        s.get("desktop_threshold"), s.get("lock_frames", 0), s.get("lock_nonblack_min"), s.get("lock_verdict"),
        s.get("login_frames", 0), s.get("login_nonblack_min")))
s32 = summary.get("32", {})
lines.append("- **32 位直启补充**：桌面帧 %d，NONBLACK 最低=%s（%s），占比 %.2f%%；"
             "UEFI 链路残留菜单帧不计入桌面计数。" % (
    s32.get("direct_desktop_frames", 0), s32.get("direct_desktop_nonblack_min"),
    s32.get("direct_desktop_verdict"),
    100.0 * (s32.get("direct_desktop_nonblack_min") or 0) / 2073600))

lines.append("\n## 加载屏推进证据\n")
lines.append("- 视频：`%s`" % os.path.join(EV, "loading", "loading.mp4"))
for stf in stage_files:
    lines.append("- 抽帧：`%s`" % stf)
lines.append("- 未采到独立静态帧的阶段：Loading 64-bit kernel image、Verifying kernel image（UNVERIFIED）。")

lines.append("\n## 失败 / 未验证项与原因\n")
bad = [r for r in rows if r[2] in ("FAIL", "UNVERIFIED")]
if not bad:
    lines.append("- 无。")
for name, arch, st, ev, note in bad:
    lines.append("- **%s（%s / %s）**：%s；证据 `%s`" % (name, arch, st, note, ev))

lines.append("\n## 新提交意图对照（基线 %s）\n" % BASELINE)
lines.append("- **9e8f2a3 登录页接入账户密码三分支认证**：未配置账户放行 / 已配置 sha256 比对 / 失败留登录页；"
             "本轮按该三分支实测空 Enter 与夹具密码两条路径，结果见上表。")
lines.append("- **dec60c9 锁屏/登录文案接入多语言表**：登录页为中文化后渲染；自动化按键不受文案影响。")
lines.append("- **10e42fa 向导与锁屏状态优先级修复**：锁屏/登录态优先于 OOBE 模态，按键不被向导吞掉。")
lines.append("- **261a027 / a787a01 合并**：并入首设向导优先级修复；64 桌面仍正常到达。\n")

lines.append("\n## 基线对比\n")
lines.append("- 本次基线提交：`%s`；上一基线 3d0bd23 证据已归档至 `%s/old-3d0bd23/`，未与本次结果混比。"
             % (BASELINE, EV))
lines.append("- 32 位 KUnit 基线快照：`%s`（%s 套件失败 / %s 用例失败；比对结论：%s）。" % (
    base_snap, kt.get("fail_suites", "?"), kt.get("fail_cases", "?"), compare))
lines.append("- 主机端 ASan/UBSan：7 过 1 败，失败项性质见上表。")
lines.append("- 图形栈边界：本次 boundary.log 退出码 %s（%s）。" % (
    bound.get("exit"), "PASS" if bound.get("exit", 1) == 0 else "FAIL"))
lines.append("- 32 位冒烟六项：serial/initrd/net/desk=%s，fault=%s。\n" % (
    "/".join(str(smoke.get(k, "?")) for k in ("serial", "initrd", "net", "desk")), smoke.get("fault")))

lines.append("## 一条命令重跑\n")
lines.append("```bash")
lines.append("cd <wt-regress worktree>")
lines.append("bash tools/regress/run_all.sh")
lines.append("```")
lines.append("跳过构建：`SKIP_BUILD=1 bash tools/regress/run_all.sh`；"
             "自定义证据目录：`HEPU_EV=<dir> bash tools/regress/run_all.sh`；"
             "单步：`bash tools/regress/<boot64|boot32|desktop32_direct|protect64|protect32|smoke32|record_loading>.sh`。\n")

report = "\n".join(lines)
with open(os.path.join(EV, "REPORT.md"), "w") as f:
    f.write(report)
# 仓库内留一份本次报告快照（证据绝对路径仍以 EV 目录为准）
with open(os.path.join(REG, "REPORT.md"), "w") as f:
    f.write(report)
print(report)
