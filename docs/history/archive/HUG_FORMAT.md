---
title: HUG_FORMAT
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# `.hug` 应用格式规范

**版本**：V1.0
**适用**：赫普OS原生应用包格式

---

## 1. 概述

`.hug`（Hyper User Gear）是赫普OS专属原生应用封装格式，专为微内核架构设计。

设计目标：
- 极致轻量化，去除冗余
- 自带沙盒隔离声明
- 权限完全透明、显式授权
- 防篡改、防植入
- 跨设备统一分发

---

## 2. 文件整体结构

`.hug` 文件为单一二进制归档，结构如下：

```
┌──────────────────────────────────┐ 偏移 0x00
│  MAGIC (4 bytes)  "HUG1"        │
├──────────────────────────────────┤
│  HEADER (固定 64 bytes)          │
│  - version (u16)                │
│  - flags (u16)                  │
│  - manifest_offset (u32)        │
│  - manifest_size (u32)          │
│  - payload_offset (u32)         │
│  - payload_size (u32)           │
│  - signature_offset (u32)       │
│  - signature_size (u32)         │
│  - reserved (32 bytes)          │
├──────────────────────────────────┤
│  MANIFEST (JSON, zlib压缩)       │
│  应用元信息、权限声明、入口      │
├──────────────────────────────────┤
│  PAYLOAD (zlib压缩归档)          │
│  可执行文件、资源、库            │
├──────────────────────────────────┤
│  SIGNATURE (Ed25519签名)         │
│  对 header+manifest+payload 签名 │
└──────────────────────────────────┘
```

### 2.1 MAGIC
固定四字节：`0x48 0x55 0x47 0x31`（ASCII "HUG1"）

### 2.2 flags 位定义
| 位 | 含义 |
|----|------|
| 0 | 系统应用（1=系统级，0=普通） |
| 1 | 要求创变模式权限 |
| 2 | 包含原生可执行代码 |
| 3 | 仅含Web/解释型资源 |
| 4-15 | 保留 |

---

## 3. Manifest 字段说明

Manifest 为 JSON 格式（zlib压缩存储），解压后示例：

```json
{
  "app_id": "com.hepu.example",
  "app_name": "示例应用",
  "version": "1.0.0",
  "version_code": 1,
  "author": "开发者名称",
  "entry": "bin/main",
  "min_os_version": "1.0.0",
  "target_arch": ["x86_64", "aarch64"],
  "permissions": [
    {"name": "internet", "reason": "用于同步数据", "required": true},
    {"name": "camera", "reason": "扫码功能", "required": false},
    {"name": "storage.read", "reason": "读取用户文件", "required": false}
  ],
  "sandbox": {
    "network": "deny",
    "filesystem": "isolated",
    "ipc": "filtered"
  },
  "icons": {
    "192": "res/icon-192.png",
    "512": "res/icon-512.png"
  }
}
```

### 3.1 权限清单（permissions）
每项权限必须声明 `name`、`reason`、`required`。
- `required: true`：用户不授权则应用无法启动
- `required: false`：用户可拒绝，应用需优雅降级

系统预置权限名：
- `internet` — 网络访问
- `camera` — 相机
- `microphone` — 麦克风
- `location` — 位置信息
- `contacts.read` — 通讯录读取
- `sms.read` — 短信读取
- `storage.read` / `storage.write` — 存储读写
- `notifications` — 发送通知
- `background.run` — 后台运行

### 3.2 sandbox 字段
- `network`：`allow` / `deny` / `filtered`
- `filesystem`：`isolated`（仅应用私有目录）/ `shared`（需授权）
- `ipc`：`none` / `filtered` / `allow`

---

## 4. Payload 内容

Payload 为 zlib 压缩的 tar 归档，内部目录结构：

```
/
├── bin/          可执行文件
├── lib/          依赖库
├── res/          资源文件（图片、音频、配置）
├── locales/      多语言资源
└── manifest.json （冗余副本，用于校验）
```

---

## 5. 签名与校验

1. 签名算法：Ed25519
2. 签名范围：MAGIC + HEADER + MANIFEST + PAYLOAD（不含签名字段本身）
3. 开发者公钥内置在 manifest 中，首次安装时用户确认信任
4. 系统应用使用赫普OS官方根密钥签名
5. 安装时校验失败则拒绝安装

---

## 6. 安装流程

1. 读取 MAGIC，校验文件类型
2. 解析 HEADER，解压 MANIFEST
3. 校验签名
4. 展示权限清单，用户逐项授权
5. 解压 PAYLOAD 到应用私有沙盒目录
6. 注册应用到系统应用列表
7. 生成沙盒隔离配置

---

## 7. 与 APK 的区别

| 特性 | .hug | APK |
|------|------|-----|
| 内核适配 | 赫普微内核原生 | Linux/安卓 |
| 沙盒 | 格式级强制声明 | 依赖系统版本 |
| 权限 | 必须声明reason | 可模糊声明 |
| 签名 | Ed25519 | RSA/ECDSA |
| 后台行为 | 默认禁止，需显式授权 | 普遍自启 |
| 包体积 | 精简，无冗余代码 | 含多架构冗余 |

---

## 8. 工具链

项目提供 `hug-sdk/tools/hugtool.py`：
- `hugtool pack <dir> <output.hug>` — 打包
- `hugtool unpack <file.hug> <dir>` — 解包
- `hugtool verify <file.hug>` — 校验签名与结构
- `hugtool info <file.hug>` — 查看manifest信息
