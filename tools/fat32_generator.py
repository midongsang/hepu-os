#!/usr/bin/env python3
"""
简化FAT32文件系统生成器
用于创建HepuOS UEFI ESP分区
"""
import struct
import os

class FAT32Generator:
    def __init__(self, size_bytes, sector_size=512, sectors_per_cluster=8):
        self.sector_size = sector_size
        self.sectors_per_cluster = sectors_per_cluster
        self.cluster_size = sector_size * sectors_per_cluster
        self.total_sectors = size_bytes // sector_size

        # FAT32参数
        self.reserved_sectors = 32
        self.number_of_fats = 2
        self.root_dir_cluster = 2

        # 计算FAT大小
        data_sectors = self.total_sectors - self.reserved_sectors - 32
        self.total_clusters = data_sectors // sectors_per_cluster
        self.fat_size = (self.total_clusters * 4 + sector_size - 1) // sector_size

        # 计算各区域起始扇区
        self.fat_start = self.reserved_sectors
        self.root_dir_start = self.reserved_sectors + self.number_of_fats * self.fat_size
        self.data_start = self.root_dir_start

        # 创建数据缓冲区
        self.data = bytearray(size_bytes)

        # 文件列表
        self.files = {}

    def create_boot_sector(self):
        """创建FAT32引导扇区"""
        bs = bytearray(512)

        # 跳转指令
        bs[0] = 0xEB
        bs[1] = 0x58
        bs[2] = 0x90

        # OEM名称
        bs[3:11] = b'MSDOS5.0'

        # 每扇区字节数
        struct.pack_into('<H', bs, 11, self.sector_size)
        # 每簇扇区数
        bs[13] = self.sectors_per_cluster
        # 保留扇区数
        struct.pack_into('<H', bs, 14, self.reserved_sectors)
        # FAT数量
        bs[16] = self.number_of_fats
        # 根目录条目数（FAT32为0）
        struct.pack_into('<H', bs, 17, 0)
        # 总扇区数（FAT32为0，使用大扇区数）
        struct.pack_into('<H', bs, 19, 0)
        # 介质描述符
        bs[21] = 0xF8
        # 每FAT扇区数（FAT16为0）
        struct.pack_into('<H', bs, 22, 0)
        # 每磁道扇区数
        struct.pack_into('<H', bs, 24, 63)
        # 磁头数
        struct.pack_into('<H', bs, 26, 255)
        # 隐藏扇区数
        struct.pack_into('<I', bs, 28, 0)
        # 大总扇区数
        struct.pack_into('<I', bs, 32, self.total_sectors)

        # FAT32扩展字段
        # 每FAT扇区数
        struct.pack_into('<I', bs, 36, self.fat_size)
        # 扩展标志
        struct.pack_into('<H', bs, 40, 0)
        # 文件系统版本
        struct.pack_into('<H', bs, 42, 0)
        # 根目录簇号
        struct.pack_into('<I', bs, 44, self.root_dir_cluster)
        # FS信息扇区
        struct.pack_into('<H', bs, 48, 1)
        # 备份引导扇区
        struct.pack_into('<H', bs, 50, 6)
        # 保留
        for i in range(52, 64):
            bs[i] = 0
        # 物理驱动器号
        bs[64] = 0x80
        # 保留
        bs[65] = 0
        # 扩展引导签名
        bs[66] = 0x29
        # 卷序列号
        struct.pack_into('<I', bs, 67, 0x12345678)
        # 卷标
        bs[71:82] = b'HEPU_ESP   '
        # 文件系统类型
        bs[82:90] = b'FAT32   '

        # 引导代码
        bs[90:510] = b'\x00' * 420

        # 签名
        bs[510] = 0x55
        bs[511] = 0xAA

        self.data[0:512] = bs

    def create_fsinfo(self):
        """创建FS信息扇区"""
        fsinfo = bytearray(512)
        fsinfo[0:4] = b'RRaA'
        fsinfo[484:488] = b'RRaA'
        struct.pack_into('<I', fsinfo, 488, 0xFFFFFFFF)  # 空闲簇数
        struct.pack_into('<I', fsinfo, 492, self.root_dir_cluster)  # 下一个空闲簇
        fsinfo[510] = 0x55
        fsinfo[511] = 0xAA
        self.data[512:1024] = fsinfo

    def init_fat(self):
        """初始化FAT表"""
        fat_offset = self.fat_start * self.sector_size
        # FAT[0] = 0x0FFFFFF8 (介质描述符)
        struct.pack_into('<I', self.data, fat_offset, 0x0FFFFFF8)
        # FAT[1] = 0x0FFFFFFF (EOC标记)
        struct.pack_into('<I', self.data, fat_offset + 4, 0x0FFFFFFF)
        # 根目录簇 = EOC
        struct.pack_into('<I', self.data, fat_offset + self.root_dir_cluster * 4, 0x0FFFFFFF)

        # 复制到第二个FAT
        fat2_offset = (self.fat_start + self.fat_size) * self.sector_size
        self.data[fat2_offset:fat2_offset + self.fat_size * self.sector_size] = \
            self.data[fat_offset:fat_offset + self.fat_size * self.sector_size]

    def allocate_cluster(self, cluster_num, data):
        """分配簇并写入数据"""
        cluster_offset = self.data_start * self.sector_size + \
                        (cluster_num - 2) * self.cluster_size
        self.data[cluster_offset:cluster_offset + len(data)] = data

        # 更新FAT
        fat_offset = self.fat_start * self.sector_size
        struct.pack_into('<I', self.data, fat_offset + cluster_num * 4, 0x0FFFFFFF)

        # 复制到第二个FAT
        fat2_offset = (self.fat_start + self.fat_size) * self.sector_size
        struct.pack_into('<I', self.data, fat2_offset + cluster_num * 4, 0x0FFFFFFF)

    def create_dir_entry(self, name, ext, cluster, size, is_dir=False):
        """创建目录条目"""
        entry = bytearray(32)

        # 文件名（8.3格式）
        name_bytes = name.ljust(8)[:8].upper().encode()
        ext_bytes = ext.ljust(3)[:3].upper().encode()
        entry[0:8] = name_bytes
        entry[8:11] = ext_bytes

        # 属性
        if is_dir:
            entry[11] = 0x10  # 目录
        else:
            entry[11] = 0x20  # 归档

        # 保留
        entry[12] = 0
        # 创建时间（毫秒）
        entry[13] = 0
        # 创建时间
        struct.pack_into('<H', entry, 14, 0)
        # 创建日期
        struct.pack_into('<H', entry, 16, 0x4A21)  # 2026-01-01
        # 最后访问日期
        struct.pack_into('<H', entry, 18, 0x4A21)
        # 簇号高16位
        struct.pack_into('<H', entry, 20, (cluster >> 16) & 0xFFFF)
        # 最后修改时间
        struct.pack_into('<H', entry, 22, 0)
        # 最后修改日期
        struct.pack_into('<H', entry, 24, 0x4A21)
        # 簇号低16位
        struct.pack_into('<H', entry, 26, cluster & 0xFFFF)
        # 文件大小
        struct.pack_into('<I', entry, 28, size)

        return entry

    def add_file(self, path, data):
        """添加文件到文件系统（支持子目录）"""
        parts = path.strip('/').split('/')
        filename = parts[-1]

        # 解析文件名（8.3格式）
        if '.' in filename:
            name, ext = filename.rsplit('.', 1)
        else:
            name, ext = filename, ''

        # 找到或创建目录路径
        parent_cluster = 2  # 根目录簇
        for i in range(len(parts) - 1):
            dir_name = parts[i]
            parent_cluster = self._find_or_create_dir(parent_cluster, dir_name)

        # 分配簇
        num_clusters = (len(data) + self.cluster_size - 1) // self.cluster_size
        if num_clusters == 0:
            num_clusters = 1

        # 找到空闲簇
        fat_offset = self.fat_start * self.sector_size
        start_cluster = self._next_free_cluster()
        for i in range(num_clusters):
            cluster = start_cluster + i
            chunk = data[i * self.cluster_size:(i + 1) * self.cluster_size]
            self.allocate_cluster(cluster, chunk)
            if i < num_clusters - 1:
                struct.pack_into('<I', self.data, fat_offset + cluster * 4, cluster + 1)
                fat2_offset = (self.fat_start + self.fat_size) * self.sector_size
                struct.pack_into('<I', self.data, fat2_offset + cluster * 4, cluster + 1)
        # 标记最后一个簇为EOF
        struct.pack_into('<I', self.data, fat_offset + (start_cluster + num_clusters - 1) * 4, 0x0FFFFFFF)
        fat2_offset = (self.fat_start + self.fat_size) * self.sector_size
        struct.pack_into('<I', self.data, fat2_offset + (start_cluster + num_clusters - 1) * 4, 0x0FFFFFFF)

        # 创建目录条目
        entry = self.create_dir_entry(name, ext, start_cluster, len(data))

        # 写入父目录
        self._add_dir_entry(parent_cluster, entry)

        self.files[path] = (start_cluster, len(data))

    def _next_free_cluster(self):
        """找到下一个空闲簇"""
        fat_offset = self.fat_start * self.sector_size
        cluster = 3
        while True:
            val = struct.unpack_from('<I', self.data, fat_offset + cluster * 4)[0]
            if val == 0:
                return cluster
            cluster += 1

    def _find_or_create_dir(self, parent_cluster, dir_name):
        """在父目录中查找或创建子目录，返回子目录簇号"""
        dir_cluster = self._find_dir_entry(parent_cluster, dir_name)
        if dir_cluster:
            return dir_cluster

        # 创建新目录
        dir_cluster = self._next_free_cluster()
        self.allocate_cluster(dir_cluster, b'\x00' * self.cluster_size)
        fat_offset = self.fat_start * self.sector_size
        struct.pack_into('<I', self.data, fat_offset + dir_cluster * 4, 0x0FFFFFFF)
        fat2_offset = (self.fat_start + self.fat_size) * self.sector_size
        struct.pack_into('<I', self.data, fat2_offset + dir_cluster * 4, 0x0FFFFFFF)

        # 创建.和..条目
        dot_entry = self.create_dir_entry('.', '', dir_cluster, 0, is_dir=True)
        dotdot_entry = self.create_dir_entry('..', '', parent_cluster, 0, is_dir=True)
        self._add_dir_entry(dir_cluster, dot_entry)
        self._add_dir_entry(dir_cluster, dotdot_entry)

        # 在父目录中添加目录条目
        if '.' in dir_name:
            dname, dext = dir_name.rsplit('.', 1)
        else:
            dname, dext = dir_name, ''
        dir_entry = self.create_dir_entry(dname, dext, dir_cluster, 0, is_dir=True)
        self._add_dir_entry(parent_cluster, dir_entry)

        return dir_cluster

    def _find_dir_entry(self, dir_cluster, name):
        """在目录中查找条目，返回簇号"""
        dir_offset = (self.data_start + (dir_cluster - 2) * self.sectors_per_cluster) * self.sector_size
        for i in range(16):
            e_offset = dir_offset + i * 32
            if self.data[e_offset] == 0:
                break
            if self.data[e_offset] == 0xE5:
                continue
            entry_name = self.data[e_offset:e_offset + 8].decode('ascii', errors='replace').strip()
            if entry_name.upper() == name.upper()[:8]:
                return struct.unpack_from('<H', self.data, e_offset + 20)[0] << 16 | struct.unpack_from('<H', self.data, e_offset + 26)[0]
        return None

    def _add_dir_entry(self, dir_cluster, entry):
        """在目录中添加条目"""
        dir_offset = (self.data_start + (dir_cluster - 2) * self.sectors_per_cluster) * self.sector_size
        for i in range(16):
            e_offset = dir_offset + i * 32
            if self.data[e_offset] == 0 or self.data[e_offset] == 0xE5:
                self.data[e_offset:e_offset + 32] = entry
                return

    def generate(self):
        """生成完整的FAT32文件系统"""
        self.create_boot_sector()
        self.create_fsinfo()
        self.init_fat()
        return bytes(self.data)


def create_esp_partition(size_bytes, files):
    """创建ESP分区"""
    fat = FAT32Generator(size_bytes)

    # 创建目录结构
    # 先创建/EFI/BOOT目录
    # 简化：直接在根目录创建文件，UEFI也能识别

    for path, data in files.items():
        fat.add_file(path, data)

    return fat.generate()


if __name__ == '__main__':
    # 测试
    files = {
        '/test.txt': b'Hello World\n',
    }
    data = create_esp_partition(64 * 1024 * 1024, files)
    with open('/tmp/test_fat32.img', 'wb') as f:
        f.write(data)
    print(f"Created FAT32 image: {len(data)} bytes")
