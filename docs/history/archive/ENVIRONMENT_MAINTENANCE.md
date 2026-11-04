---
title: ENVIRONMENT_MAINTENANCE
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# HepuOS 开发环境维护指南

## 环境问题诊断

### 问题1：QEMU频繁丢失
**原因**：沙箱环境每次会话重置，apt安装的系统包丢失。
**解决方案**：
1. 每次会话开始时运行 `source env_setup.sh`
2. 将QEMU二进制文件备份到 `tools/bin/` 目录
3. 使用静态编译的QEMU（可选）

### 问题2：/tmp目录被清理
**原因**：系统定期清理/tmp目录。
**解决方案**：
1. 重要文件放在项目目录下，不要放在/tmp
2. gnu-efi等编译产物备份到 `tools/gnu-efi/`
3. 临时文件使用 `/tmp/hepu_*` 前缀，便于识别

### 问题3：apt源不稳定
**原因**：默认archive.ubuntu.com连接不稳定。
**解决方案**：
1. 使用阿里云镜像源（已配置在env_setup.sh中）
2. 备选：清华源、中科大源

### 问题4：loop设备不可用
**原因**：沙箱环境限制loop设备访问。
**解决方案**：
1. 使用mtools直接操作FAT32镜像（需安装mtools）
2. 或使用Python脚本创建FAT32镜像
3. 或在更新镜像时使用dd直接写入

### 问题5：OVMF固件无法安装
**原因**：apt源不稳定或包名变化。
**解决方案**：
1. 手动下载OVMF固件到 `tools/firmware/`
2. 或使用EDK2编译的OVMF

## 环境初始化流程

每次新会话开始时：

```bash
# 1. 进入项目目录
cd /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os

# 2. 运行环境初始化脚本
source env_setup.sh

# 3. 验证工具
which qemu-system-i386
ls tools/bin/

# 4. 开始开发
```

## 持久化工具清单

| 工具 | 用途 | 持久化位置 |
|------|------|-----------|
| qemu-system-i386 | BIOS引导测试 | tools/bin/ |
| qemu-system-x86_64 | UEFI引导测试 | tools/bin/ |
| OVMF_CODE.fd | UEFI固件 | tools/firmware/ |
| gnu-efi | UEFI引导器编译 | tools/gnu-efi/ |
| mtools | FAT32镜像操作 | tools/bin/ |

## 常用命令

### 编译内核
```bash
cd /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os
make
```

### 编译BIOS引导器
```bash
KERNEL=build_v250/hepu-kernel.bin
HDD_SECTORS=$(( ($(stat -c%s "$KERNEL") + 511) / 512 ))
gcc -m32 -DDEBUG_BOOT -DKERNEL_LBA=50 -DKERNEL_SECTORS=$HDD_SECTORS -DKERNEL_ENTRY=0x101000 -c tools/hepboot/hepboot.S -o /tmp/hepu_bios_boot/hepboot.o
ld -T tools/hepboot/hepboot.ld -m elf_i386 /tmp/hepu_bios_boot/hepboot.o -o /tmp/hepu_bios_boot/hepboot.elf
objcopy -O binary /tmp/hepu_bios_boot/hepboot.elf /tmp/hepu_bios_boot/hepboot_stage2.bin
truncate -s 8192 /tmp/hepu_bios_boot/hepboot_stage2.bin
```

### 编译UEFI引导器
```bash
cd tools/hepboot/uefi
make
```

### 测试BIOS引导
```bash
qemu-system-i386 -drive if=ide,format=raw,file=build/hepu-os-hybrid-v0.10.5.img -boot c -m 512M -serial file:/tmp/serial.log -vga std
```

### 测试UEFI引导
```bash
qemu-system-x86_64 -bios tools/firmware/OVMF_CODE.fd -drive if=ide,format=raw,file=build/hepu-os-hybrid-v0.10.5.img -m 512M
```

## 故障排查

### QEMU启动后无输出
1. 检查镜像是否正确：`ls -la build/*.img`
2. 检查引导器是否更新：`dd if=build/hepu-os-hybrid-v0.10.5.img bs=512 count=1 2>/dev/null | xxd | tail -3`
3. 检查串口输出：`cat /tmp/serial.log`

### 编译失败
1. 检查依赖：`which gcc ld objcopy`
2. 检查32位支持：`gcc -m32 -E - < /dev/null`
3. 检查gnu-efi：`ls /tmp/gnu-efi-3.0.13/inc/efi.h`

### 镜像更新后无法启动
1. 检查MBR签名：最后两字节应为0x55AA
2. 检查stage2位置：LBA34（偏移17408字节）
3. 检查内核位置：LBA50（偏移25600字节）
4. 检查内核大小：`ls -la build_v250/hepu-kernel.bin`
