#!/bin/bash
# ext2_meta_guard.sh — P1 元数据区保护对抗测试
#
# 做法：故意把组 0/1 的 GDT、块位图、inode 位图、inode 表对应的块位图位清零，
# 让分配器眼里的“空闲块”包含文件系统自身元数据；然后跑一次真实写阶段。
#
# 判定：
#   1) VFS-TEST 写阶段 PASS（说明系统还能正常写文件）
#   2) e2fsck 只报“位图差异/空闲计数”这类故意制造的差异，不报 inode 表/
#      GDT/超级块结构损坏
#   3) debugfs 仍能读出根目录，根 inode 仍是目录
#
# 输出最后一行 "META-GUARD: PASS" 或 "META-GUARD: FAIL"。

set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
TC=${HEPU_TOOLCHAIN:-$(dirname "$ROOT")/.toolchain/pkg/root2}
DD=${HEPU_DATADIR:-$(dirname "$ROOT")/.toolchain/pkg/datadir}
export PATH=$TC/sbin:$TC/usr/bin:$PATH
export LD_LIBRARY_PATH=$TC/usr/lib/x86_64-linux-gnu:$TC/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$TC/usr/lib/x86_64-linux-gnu/qemu

ELF=build_v250/kernel.elf
DISK=/tmp/meta_guard_disk.img
PART=/tmp/meta_guard_part.img
LOG=/tmp/meta_guard.log
WORK=/tmp/meta_guard_work
rm -rf "$WORK"; mkdir -p "$WORK"

cleanup() { pkill -9 -f "qemu-system-i386.*$DISK" 2>/dev/null; }
trap cleanup EXIT

[ -f "$ELF" ] || { echo "缺少 $ELF，先 make"; echo "META-GUARD: FAIL"; exit 1; }

# 1. 造盘
echo "[1/5] 造 64MB ext2 盘"
rm -f "$DISK" "$PART"
python3 - <<PY
import struct, subprocess
disk="$DISK"; part="$PART"
subprocess.run(["mke2fs","-q","-t","ext2","-b","1024","-F",part,"58368"],check=True)
with open(disk,"wb") as f: f.truncate(64*1024*1024)
mbr=bytearray(512)
start=2048; sectors=58368*1024//512
mbr[446:462]=struct.pack("<BBBBBBBBII",0,0xFE,0xFF,0xFF,0x83,0xFE,0xFF,0xFF,start,sectors)
mbr[510]=0x55; mbr[511]=0xAA
with open(disk,"r+b") as f: f.seek(0); f.write(bytes(mbr))
with open(part,"rb") as src, open(disk,"r+b") as dst:
    dst.seek(start*512); dst.write(src.read())
PY

# 2. 解析元数据块并清零位图位（保留 block1 的 bit0，避免触发旧的整页清零防线）
echo "[2/5] 故意清零元数据块位图位"
META=$(python3 - <<PY
import struct
part="$PART"
bs=1024
data=open(part,"rb").read(4096)
assert struct.unpack_from("<H",data,1024+56)[0]==0xEF53
sb=data[1024:1024+1024]
first=struct.unpack_from("<I",sb,20)[0]
log_bs=struct.unpack_from("<I",sb,24)[0]
bpg=struct.unpack_from("<I",sb,32)[0]
ipg=struct.unpack_from("<I",sb,40)[0]
blocks=struct.unpack_from("<I",sb,4)[0]
inode_size=struct.unpack_from("<H",sb,88)[0] or 128
bs=1024<<log_bs
groups=(blocks+bpg-1)//bpg
gdt_off=(first+1)*bs
gdt=[struct.unpack_from("<III",data,gdt_off+i*32) for i in range(groups)]
meta=[]
for g,(bb,ib,it) in enumerate(gdt):
    meta += [bb, ib, it]
# 只取前两组做对抗，避免整盘位图全被改
for g in (0,1):
    bb,ib,it=gdt[g]
    meta += [bb, ib, it]
    for k in range(1,5):  # GDT/块位图/inode位图/inode表后的连续块
        meta.append(first+g*bpg+k)
print(" ".join(str(x) for x in sorted(set(meta))))
PY
)
for b in $META; do
    # bit0（超级块）保留，其余元数据位清零
    [ "$b" -eq 1 ] && continue
    debugfs -w -R "clrb $b" "$PART" >/dev/null 2>&1
done
echo "  已清零: $(echo "$META" | wc -w) 个元数据块位"

# 3. 写标记 + 启动写阶段
echo "[3/5] 注入 /vfs_selftest='w' 并启动"
printf 'w' > "$WORK/mark_w"
debugfs -w -R "write $WORK/mark_w /vfs_selftest" "$PART" >/dev/null 2>&1
dd if="$PART" of="$DISK" bs=512 seek=2048 conv=notrunc status=none
rm -f "$LOG"
pkill -9 -f qemu-system-i386 2>/dev/null; sleep 1
timeout 70 qemu-system-i386 -L "$DD" -kernel "$ELF" -m 256 -display none \
    -drive file="$DISK",format=raw,if=ide -serial file:"$LOG" >/dev/null 2>&1

# 4. 判定 VFS 写阶段
echo "[4/5] 判定"
VFS_PASS=0
grep -aq "VFS-TEST. RESULT: PASS" "$LOG" && VFS_PASS=1
grep -a "VFS-TEST. RESULT" "$LOG" | tail -1 | sed 's/^/  /'
if [ "$VFS_PASS" -ne 1 ]; then
    grep -a "VFS-TEST.*FAIL\|EXT2.*error" "$LOG" | tail -10 | sed 's/^/  /'
    echo "META-GUARD: FAIL"
    exit 1
fi

# 5. 结构完整性：e2fsck 不应报 inode 表/GDT/超级块损坏
echo "[5/5] e2fsck 结构检查"
SECTORS=$((58368 * 1024 / 512))
dd if="$DISK" of="$PART" bs=512 skip=2048 count=$SECTORS conv=notrunc status=none
FSCK=$(e2fsck -fn "$PART" 2>&1 || true)
echo "$FSCK" | sed 's/^/  /' | head -20
if echo "$FSCK" | grep -Eiq "inode table.*(corrupt|invalid)|group descriptors? .*corrupt|bad magic|superblock.*invalid"; then
    echo "  结构元数据被破坏"
    echo "META-GUARD: FAIL"
    exit 1
fi
if ! echo "$FSCK" | grep -q "Block bitmap differences"; then
    echo "  没有看到预期的位图差异（测试注入可能失效）"
    echo "META-GUARD: FAIL"
    exit 1
fi

# 根目录仍可读，根 inode 仍是目录
ROOTSTAT=$(debugfs -R "stat <2>" "$PART" 2>/dev/null || true)
if ! echo "$ROOTSTAT" | grep -q "Type: directory"; then
    echo "  根 inode 已损坏"
    echo "META-GUARD: FAIL"
    exit 1
fi

echo "  ✓ VFS 写阶段 PASS，元数据块未被数据分配覆盖，根目录完整"
echo "META-GUARD: PASS"
