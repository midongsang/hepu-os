#!/usr/bin/env python3
"""
HepuOS 双内核 UEFI+GPT 镜像构建脚本
使用 pyfatfs 创建 FAT32 ESP 分区
"""
import struct
import os
import sys
import subprocess
import zlib
import uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fat32_generator import create_esp_partition

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# 镜像输出路径可用环境变量覆盖（/tmp 会被周期性清理，构建/回归可指到项目目录下）
IMG_PATH = os.environ.get("HEPU_IMG", "/tmp/hepu_uefi_dual.img")
IMG_SIZE = 256 * 1024 * 1024  # 256MB

# 分区布局
ESP_START = 2048          # 1MiB
ESP_SIZE = 128 * 1024 * 1024  # 128MB
ESP_SECTORS = ESP_SIZE // 512
DATA_START = ESP_START + ESP_SECTORS
DATA_SIZE = 120 * 1024 * 1024  # 120MB
DATA_SECTORS = DATA_SIZE // 512

def create_gpt_partition_table(img):
    """创建GPT分区表"""
    # Protective MBR (LBA 0)
    mbr = bytearray(512)
    mbr[446] = 0x00  # boot flag
    mbr[447] = 0x00  # start CHS
    mbr[448] = 0x02
    mbr[449] = 0x00
    mbr[450] = 0xEE  # GPT protective
    mbr[451] = 0xFF  # end CHS
    mbr[452] = 0xFF
    mbr[453] = 0xFF
    struct.pack_into('<I', mbr, 454, 1)  # start LBA
    struct.pack_into('<I', mbr, 458, (IMG_SIZE // 512) - 1)  # size
    mbr[510] = 0x55
    mbr[511] = 0xAA
    img.seek(0)
    img.write(mbr)

    # GPT Header (LBA 1)
    gpt_header = bytearray(512)
    gpt_header[0:8] = b'EFI PART'  # signature
    struct.pack_into('<I', gpt_header, 8, 0x00010000)  # revision
    struct.pack_into('<I', gpt_header, 12, 92)  # header size
    # CRC32 placeholder (calculate later)
    struct.pack_into('<Q', gpt_header, 24, 1)  # current LBA
    struct.pack_into('<Q', gpt_header, 32, (IMG_SIZE // 512) - 1)  # backup LBA
    struct.pack_into('<Q', gpt_header, 40, 34)  # first usable LBA
    struct.pack_into('<Q', gpt_header, 48, (IMG_SIZE // 512) - 34)  # last usable LBA
    # GUID (random)
    import uuid
    disk_guid = uuid.uuid4().bytes_le
    gpt_header[56:72] = disk_guid
    struct.pack_into('<Q', gpt_header, 72, 2)  # partition entry LBA
    struct.pack_into('<I', gpt_header, 80, 128)  # number of partition entries
    struct.pack_into('<I', gpt_header, 84, 128)  # size of partition entry

    # Calculate CRC32
    import zlib
    crc = zlib.crc32(bytes(gpt_header[:92])) & 0xFFFFFFFF
    struct.pack_into('<I', gpt_header, 16, crc)

    img.seek(512)
    img.write(gpt_header)

    # Partition entries (LBA 2-33)
    part_entries = bytearray(128 * 128)  # 128 entries * 128 bytes

    # Partition 1: ESP (FAT32)
    esp_guid = b'\x28\x73\x2A\xC1\x1F\xF8\xD2\x11\xBA\x4B\x00\xA0\xC9\x3E\xC9\x3B'
    part_entries[0:16] = esp_guid
    part_entries[16:32] = uuid.uuid4().bytes_le  # unique GUID
    struct.pack_into('<Q', part_entries, 32, ESP_START)
    struct.pack_into('<Q', part_entries, 40, ESP_START + ESP_SECTORS - 1)
    struct.pack_into('<Q', part_entries, 48, 0x7)  # attributes: ESP
    name = 'EFI System Partition'
    part_entries[56:56+len(name)*2] = name.encode('utf-16-le')

    # Partition 2: DATA (ext2)
    data_guid = b'\xA2\xA0\xD0\xEB\xE5\xB9\x33\x44\x87\xC0\x68\xB6\xB7\x26\x99\xC7'
    part_entries[128:144] = data_guid
    part_entries[144:160] = uuid.uuid4().bytes_le
    struct.pack_into('<Q', part_entries, 160, DATA_START)
    struct.pack_into('<Q', part_entries, 168, DATA_START + DATA_SECTORS - 1)
    name2 = 'HepuOS Root'
    part_entries[184:184+len(name2)*2] = name2.encode('utf-16-le')

    img.seek(1024)
    img.write(part_entries)

    # 关键: 分区数组写完后必须回填 entries CRC, 再重算 header CRC。
    # 原实现先算 header CRC 再写数组, 且 entries CRC(偏移 88) 一直是 0 ——
    # OVMF 校验失配会直接丢弃整个 GPT, 表现就是 "Boot ... Not Found" 然后掉 PXE。
    ent_crc = zlib.crc32(bytes(part_entries)) & 0xFFFFFFFF
    struct.pack_into('<I', gpt_header, 88, ent_crc)
    struct.pack_into('<I', gpt_header, 16, 0)   # header CRC 计算时该字段必须按 0 算
    hdr_crc = zlib.crc32(bytes(gpt_header[:92])) & 0xFFFFFFFF
    struct.pack_into('<I', gpt_header, 16, hdr_crc)
    img.seek(512)
    img.write(gpt_header)

    # 备份 GPT: 标准 GPT 必须在磁盘末尾保留一份 header + entries。
    # 只写主表时, 严格实现(OVMF)会在校验/切换时判分区表不可信 —— 表现为 ESP 里的
    # BOOTX64.EFI 明明存在, 固件却报 "Boot ... Not Found" 然后掉 PXE。
    last_lba = (IMG_SIZE // 512) - 1
    img.seek((last_lba - 32) * 512)
    img.write(part_entries)

    bkp = bytearray(gpt_header)
    struct.pack_into('<Q', bkp, 24, last_lba)        # MyLBA
    struct.pack_into('<Q', bkp, 32, 1)               # AlternateLBA
    struct.pack_into('<Q', bkp, 72, last_lba - 32)   # PartitionEntryLBA
    struct.pack_into('<I', bkp, 16, 0)
    bcrc = zlib.crc32(bytes(bkp[:92])) & 0xFFFFFFFF
    struct.pack_into('<I', bkp, 16, bcrc)
    img.seek(last_lba * 512)
    img.write(bkp)

    print(f"[GPT] 分区表创建完成 (entries CRC={ent_crc:08x}, header CRC={hdr_crc:08x}, backup@{last_lba})")
    print(f"  ESP: LBA {ESP_START}-{ESP_START+ESP_SECTORS-1} ({ESP_SIZE//1024//1024}MB)")
    print(f"  DATA: LBA {DATA_START}-{DATA_START+DATA_SECTORS-1} ({DATA_SIZE//1024//1024}MB)")

def create_fat32_esp(img):
    """创建FAT32 ESP分区并写入文件"""
    print("[FAT32] 创建ESP分区...")

    # 准备文件
    files = {}

    # BOOTX64.EFI（UEFI规范要求在/EFI/BOOT/目录）
    with open(f"{PROJECT}/tools/hepboot/uefi/hepboot_uefi.efi", 'rb') as f:
        files['/EFI/BOOT/BOOTX64.EFI'] = f.read()
    print(f"  /EFI/BOOT/BOOTX64.EFI ({len(files['/EFI/BOOT/BOOTX64.EFI'])} bytes)")

    # 64位内核 —— 必须放 ESP 根目录: 引导器读的是 \\KERNEL64.BIN
    with open(f"{PROJECT}/build_x86_64/hepu-kernel64.bin", 'rb') as f:
        files['/KERNEL64.BIN'] = f.read()
    print(f"  KERNEL64.BIN ({len(files['/KERNEL64.BIN'])} bytes)")

    # 32位内核(可选, 当前主线不产出该文件)
    k32 = f"{PROJECT}/build_v250/hepu-kernel.bin"
    if os.path.exists(k32):
        with open(k32, 'rb') as f:
            files['/KERNEL32.BIN'] = f.read()
        print(f"  KERNEL32.BIN ({len(files['/KERNEL32.BIN'])} bytes)")
    else:
        print("  KERNEL32.BIN: 跳过(无 build_v250/hepu-kernel.bin)")

    # 配置文件 —— 同样在根目录
    files['/HEPBOOT.CFG'] = b"# HepuBoot UEFI Configuration v0.57.0\ntimeout=5\ndefault=0\nkernel_arch=64\ncmdline=\n"

    # 生成FAT32分区
    esp_data = create_esp_partition(ESP_SIZE, files)

    # 写入主镜像
    img.seek(ESP_START * 512)
    img.write(esp_data)

    print("[FAT32] ESP分区创建完成")

def create_ext2_data(img):
    """创建ext2根分区"""
    print("[EXT2] 创建根分区...")

    ext2_tmp = "/tmp/ext2_partition.img"
    subprocess.run(['mkfs.ext2', '-F', '-b', '4096', ext2_tmp,
                    str(DATA_SIZE // 4096)],
                   capture_output=True)

    # 写入字体文件
    font_path = f"{PROJECT}/kernel/fonts/DejaVuSans.ttf"
    if os.path.exists(font_path):
        subprocess.run(['debugfs', '-w', ext2_tmp,
                        '-R', 'mkdir /fonts'], capture_output=True)
        subprocess.run(['debugfs', '-w', ext2_tmp,
                        '-R', f'write {font_path} /fonts/DejaVuSans.ttf'],
                       capture_output=True)
        print("  写入 /fonts/DejaVuSans.ttf")

    # 壁纸: 桌面 hs_wallpaper_png() 读 /usr/share/wallpapers/wallpaper.png
    # (旧注释写 /wallpaper.png 与代码不一致, 导致桌面一直 [PNG64] no sample image)
    wp = f"{PROJECT}/assets/samples/wallpaper.png"
    if os.path.exists(wp):
        for d in ('/usr', '/usr/share', '/usr/share/wallpapers'):
            subprocess.run(['debugfs', '-w', ext2_tmp, '-R', f'mkdir {d}'],
                           capture_output=True)
        subprocess.run(['debugfs', '-w', ext2_tmp,
                        '-R', f'write {wp} /usr/share/wallpapers/wallpaper.png'],
                       capture_output=True)
        subprocess.run(['debugfs', '-w', ext2_tmp,
                        '-R', f'write {wp} /wallpaper.png'], capture_output=True)
        print("  写入 /usr/share/wallpapers/wallpaper.png + /wallpaper.png")

    # 光标: hpdesk 读 /usr/share/cursors/default.xcur (cursor64.c 解析 Xcursor)
    # 旧脚本完全没有打包 assets/cursors, 导致运行时 ext2_64_read_file 失败、
    # 静默回退到 gfx64 内置位图 —— 表现就是"用户给的鼠标包没加载"。
    cdir = f"{PROJECT}/assets/cursors"
    if os.path.isdir(cdir):
        for d in ('/usr', '/usr/share', '/usr/share/cursors'):
            subprocess.run(['debugfs', '-w', ext2_tmp, '-R', f'mkdir {d}'],
                           capture_output=True)
            subprocess.run(['debugfs', '-w', ext2_tmp, '-R', f'mkdir {d}'],
                           capture_output=True)
        ncur = 0
        for f in sorted(os.listdir(cdir)):
            if not f.endswith('.xcur'):
                continue
            subprocess.run(['debugfs', '-w', ext2_tmp,
                            '-R', f'write {cdir}/{f} /usr/share/cursors/{f}'],
                           capture_output=True)
            ncur += 1
        print(f"  写入 /usr/share/cursors/*.xcur ({ncur} 个)")

    # 用户态程序: 内核用绝对路径 /bin/xxx.elf 去 exec，而旧脚本从没打包过 /bin
    # —— 结果就是 autostart 与自检电池全部 "not found"，系统里一个用户进程都没有。
    # 这里把三处产物统一扁平化写入 /bin（内核只认 /bin/<name>.elf）。
    elf_dirs = [
        f"{PROJECT}/user/build",
        f"{PROJECT}/user/build/legacy",
        f"{PROJECT}/user/apps",
    ]
    subprocess.run(['debugfs', '-w', ext2_tmp, '-R', 'mkdir /bin'],
                   capture_output=True)
    nel = 0
    for d in elf_dirs:
        if not os.path.isdir(d):
            continue
        for root, _dirs, files in os.walk(d):
            for f in sorted(files):
                if not f.endswith('.elf'):
                    continue
                dst = f'/bin/{f}'
                subprocess.run(['debugfs', '-w', ext2_tmp,
                                '-R', f'write {os.path.join(root, f)} {dst}'],
                               capture_output=True)
                nel += 1
    print(f"  写入 /bin/*.elf ({nel} 个)")

    # 将ext2分区写入主镜像
    with open(ext2_tmp, 'rb') as f:
        ext2_data = f.read()
    img.seek(DATA_START * 512)
    img.write(ext2_data)

    os.remove(ext2_tmp)
    print("[EXT2] 根分区创建完成")
    # hybrid MBR 统一在 main() 收尾由 _write_protective_mbr 写入


def create_gpt_via_sgdisk(img_path):
    """标准 GPT: 用 sgdisk 生成(主表+备份表+CRC 都由工具保证)。
    自研 create_gpt_partition_table() 保留作教学/研究, 但它产出的表 OVMF 不认
    (见 docs/status/FIXES_0_57_0.md §1.31), 构建期以本实现为准。"""
    print("[GPT] 使用 sgdisk 生成标准分区表...")
    subprocess.run(['sgdisk', '--clear', img_path], capture_output=True)
    subprocess.run(['sgdisk',
                    '-n', f'1:{ESP_START}:{ESP_START + ESP_SECTORS - 1}',
                    '-t', '1:ef00', '-c', '1:EFI System Partition', img_path], capture_output=True)
    subprocess.run(['sgdisk',
                    '-n', f'2:{DATA_START}:{DATA_START + DATA_SECTORS - 1}',
                    '-t', '2:8300', '-c', '2:HepuOS Root', img_path], capture_output=True)
    print(f"[GPT] ESP LBA {ESP_START}-{ESP_START+ESP_SECTORS-1}, "
          f"DATA LBA {DATA_START}-{DATA_START+DATA_SECTORS-1}")


def create_fat32_esp_tool(img_path):
    """标准 FAT32 ESP: mkfs.vfat 造卷 + mtools 写文件。
    自研 FAT32Generator 的 FSInfo/簇链细节不足以让 OVMF 识别, 构建期以本实现为准。"""
    print("[FAT32] 使用 mkfs.vfat + mcopy 生成 ESP...")
    esp_tmp = "/tmp/hepu_esp_build.img"
    files = {}
    with open(f"{PROJECT}/tools/hepboot/uefi/hepboot_uefi.efi", 'rb') as f:
        files['EFI/BOOT/BOOTX64.EFI'] = f.read()
    with open(f"{PROJECT}/build_x86_64/hepu-kernel64.bin", 'rb') as f:
        files['KERNEL64.BIN'] = f.read()
    k32 = f"{PROJECT}/build_v250/hepu-kernel.bin"
    if os.path.exists(k32):
        with open(k32, 'rb') as f:
            files['KERNEL32.BIN'] = f.read()
    files['HEPBOOT.CFG'] = (b"# HepuBoot UEFI Configuration v0.57.0\n"
                            b"timeout=5\ndefault=0\nkernel_arch=64\n"
                            b"resolution=1920x1080\ncmdline=\n")

    subprocess.run(['dd', 'if=/dev/zero', f'of={esp_tmp}', 'bs=512',
                    f'count={ESP_SECTORS}', 'status=none'], check=True)
    subprocess.run(['mkfs.vfat', '-F', '32', '-s', '1', '-n', 'HEPU_ESP', esp_tmp],
                   capture_output=True, check=True)
    env = dict(os.environ, MTOOLS_SKIP_CHECK='1')
    subprocess.run(['mmd', '-i', esp_tmp, '::/EFI', '::/EFI/BOOT'], env=env, capture_output=True)
    for name, data in files.items():
        tmp = '/tmp/' + name.replace('/', '_')
        with open(tmp, 'wb') as f:
            f.write(data)
        subprocess.run(['mcopy', '-i', esp_tmp, tmp, f'::{name}'], env=env, check=True)
        print(f"  {name} ({len(data)} bytes)")

    with open(esp_tmp, 'rb') as f:
        esp_data = f.read()
    with open(img_path, 'r+b') as img:
        img.seek(ESP_START * 512)
        img.write(esp_data)
    os.remove(esp_tmp)
    print("[FAT32] ESP分区创建完成")




def write_hybrid_mbr(f, root_lba, root_sectors):
    """在保护性 MBR 里补一个 0x83 分区块, 指向 ext2 根分区。
    UEFI 固件读 GPT, 不看 MBR; 而内核 IDE 扫描只认 MBR 的 0x83 项,
    GPT 盘原本只有 0xEE 保护项 -> 内核拿不到根分区偏移, 会把 GPT 头当超级块。
    这是让两边各取所需的桥。

    V0.58.5: 改成复用调用方的句柄 —— 之前这里自己 open() 一次,
    与外层 create_ext2_data 的句柄缓冲交错, 最后落盘的 LBA 0 变成全 0。
    """
    f.seek(446)
    mbr = bytearray(f.read(66))
    if len(mbr) < 66:
        mbr = bytearray(66)
    e = bytearray(16)
    e[0] = 0x00; e[4] = 0x83
    e[8:12] = root_lba.to_bytes(4, 'little')
    e[12:16] = root_sectors.to_bytes(4, 'little')
    mbr[16:32] = e
    f.seek(446)
    f.write(mbr)
    f.seek(510); f.write(b'\x55\xaa')
    print('  [MBR] hybrid 0x83 -> LBA %d (%d sectors)' % (root_lba, root_sectors))


def _write_protective_mbr(img_path, root_lba, root_sectors):
    """构建收尾: 用独立描述符重写 LBA 0 的 446..512 区段。

    - 槽 0 必须是类型 0xEE 的保护项(GPT 规范);
    - 槽 1 写 hybrid 0x83 指向 ext2 根分区(本机内核只解析 MBR 分区项);
    - 0x55AA 结尾。

    V0.58.5: 这个函数必须在所有其它写操作关闭句柄之后再调用, 并且只写
    446..512 这 66 字节 —— 之前用共享句柄整块重写 512B, 与外层 120MB 写入
    的缓冲交错, 最终落盘的 LBA 0 要么全 0、要么 hybrid 槽被冲掉。
    """
    total = IMG_SIZE // 512
    e0 = bytearray(16)
    e0[0] = 0x00
    e0[1] = 0x00; e0[2] = 0x02; e0[3] = 0x00     # start CHS
    e0[4] = 0xEE
    e0[5] = 0xFF; e0[6] = 0xFF; e0[7] = 0xFF     # end CHS
    struct.pack_into('<I', e0, 8, 1)
    struct.pack_into('<I', e0, 12, min(total - 1, 0xFFFFFFFF))

    e1 = bytearray(16)
    e1[0] = 0x00
    e1[4] = 0x83
    struct.pack_into('<I', e1, 8, root_lba)
    struct.pack_into('<I', e1, 12, root_sectors)

    tail = bytearray(66)          # 446..512
    tail[0:16] = e0
    tail[16:32] = e1
    tail[64] = 0x55; tail[65] = 0xAA

    with open(img_path, 'r+b') as f:
        f.seek(446)
        f.write(tail)
        f.flush()
        os.fsync(f.fileno())
    print("[GPT] MBR: slot0=0xEE(start=1, %d sectors) slot1=0x83(start=%d, %d sectors)" %
          (min(total - 1, 0xFFFFFFFF), root_lba, root_sectors))


def _open_img(path):
    return open(path, 'r+b')


def main():
    print("=== HepuOS 双内核 UEFI 镜像构建 ===")
    with open(IMG_PATH, 'wb') as img:
        img.truncate(IMG_SIZE)

    # V0.58.5: 环境里 sgdisk / mkfs.vfat / mtools 已被清理且无 root 权限,
    # 回到本脚本自带的纯 Python 实现(GPT CRC 与 FAT32 由 tools/fat32_generator.py 保证)。
    # V0.58.5: 环境重置后 sgdisk / mkfs.vfat / mtools 已用 dpkg -x 从 jammy 包
    # 还原到 /tmp/pkg/root3(sgdisk 1.0.8 / mtools 4.0.43 / dosfstools 4.2),
    # 构建回到历史上验证过 OVMF 能认的路径。
    # 自研 create_gpt_partition_table / create_fat32_esp 保留作研究用途 ——
    # 它们产出的 GPT 与 FAT32 即使逐字段合规, OVMF 仍然 "Not Found" 掉 PXE
    # (缺少 SimpleFileSystem 协议实例), 不能用作出镜像。
    create_gpt_via_sgdisk(IMG_PATH)
    create_fat32_esp_tool(IMG_PATH)

    with open(IMG_PATH, 'r+b') as img:
        create_ext2_data(img)

    # 收尾: 所有句柄关闭后再写 MBR(见函数注释)
    _write_protective_mbr(IMG_PATH, DATA_START, DATA_SECTORS)

    size = os.path.getsize(IMG_PATH)
    print(f"\n=== 构建完成 ===\n  镜像: {IMG_PATH}\n  大小: {size} bytes ({size//1024//1024}MB)")
    print("  布局: GPT | ESP(FAT32 128MB, EFI/BOOT/BOOTX64.EFI + KERNEL64.BIN + HEPBOOT.CFG)"
          " | Root(ext2 120MB)")


if __name__ == "__main__":
    main()
