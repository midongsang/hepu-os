# Hepu OS Secure Boot 签名引导器安装指南

## 概述

Hepu OS 引导器已使用自签名密钥进行 Secure Boot 签名。要在启用 Secure Boot 的真机上启动，需要将 Hepu OS 的公钥导入到固件的 Secure Boot 数据库中。

## 密钥体系

| 密钥 | 用途 | 签名者 |
|------|------|--------|
| PK (Platform Key) | 平台密钥，最高权限 | 自签名 |
| KEK (Key Exchange Key) | 密钥交换密钥，用于更新db | PK签名 |
| db (Signature Database) | 签名数据库，验证EFI文件 | KEK签名 |

## 文件说明

```
tools/hepboot/uefi/secureboot/
├── PK.crt / PK.key      # 平台密钥（证书+私钥）
├── KEK.crt / KEK.key    # 密钥交换密钥
├── db.crt / db.key      # 签名数据库密钥
├── PK.esl / PK.auth     # PK签名列表+认证文件
├── KEK.esl / KEK.auth   # KEK签名列表+认证文件
├── db.esl / db.auth     # db签名列表+认证文件
└── README.md            # 本文件
```

## 方法一：通过BIOS设置导入（推荐）

### 步骤

1. **进入BIOS设置**
   - 重启电脑，按 F2/Del/F10 进入BIOS（不同品牌按键不同）
   - 找到 Secure Boot 相关设置

2. **进入Secure Boot密钥管理**
   - 通常在 Security 或 Boot 菜单下
   - 找到 "Key Management" 或 "Secure Boot Key Management"

3. **清空原有密钥**
   - 选择 "Reset to Setup Mode" 或 "Delete All Keys"
   - 确认删除所有现有密钥（PK/KEK/db/dbx）

4. **导入Hepu OS密钥**
   - 将包含密钥文件的U盘插入电脑
   - 依次导入：
     - **PK**: 选择 `PK.auth` 或 `PK.crt`，类型选 "Platform Key"
     - **KEK**: 选择 `KEK.auth` 或 `KEK.crt`，类型选 "Key Exchange Key"
     - **db**: 选择 `db.auth` 或 `db.crt`，类型选 "Authorized Signature Database"

5. **启用Secure Boot**
   - 保存设置并退出BIOS
   - Secure Boot现在使用Hepu OS密钥验证引导器

6. **启动Hepu OS**
   - 从Hepu OS U盘启动
   - 引导器已签名，Secure Boot验证通过

## 方法二：通过UEFI Shell导入

### 准备

1. 将以下文件复制到U盘FAT32分区根目录：
   - `PK.auth`
   - `KEK.auth`
   - `db.auth`

2. 在BIOS中启动到UEFI Shell

### 执行命令

```shell
# 切换到U盘（通常是FS0:或FS1:）
FS0:

# 查看文件
dir

# 导入db（签名数据库）
dbauth -a db.auth

# 导入KEK（密钥交换密钥）
kekauth -a KEK.auth

# 导入PK（平台密钥）- 导入PK后自动退出Setup Mode
pkauth -a PK.auth

# 验证密钥已导入
dbauth -l
kekauth -l
pkauth -l

# 重启
reset
```

## 方法三：使用KeyTool.efi（图形化工具）

1. 下载 `KeyTool.efi`（来自efitools包）
2. 复制到U盘，从UEFI Shell运行
3. 按界面提示导入密钥

## 验证Secure Boot状态

启动Hepu OS后，在Shell中执行：

```
sbstatus
```

或查看启动日志中的 Secure Boot 状态行。

## 注意事项

### 安全警告

- **私钥文件（.key）必须妥善保管**，不要发布或共享
- 私钥泄露意味着攻击者可以签名恶意引导器
- 建议将私钥存储在离线安全位置

### 兼容性

- 不同品牌BIOS的Secure Boot界面差异较大
- 部分笔记本电脑不允许修改PK（厂商锁定）
- 如果无法修改PK，可以尝试只导入db到现有数据库

### 恢复Windows启动

- 导入Hepu OS密钥后，Windows引导器可能无法通过验证
- 需要同时导入微软的公钥（`Microsoft Windows Production PCA 2011`）
- 或者使用shim链式加载方案（见下文）

## Shim链式加载方案（可选）

如果不想修改固件密钥，可以使用shim：

1. shim是微软签名的第一阶段引导器
2. shim验证并加载我们的引导器
3. 用户无需修改BIOS Secure Boot设置

### 实现步骤

1. 下载shim（`shimx64.efi`）
2. 将shim重命名为 `BOOTX64.EFI`
3. 将我们的引导器命名为 `grubx64.efi`（shim默认加载此名）
4. 将我们的db证书嵌入shim（`shim-mokutil`）

## 故障排除

### 问题：引导器被Secure Boot拒绝

**原因**：密钥未正确导入或签名不匹配

**解决**：
1. 确认BIOS中Secure Boot已启用
2. 确认db密钥已正确导入
3. 确认引导器是签名版本（`hepboot_uefi_signed.efi`）
4. 用 `sbverify --cert db.crt BOOTX64.EFI` 验证签名

### 问题：无法导入PK

**原因**：BIOS处于User Mode，需要先进入Setup Mode

**解决**：
1. 在BIOS中选择 "Reset to Setup Mode"
2. 或删除现有PK后自动进入Setup Mode
3. 然后导入新的PK

### 问题：导入密钥后Windows无法启动

**原因**：Windows引导器需要微软公钥验证

**解决**：
1. 同时导入微软公钥到db
2. 或使用shim方案
3. 或在需要时临时禁用Secure Boot启动Windows

## 相关文件

- 签名引导器：`tools/hepboot/uefi/hepboot_uefi_signed.efi`
- 未签名引导器：`tools/hepboot/uefi/hepboot_uefi.efi`
- 密钥目录：`tools/hepboot/uefi/secureboot/`

## 重新签名引导器

如果修改了引导器代码，需要重新签名：

```bash
cd tools/hepboot/uefi
make  # 编译引导器
sbsign --key secureboot/db.key --cert secureboot/db.crt \
  --output hepboot_uefi_signed.efi hepboot_uefi.efi
```
