#!/bin/bash
set -u
source tools/regress/env.sh
source tools/regress/lib_qemu.sh
OUT="${1:-/tmp/desktop-batch4}"
rm -rf "$OUT"; mkdir -p "$OUT"
INST="$RUN/inst/desktop-batch4"
mkdir -p "$INST"
VNCPORT=5907
SER="$OUT/ser.log"
exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || exit 1
cleanup(){ q_stop; }
trap cleanup EXIT
q_start "$SER"
q_wait_for "booting to lock screen" 90 "$SER" || { echo FAIL_BOOT; exit 3; }
sleep 3
q_sendkey ret
q_wait_for "lock -> login" 30 "$SER" || { echo FAIL_LOGIN; exit 3; }
sleep 2
q_send_text "$FIXTURE_PASSWORD"
sleep 1
q_sendkey ret
q_wait_for "login -> desktop" 40 "$SER" || { echo FAIL_UNLOCK; exit 3; }
sleep 18
q_dump "$OUT/01-desktop.ppm" "$OUT/01-desktop.png" || exit 3

q_sendkey l
q_wait_for "snapshot frozen" 20 "$SER" || { echo FAIL_SNAPSHOT; exit 3; }
sleep 2
q_dump "$OUT/02-lock-snapshot.ppm" "$OUT/02-lock-snapshot.png" || exit 3
q_sendkey ret
sleep 0.10
python3 - "$INST/mon.sock" "$OUT/03-reveal-early.ppm" "$OUT/03-reveal-early.png" <<'PY'
import socket,sys,time
from PIL import Image
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.05)
s.sendall(("screendump %s\n"%sys.argv[2]).encode()); time.sleep(0.4); s.close()
im=Image.open(sys.argv[2]).convert('RGB'); im.save(sys.argv[3]); print('EARLY', im.size)
PY
for i in $(seq 1 20); do c=$(grep -ac "reveal done" "$SER" 2>/dev/null || true); [ "${c:-0}" -ge 2 ] && break; sleep 1; done
[ "${c:-0}" -ge 2 ] || { echo FAIL_REVEAL_DONE; exit 3; }
q_dump "$OUT/04-login-final.ppm" "$OUT/04-login-final.png" || exit 3
q_send_text "$FIXTURE_PASSWORD"
sleep 1
q_sendkey ret
for i in $(seq 1 30); do c=$(grep -ac "fade mid" "$SER" 2>/dev/null || true); [ "${c:-0}" -ge 2 ] && break; sleep 1; done
[ "${c:-0}" -ge 2 ] || { echo FAIL_FADE_MID; exit 3; }
q_dump "$OUT/05-unlock-fade.ppm" "$OUT/05-unlock-fade.png" || exit 3
for i in $(seq 1 30); do c=$(grep -ac "unlock fade done" "$SER" 2>/dev/null || true); [ "${c:-0}" -ge 2 ] && break; sleep 1; done
[ "${c:-0}" -ge 2 ] || { echo FAIL_FADE_DONE; exit 3; }
sleep 2
q_dump "$OUT/06-desktop-after.ppm" "$OUT/06-desktop-after.png" || exit 3

q_sendkey f11
q_wait_for "create Files" 12 "$SER" || { echo FAIL_FILES; exit 3; }
sleep 2
q_sendkey f12
q_wait_for "create Terminal" 12 "$SER" || { echo FAIL_TERM; exit 3; }
sleep 3
q_dump "$OUT/07-three-windows.ppm" "$OUT/07-three-windows.png" || exit 3
q_sendkey f9
q_wait_for "layout=Columns" 15 "$SER" || { echo FAIL_COLUMNS; exit 3; }
sleep 8
q_dump "$OUT/08-columns-3of3.ppm" "$OUT/08-columns-3of3.png" || exit 3
q_sendkey f10
sleep 9
q_dump "$OUT/09-columns-1of3.ppm" "$OUT/09-columns-1of3.png" || exit 3
q_sendkey f10
sleep 9
q_dump "$OUT/10-columns-2of3.ppm" "$OUT/10-columns-2of3.png" || exit 3
q_sendkey f10
sleep 9
q_dump "$OUT/11-columns-3of3.ppm" "$OUT/11-columns-3of3.png" || exit 3
q_sendkey f9
sleep 3
q_dump "$OUT/12-tile-back.ppm" "$OUT/12-tile-back.png" || exit 3
grep -aE "\[LOCK\]|\[WM\] columns|\[WM\] layout|\[WM\] columns scroll" "$SER" > "$OUT/key-lines.txt"
cat "$OUT/key-lines.txt"
