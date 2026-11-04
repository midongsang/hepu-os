#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
赫普OS - ISO 9660 + El Torito 引导镜像构造器
手写生成可启动ISO，不依赖 genisoimage/mkisofs/xorriso。

布局（2048字节扇区）：
  LBA 0-15  : 系统区
  LBA 16    : PVD (Primary Volume Descriptor)
  LBA 17    : Boot Record (El Torito)
  LBA 18    : Terminator
  LBA 19    : Boot Catalog
  LBA 20    : Boot Image（引导器ISO变体，1扇区）
  LBA 21    : 内核文件数据（引导器从固定LBA 21读取，ISO目录项指向同一数据）
  之后      : Path Table / 根目录数据区
"""
import struct
import sys
import os

SECTOR = 2048

def both_endian_u16(v):
    return struct.pack('<H', v) + struct.pack('>H', v)

def both_endian_u32(v):
    return struct.pack('<I', v) + struct.pack('>I', v)

def make_directory_record(location, length, date_bytes, flags,
                          file_id, file_id_len=None):
    if file_id_len is None:
        file_id_len = len(file_id)
    # 名字长度偶则补1字节pad
    name_field = file_id
    total = 33 + file_id_len
    if total % 2 == 1:
        name_field += b'\x00'
    rec = bytearray()
    rec.append(0)                      # 长度，最后填
    rec.append(0)                      # 扩展属性长度
    rec += both_endian_u32(location)   # extent位置
    rec += both_endian_u32(length)     # 数据长度
    rec += date_bytes                  # 7字节日期
    rec.append(flags)                  # 标志
    rec.append(0)                      # 文件单元大小
    rec.append(0)                      # 交错间隙
    rec += both_endian_u16(1)          # 卷序号
    rec.append(file_id_len)            # 文件名长度
    rec += name_field
    # 计算并填充记录长度（包含pad，至少34）
    while len(rec) < 34:
        rec.append(0)
    rec[0] = len(rec)
    return bytes(rec)

def make_iso(boot_img_bytes, kernel_bytes, kernel_name,
             volume_id=b'HEPUOS', out_path='hepu-os.iso'):
    # 内核扇区数
    kernel_sectors = (len(kernel_bytes) + SECTOR - 1) // SECTOR
    # boot image 占1扇区（pad到2048）
    boot_img = boot_img_bytes.ljust(SECTOR, b'\x00')[:SECTOR]

    # 日期时间（PVD 7字节目录日期）
    import datetime
    now = datetime.datetime.now()
    date7 = bytes([now.year - 1900, now.month, now.day, now.hour,
                   now.minute, now.second, 0])

    # 布局
    LBA_PVD = 16
    LBA_BOOTREC = 17
    LBA_TERM = 18
    LBA_BOOTCAT = 19
    LBA_BOOTIMG = 20
    LBA_KERNEL = 21
    LBA_PATHTBL = LBA_KERNEL + kernel_sectors
    LBA_ROOTDIR = LBA_PATHTBL + 1
    VOLUME_SIZE = LBA_ROOTDIR + 1

    # ---- 路径表 (L path table) ----
    # 根目录记录
    root_pt = bytearray()
    root_pt.append(1)                  # len_di
    root_pt.append(0)                  # ext_attr
    root_pt.append(0)                  # dir_id = "\0"
    root_pt += struct.pack('<HH', 1, 1)  # dir_number, parent_dir_number
    # pad到偶数
    if len(root_pt) % 2 == 1:
        root_pt.append(0)
    path_table = bytes(root_pt)
    path_table_size = len(path_table)

    # ---- 根目录数据区 ----
    # 内核文件名（8.3）
    kern_id = kernel_name.encode('ascii')
    dot_index = kern_id.index(b'.')
    kern_8_3 = kern_id[:dot_index][:8] + b'.' + kern_id[dot_index+1:][:3]
    if len(kern_8_3) == 9:  # 无扩展名场景（HEPU-KRN.BIN 有扩展名）
        pass

    root_dir_data = bytearray()
    # "." 记录：指向根目录数据区自身
    root_dir_data += make_directory_record(LBA_ROOTDIR, SECTOR, date7, 0x02, b'.')
    # ".." 记录
    root_dir_data += make_directory_record(LBA_ROOTDIR, SECTOR, date7, 0x02, b'..')
    # 内核文件记录：指向 LBA_KERNEL
    root_dir_data += make_directory_record(LBA_KERNEL, len(kernel_bytes), date7,
                                           0x00, kern_8_3)
    # pad到2048
    while len(root_dir_data) < SECTOR:
        root_dir_data.append(0)
    root_dir_data = bytes(root_dir_data)
    root_dir_length = SECTOR

    # ---- PVD ----
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[6] = 1
    pvd[8:40] = b' ' * 32
    pvd[40:72] = volume_id.ljust(32, b' ')[:32]
    pvd[80:88] = both_endian_u32(VOLUME_SIZE)
    pvd[120:124] = both_endian_u16(1)      # volume set size
    pvd[124:128] = both_endian_u16(1)      # volume sequence number
    pvd[128:132] = both_endian_u16(SECTOR) # logical block size
    pvd[132:140] = both_endian_u32(path_table_size)
    pvd[140:144] = struct.pack('<I', LBA_PATHTBL)   # L path table loc
    pvd[144:148] = struct.pack('<I', 0)             # L optional
    pvd[148:152] = struct.pack('>I', LBA_PATHTBL)   # M path table loc
    pvd[152:156] = struct.pack('>I', 0)             # M optional
    pvd[156:190] = make_directory_record(LBA_ROOTDIR, root_dir_length,
                                         date7, 0x02, b'')  # 根目录记录(含名字?)
    # 根目录记录在PVD里名字为空(文件标识长度0)；但make_directory_record会加名字
    # 修正：PVD中的根目录记录 file_id_len=0
    pvd[190:318] = b' ' * 128                     # volume set id
    pvd[318:446] = b' ' * 128                     # publisher
    pvd[446:574] = b' ' * 128                     # preparer
    pvd[574:702] = (b'HEPU HYPER EVOLVE OS ISO'
                    .ljust(128, b' ')[:128])      # application id
    pvd[702:739] = b' ' * 37
    pvd[739:776] = b' ' * 37
    pvd[776:813] = b' ' * 37
    # 创建日期
    pvd[813:830] = date7 + bytes([0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    pvd[830:847] = date7 + bytes([0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    pvd[881] = 1                                  # file structure version
    pvd = bytes(pvd)

    # ---- Boot Record (El Torito) ----
    bootrec = bytearray(SECTOR)
    bootrec[0] = 0
    bootrec[1:6] = b'CD001'
    bootrec[6] = 1
    bootrec[7:30] = b'EL TORITO SPECIFICATION'
    bootrec[30:40] = b'\x00' * 10
    bootrec[71:78] = struct.pack('<I', LBA_BOOTCAT)[0:4] + b'\x00' * 3
    # 标准：boot catalog pointer 在 offset 71 (4字节, LSB)
    bootrec[71:75] = struct.pack('<I', LBA_BOOTCAT)
    bootrec = bytes(bootrec)

    # ---- Terminator ----
    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b'CD001'
    term[6] = 1
    term = bytes(term)

    # ---- Boot Catalog ----
    cat = bytearray(SECTOR)
    # Validation Entry
    cat[0] = 1                                    # header id
    cat[1] = 0                                    # platform 80x86
    cat[2:4] = b'\x00\x00'
    cat[4:28] = b'HEPUOS BOOT CATALOG'.ljust(24, b' ')[:24]
    # checksum: 使Validation Entry按16位小端字求和(含0xAA55尾字)为0 (El Torito规范)
    # 注意：字节序列 55 AA 按小端字读为 0xAA55
    def _word_sum16(b):
        t = 0
        for i in range(0, len(b) - 1, 2):
            t = (t + struct.unpack('<H', b[i:i+2])[0]) & 0xFFFF
        return t
    s = (_word_sum16(bytes(cat[0:28])) + 0xAA55) & 0xFFFF
    cat[28:30] = struct.pack('<H', (0 - s) & 0xFFFF)
    cat[30:32] = b'\x55\xAA'
    # Initial/Default Entry
    cat[32] = 0x88                                # bootable
    cat[33] = 0                                   # no emulation
    cat[34:36] = struct.pack('<H', 0x07C0)        # load segment
    cat[36] = 0                                   # system type
    cat[37] = 0
    cat[38:40] = struct.pack('<H', 1)             # sector count
    cat[40:44] = struct.pack('<I', LBA_BOOTIMG)   # load RBA
    cat[44:52] = b'\x00' * 8
    cat[52:76] = b'HEPU OS BOOT IMAGE'.ljust(24, b' ')[:24]
    # 结束条目
    cat[64:96] = b'\x00' * 32
    cat = bytes(cat)

    # ---- 组装 ----
    sectors = []
    sectors.append(b'\x00' * (16 * SECTOR))        # 系统区 16个2048扇区
    sectors.append(pvd)
    sectors.append(bootrec)
    sectors.append(term)
    sectors.append(cat)
    sectors.append(boot_img)
    sectors.append(kernel_bytes.ljust(kernel_sectors * SECTOR, b'\x00'))
    sectors.append(path_table.ljust(SECTOR, b'\x00'))
    sectors.append(root_dir_data)
    data = b''.join(sectors)
    data = data.ljust(VOLUME_SIZE * SECTOR, b'\x00')

    with open(out_path, 'wb') as f:
        f.write(data)
    return out_path, VOLUME_SIZE

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("用法: make_iso.py <boot_img> <kernel_bin> <out_iso>")
        sys.exit(1)
    boot = open(sys.argv[1], 'rb').read()
    kern = open(sys.argv[2], 'rb').read()
    path, size = make_iso(boot, kern, 'HEPU-KRN.BIN', out_path=sys.argv[3])
    print(f"ISO已生成: {path} ({size} 扇区, {size*SECTOR/1024/1024:.2f} MB)")
