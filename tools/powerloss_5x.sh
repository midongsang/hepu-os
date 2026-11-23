#!/bin/bash
# powerloss_5x.sh — P1 验收：连续 5 次写入中强杀 QEMU
# 每次：新盘 → cache=unsafe → 第一个 chunk 后 2s kill → e2fsck -n/-y + pl.dat。
# 判定：5 次都必须 STARTED=1、DONE=0、e2fsck -y 退出 0 或 1、pl.dat 存在。
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
RUNS=${RUNS:-5}
OUT=/tmp/hepu_powerloss_5x
rm -rf "$OUT"; mkdir -p "$OUT"

pass=0
for i in $(seq 1 "$RUNS"); do
    echo "===== RUN $i/$RUNS ====="
    bash "$ROOT/tools/powerloss_test.sh" > "$OUT/run_$i.log" 2>&1
    cp /tmp/pl_fsck_ro.txt "$OUT/fsck_ro_$i.txt" 2>/dev/null || true
    cp /tmp/pl_fsck_fix.txt "$OUT/fsck_fix_$i.txt" 2>/dev/null || true
    started=$(grep -c "guest_started=1" "$OUT/run_$i.log" || true)
    finished=$(grep -c "workload_finished=1" "$OUT/run_$i.log" || true)
    fixrc=$(sed -n 's/.*e2fsck -y exit=\([0-9]\+\).*/\1/p' "$OUT/run_$i.log" | tail -1)
    pl=$(grep -c "pl.dat" "$OUT/run_$i.log" || true)
    chunks=$(grep -o "chunks_written=[0-9]*" "$OUT/run_$i.log" | tail -1 | tr -dc '0-9')
    echo "  started=$started finished=$finished chunks=$chunks fsck_y_exit=${fixrc:-?} pl.dat=$pl"
    grep -aE "Block bitmap differences|Inode bitmap differences|Free blocks count wrong|Inode bitmap|deleted/unused" "$OUT/run_$i.log" | head -5 | sed 's/^/  /'
    if [ "$started" -ge 1 ] && [ "$finished" -eq 0 ] && \
       { [ "${fixrc:-9}" -eq 0 ] || [ "${fixrc:-9}" -eq 1 ]; } && [ "$pl" -ge 1 ]; then
        pass=$((pass + 1))
    else
        echo "  ✗ 本次不满足验收"
    fi
done

echo
echo "POWERLOSS 5X: $pass/$RUNS 通过"
if [ "$pass" -eq "$RUNS" ]; then
    echo "POWERLOSS-5X: PASS"
    exit 0
fi
echo "POWERLOSS-5X: FAIL"
exit 1
