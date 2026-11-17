#!/bin/bash
# ============================================================================
# 64 位保护模式：UEFI 菜单选 "3. Protection Mode" -> [PROTECT] arch=x86_64
# -> 文本终端可收发 help / ls / ktest；进不去或无回显如实记失败。
# 用法: bash tools/regress/protect64.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="$EV/protect64"
SER="$OUT/ser.log"
mkdir -p "$OUT"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

q_start "$SER"
# 菜单起来后连发 3（UEFI ConIn 采样窗口，单发易丢）
sleep 7
python3 - "$INST/mon.sock" <<'PY'
import socket,time,sys
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.3)
for _ in range(3):
    s.sendall(b"sendkey 3\n"); time.sleep(1.0)
s.close()
PY

PROT=0
if q_wait_for "\[PROTECT\].*x86_64" 60 "$SER"; then PROT=1; log "PROTECT x86_64 hit"; else log "WARN: [PROTECT] x86_64 未命中"; fi

PR=0
if q_wait_for "hepu@os:~" 30 "$SER"; then PR=1; log "prompt hit"; else log "WARN: 提示符未出现"; fi

sleep 2
# 仅在保护终端下发 help/ls/ktest；未进保护模式时不向桌面状态机灌键
if [ "$PROT" = "1" ]; then
python3 - "$INST/mon.sock" <<'PY'
import socket,time,sys
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.3)
def k(x): s.sendall(("sendkey %s\n"%x).encode()); time.sleep(0.15)
for ch in "help": k(ch)
k("ret"); time.sleep(3)
for ch in "ls": k(ch)
k("ret"); time.sleep(4)
# 64 位 KUnit 全量套件（shell 命令 ktest）
for ch in "ktest": k(ch)
k("ret"); time.sleep(20)
s.close()
PY
fi

q_dump "$OUT/protect64_text.ppm" "$OUT/protect64_text.png"
q_stop
cp -f "$SER" "$OUT/ser.log" 2>/dev/null || true

{
  echo "PROTECT=$PROT"
  echo "PROMPT=$PR"
  grep -E "\[PROTECT\]|Mode:|DESKTOP64|hpdesk|Desktop initialized" "$SER" | sort | uniq -c
  echo "--- ktest result ---"
  grep -E "\[KTEST\]|KUNIT-FAIL" "$SER" | tail -20
} > "$OUT/result.txt" 2>/dev/null || true
log "protect64 done -> $OUT"
