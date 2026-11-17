#!/bin/bash
# ============================================================================
# 回归测试汇总：主机端 ASan/UBSan 套件 + 图形栈边界自检 + 32 位冒烟 + KUnit 结果。
# 不新增失败为口径；无既往基线项在报告里写明"本次首次建立基线"。
#
# 用法: bash tools/regress/run_tests.sh
# 产物: $EV/tests/{host-tests.log, boundary.log, smoke32.txt, ser32.txt, results.json}
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

T="$EV/tests"; mkdir -p "$T"
cd "$WT"

# ---- A) 主机端测试（编译走构建锁）----
echo "==== A) host-tests (ASan/UBSan) ===="
exec {blfd}>"$BUILD_LOCK"; flock -w 900 "$blfd"
bash tools/host-tests/run_host_tests.sh > "$T/host-tests.log" 2>&1
HOST_RC=$?
flock -u "$blfd"
tail -3 "$T/host-tests.log"

# ---- B) 图形栈边界自检 ----
echo "==== B) gfx boundary ===="
bash tools/check_gfx_boundary.sh > "$T/boundary.log" 2>&1
BOUND_RC=$?
tail -8 "$T/boundary.log"

# ---- C) 32 位冒烟 + KUnit（QEMU 锁内，独立脚本）----
echo "==== C) 32-bit smoke ===="
bash "$REGRESS_DIR/smoke32.sh" 70
SMOKE_RC=$?

# ---- D) 汇总 JSON ----
HOST_SUMMARY=$(grep -E '=== 结果:' "$T/host-tests.log" | tail -1)
python3 - "$T" "$HOST_RC" "$BOUND_RC" "$SMOKE_RC" "$HOST_SUMMARY" <<'PY'
import json, os, re, sys
td, host_rc, bound_rc, smoke_rc, host_summary = sys.argv[1:6]
def read(p):
    try:
        return open(p, errors="replace").read()
    except Exception:
        return ""
smoke = read(os.path.join(td, "smoke32.txt"))
bound = read(os.path.join(td, "boundary.log"))
host = read(os.path.join(td, "host-tests.log"))

m = re.search(r"serial=(\d+) initrd=(\d+) net=(\d+) desk=(\d+) fault=(\d+) lines=(\d+)", smoke)
smoke_vals = dict(serial=m.group(1), initrd=m.group(2), net=m.group(3),
                  desk=m.group(4), fault=m.group(5), lines=m.group(6)) if m else {}
ktest = re.search(r"ktest_result=(\S+) fail_suites=(\d+) fail_cases=(\d+)", smoke)
ktest_vals = dict(result=ktest.group(1), fail_suites=int(ktest.group(2)),
                  fail_cases=int(ktest.group(3))) if ktest else {}

host_pass = len(re.findall(r"\[ok\]", host))
host_fail = len(re.findall(r"\[FAIL\]", host))

result = {
    "host_tests": {"exit": int(host_rc), "ok": host_pass, "fail": host_fail, "summary": host_summary},
    "gfx_boundary": {"exit": int(bound_rc)},
    "smoke32": {"exit": int(smoke_rc), **smoke_vals, "kunit": ktest_vals},
}
with open(os.path.join(td, "results.json"), "w") as f:
    json.dump(result, f, ensure_ascii=False, indent=2)
print(json.dumps(result, ensure_ascii=False, indent=2))
PY

echo "run_tests done: host_rc=$HOST_RC bound_rc=$BOUND_RC smoke_rc=$SMOKE_RC"
