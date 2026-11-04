#!/usr/bin/env python3
"""为混合镜像写入 GPT 分区表(容器内 parted 不可用时替代)
用法: make_gpt.py <img> <total_mb> <esp_start_mb> <esp_size_mb> <ext2_lba> <ext2_sectors> <mbr_ext2_lba>
写: LBA0 保护性MBR(0xEE全盘 + 0x83 ext2项), LBA1 GPT头, LBA2-33 分区项, 尾部备份GPT
"""
import struct, sys, hashlib, uuid

def crc32(b):
    return struct.pack('<I', zlib_crc(b))

import zlib
def zlib_crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF

def main():
    img, total_mb, esp_start_mb, esp_size_mb, ext2_lba, ext2_sec, mbr_ext2_lba = sys.argv[1:8]
    ext2_lba = int(ext2_lba); ext2_sec = int(ext2_sec); mbr_ext2_lba = int(mbr_ext2_lba)
    total_sectors = int(total_mb) * 2048
    esp_first = int(esp_start_mb) * 2048
    esp_sec = int(esp_size_mb) * 2048
    esp_last = esp_first + esp_sec - 1
    ext2_last = ext2_lba + ext2_sec - 1

    backup_hdr_lba = total_sectors - 33          # 备份GPT头
    backup_pe_lba = backup_hdr_lba + 1          # 备份分区项

    with open(img, 'r+b') as f:
        f.seek(0)

        # ---- MBR ----
        mbr = bytearray(512)
        mbr[510:512] = b'\x55\xAA'
        # part0: 0xEE 保护分区覆盖全盘
        e = bytearray(16); e[4] = 0xEE
        e[8:12] = struct.pack('<I', 1)
        e[12:16] = struct.pack('<I', total_sectors - 1)
        mbr[446:462] = e
        # part1: 0x83 ext2 (内核按此定位)
        e2 = bytearray(16); e2[4] = 0x83
        e2[8:12] = struct.pack('<I', mbr_ext2_lba)
        e2[12:16] = struct.pack('<I', ext2_sec)
        mbr[462:478] = e2
        f.write(mbr)

        # ---- 分区项数组 (LBA2) ----
        pes = bytearray(128 * 128)
        ESP_GUID = bytes.fromhex('C12A7328F81F11D2BA4B00A0C93EC93B')
        EXT_GUID = bytes.fromhex('0FC63DAF848347728E793D69D8477DE4')
        def entry(tguid, lba_f, lba_l, name):
            b = bytearray(128)
            b[0:16] = tguid
            b[16:32] = uuid.uuid4().bytes
            b[32:40] = struct.pack('<Q', lba_f)
            b[40:48] = struct.pack('<Q', lba_l)
            for i, ch in enumerate(name.encode('utf-16le')):
                b[56+i] = ch
            return b
        pes[0:128] = entry(ESP_GUID, esp_first, esp_last, 'ESP')
        pes[128:256] = entry(EXT_GUID, ext2_lba, ext2_last, 'HepuOS')
        pe_crc = zlib_crc(bytes(pes))

        # ---- GPT 头 (LBA1) ----
        hdr = bytearray(92)
        hdr[0:8] = b'EFI PART'
        hdr[8:12] = struct.pack('<I', 0x00010000)
        hdr[12:16] = struct.pack('<I', 92)
        hdr[24:32] = struct.pack('<Q', 1)            # 当前LBA
        hdr[32:40] = struct.pack('<Q', backup_hdr_lba)
        hdr[40:48] = struct.pack('<Q', 34)           # 首个可用
        hdr[48:56] = struct.pack('<Q', backup_hdr_lba - 1)
        hdr[56:72] = uuid.uuid4().bytes
        hdr[72:80] = struct.pack('<Q', 2)            # 分区项LBA
        hdr[80:84] = struct.pack('<I', 128)
        hdr[84:88] = struct.pack('<I', 128)
        hdr[88:92] = pe_crc.to_bytes(4, 'little')   # 分区项CRC
        hdr_crc = zlib_crc(bytes(hdr))
        hdr[16:20] = struct.pack('<I', hdr_crc)
        f.write(hdr)

        # 分区项写入 LBA2
        f.seek(2 * 512)
        f.write(pes)

        # ---- 备份 GPT (尾部) ----
        f.seek(backup_hdr_lba * 512)
        bh = bytearray(hdr)
        bh[24:32] = struct.pack('<Q', backup_hdr_lba)
        bh[32:40] = struct.pack('<Q', 1)
        bh[40:48] = struct.pack('<Q', 34)
        bh[48:56] = struct.pack('<Q', backup_hdr_lba - 1)
        bh[16:20] = b'\x00\x00\x00\x00'
        bh[16:20] = struct.pack('<I', zlib_crc(bytes(bh)))
        f.write(bh)
        f.seek(backup_pe_lba * 512)
        f.write(pes)
    print(f"GPT written: ESP@{esp_first}-{esp_last} ext2@{ext2_lba}-{ext2_last} backup@{backup_hdr_lba}")

if __name__ == '__main__':
    main()
