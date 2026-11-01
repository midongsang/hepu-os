# Hepu OS Shim 链式加载 Secure Boot 方案

## 概述

Shim是微软签名的第一阶段引导器，Secure Boot默认信任微软签名。Shim启动后验证并加载Hepu OS引导器，**用户无需修改BIOS Secure Boot设置**。

## 工作原理

```
UEFI固件 (Secure Boot启用)
    ↓ 验证微软签名
BOOTX64.EFI = shimx64.efi (微软签名)
    ↓ 验证MOK证书签名
grubx64.efi = hepboot_uefi_signed.efi (Hepu OS签名)
    ↓
Hepu OS 内核
```

## 文件布局

ESP分区 `/EFI/BOOT/` 目录：

| 文件 | 来源 | 说明 |
|------|------|------|
| BOOTX64.EFI | shimx64.efi | 微软签名的第一阶段引导器 |
| grubx64.efi | hepboot_uefi_signed.efi | Hepu OS签名引导器（shim默认加载此名） |
| mmx64.efi | MOK管理工具 | 证书导入界面 |

ESP分区根目录：
- `HEPU_DB.crt` - Hepu OS签名证书（用于MOK导入）

## 真机首次使用步骤

### 第一次启动（导入证书，一次性操作）

1. 将Hepu OS U盘插入电脑
2. 确保BIOS中Secure Boot已启用（默认状态）
3. 从U盘启动
4. Shim会显示MOK管理界面（因为grubx64.efi的证书尚未被信任）
5. 选择 **"Enroll key from disk"**
6. 选择U盘的ESP分区，找到 `HEPU_DB.crt` 文件
7. 确认导入证书
8. 系统重启
9. 之后每次启动都会自动通过Secure Boot验证，无需再次操作

### 后续启动

- 直接从U盘启动，Secure Boot自动验证通过
- 无需任何额外操作

## 验证Secure Boot状态

启动Hepu OS后，在Shell中执行：
```
sbstatus
```
或查看启动日志中的Secure Boot状态行。

## 常见问题

### Q: 为什么不直接用我们的签名引导器？

A: 因为我们的证书不在固件的db数据库中，Secure Boot会拒绝。Shim有微软签名，固件默认信任，shim再验证我们的引导器。

### Q: MOK证书导入后会影响Windows启动吗？

A: 不会。MOK证书是shim管理的，不影响固件的db数据库。Windows引导器仍由固件直接验证。

### Q: 可以在多台电脑上使用同一个U盘吗？

A: 可以。但每台电脑第一次启动时都需要导入一次MOK证书。

### Q: 如何移除MOK证书？

A: 在MOK管理界面选择 "Delete key"，或清除shim的NVRAM变量。

## 技术细节

### Shim版本
- 来源：Ubuntu 22.04 shim-signed 1.51+15.4
- 签名：Microsoft Windows Production PCA 2011
- 支持：Secure Boot、MOK、回退保护

### Hepu OS引导器签名
- 证书：Hepu OS Signature Database (db)
- 签发者：Hepu OS Key Exchange Key (KEK)
- 算法：RSA-2048 + SHA-256
- 有效期：10年

### 证书链
```
Hepu OS Platform Key (PK) - 自签名
    └── Hepu OS Key Exchange Key (KEK) - PK签名
        └── Hepu OS Signature Database (db) - KEK签名
            └── hepboot_uefi_signed.efi - db签名
```

## 相关文件

- Shim文件：`tools/hepboot/uefi/shim/`
- 密钥文件：`tools/hepboot/uefi/secureboot/`
- 签名引导器：`tools/hepboot/uefi/hepboot_uefi_signed.efi`
- 签名脚本：`tools/hepboot/uefi/sign_bootloader.sh`

## 重新构建引导链

修改引导器代码后：

```bash
cd tools/hepboot/uefi
make                    # 编译引导器
./sign_bootloader.sh    # 签名引导器
# 更新镜像：
mcopy -o -i ../../../build/hepu-os-uefi-v0.08.img@@1M \
  hepboot_uefi_signed.efi ::/EFI/BOOT/grubx64.efi
```

注意：只需要更新grubx64.efi，shimx64.efi不需要重新签名。
