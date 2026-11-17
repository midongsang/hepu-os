#!/bin/bash
# ============================================================================
# 32 位保护模式：multiboot 直启 kernel.elf，cmdline hepu.graphics=0
# -> [PROTECT] arch=i386，文本终端 help 有输出；不进图形桌面。
# 用法: bash tools/regress/protect32.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

OUT="$EV/protect32"
SER="$OUT/ser.log"
INST32="$RUN/inst/regress32"
MON="$INST32/mon.sock"
PID="$INST32/qemu.pid"
mkdir -p "$OUT" "$INST32"
rm -f "$SER" "$MON" "$PID"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

if [ -f "$PID" ]; then OLD=$(cat "$PID" 2>/dev/null); [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null; fi

# 32 内核键盘驱动仅 PS/2 8042 (IRQ1)：不挂 usb-kbd，避免 sendkey 注入到 USB 键盘
qemu-system-i386 -L "$CHAT/.toolchain/pkg/datadir" \
  -kernel "$WT/build_v250/kernel.elf" -append "hepu.graphics=0" \
  -m 256 -smp 1 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait -serial file:$SER -no-reboot \
  -daemonize -pidfile "$PID" 2>&1 | head -2

PROT=0
for i in $(seq 1 60); do
  sleep 1
  grep -aqE "\[PROTECT\].*i386" "$SER" 2>/dev/null && { PROT=1; break; }
done
echo "PROT=$PROT after ${i}s"

PR=0
for i in $(seq 1 30); do
  sleep 1
  grep -aq "hepu@os:~" "$SER" 2>/dev/null && { PR=1; break; }
done
echo "PROMPT=$PR after ${i}s"

# shell 自检等待键盘就绪（"self-test: waiting 3s"），晚发按键避免被自检吞掉
sleep 6
python3 - "$MON" <<'PY'
import socket,time,sys
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.3)
def k(x): s.sendall(("sendkey %s\n"%x).encode()); time.sleep(0.15)
for ch in "help": k(ch)
k("ret"); time.sleep(5)
for ch in "ls": k(ch)
k("ret"); time.sleep(4)
s.close()
PY

sleep 1
python3 - "$MON" "$OUT/protect32_text.ppm" "$OUT/protect32_text.png" <<'PY'
import socket,sys,time
from PIL import Image
mon,ppm,png=sys.argv[1],sys.argv[2],sys.argv[3]
s=socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.3)
s.sendall(("screendump %s\n"%ppm).encode()); time.sleep(2.5)
try: s.recv(4096)
except Exception: pass
s.close()
im=Image.open(ppm).convert("RGB"); im.save(png)
print("W,H =", im.size, "NONBLACK =", sum(1 for p in im.getdata() if p!=(0,0,0)))
PY

if [ -f "$PID" ]; then P=$(cat "$PID" 2>/dev/null); [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null; fi
cp -f "$SER" "$OUT/ser.log" 2>/dev/null || true

HELP=0; grep -aq "HEPU OS Help\|help <cmd>" "$SER" 2>/dev/null && HELP=1
{
  echo "PROTECT=$PROT"
  echo "PROMPT=$PR"
  echo "HELP_ECHO=$HELP"
  grep -E "\[PROTECT\]|elastic free|DESKTOP" "$SER" | sort | uniq -c
} > "$OUT/result.txt" 2>/dev/null || true
log "protect32 done -> $OUT"
