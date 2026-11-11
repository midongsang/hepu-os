# 支持资源 · SUPPORT

## 快速开始

```bash
git clone https://github.com/midongsang/hepu-os.git
cd hepu-os

# 构建 + 打包镜像
make clean && make -j$(nproc)
bash tools/make_single_img64.sh

# QEMU 启动（本地安装了 QEMU 时）
qemu-system-x86_64 -m 512M -smp 4 -vga std \
  -drive format=raw,file=/tmp/hepu64_single.img

# 或用 GUI 自动化跑回归
QEMU_DATADIR=/usr/share/qemu \
python3 tools/gui_test.py --qemu /usr/bin/qemu-system-x86_64 \
  --image /tmp/hepu64_single.img --scenario all
```

## 沟通渠道

| 类型 | 去哪 | 怎么写 |
|---|---|---|
| **疑问 / 讨论** | [GitHub Discussions](https://github.com/midongsang/hepu-os/discussions) | 普通帖子，标题说清楚问题 |
| **Bug 报告** | [GitHub Issues](https://github.com/midongsang/hepu-os/issues) · `bug` 模板 | 标题前缀 + 复现步骤 + serial.log 片段 + 截图 |
| **功能建议** | [GitHub Issues](https://github.com/midongsang/hepu-os/issues) · `enhancement` 模板 | 说清"要什么 / 为什么 / 替代方案" |
| **安全漏洞** | 见 [SECURITY.md](SECURITY.md) | `[SECURITY]` 标题或私有邮件 |

## 版本策略

- **tag 规则**：`vMAJOR.MINOR.PATCH`，三数字递增
  - MINOR 跳级（如 `v0.54` → `v0.55`）= 大功能合并
  - PATCH 递增（如 `v0.54.6` → `v0.54.7`）= 修复 + 仓库整理
- **哪个分支看**：所有开发在 `main`，**没有** develop/feature 分支
- **Release 页面**：当前仓库已清空版本发布，仅保留 `historical-milestones-v1` 历史里程碑源码归档；镜像由 `tools/make_single_img64.sh` 本地构建

## 构建依赖

| 类别 | apt 包 |
|---|---|
| 编译器 | `build-essential gcc make binutils nasm` |
| 引导器 | `nasm`（32-bit MBR/stage2 汇编） |
| 打包 | `e2fsprogs mtools unzip`（debugfs / losetup / dd） |
| QEMU | `qemu-system-x86 qemu-utils` |
| GUI 测试 | `python3 python3-pip python3-xattr python3-pillow` |

### Fresh clone 缺少外部二进制怎么办？

v0.54.7 起，以下文件**不在仓库里**：

| 需要 | fallback |
|---|---|
| `libc.so.6` + `ld-linux` | `ldconfig -p` → 宿主系统 glibc |
| OVMF `.fd` | 宿主 `/usr/share/OVMF/` 或 `apt install ovmf` |
| Secure Boot 密钥 | `tools/hepboot/uefi/secureboot/generate_keys.sh` 本地生成 |
| Shim `.efi` | Fedora shim 项目，真机 Secure Boot 时才需要 |

如果想**完全离线构建**，把宿主 glibc 复制到 `user/apps/dynhello/`：
```bash
cp /lib/x86_64-linux-gnu/libc.so.6       user/apps/dynhello/
cp /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2  user/apps/dynhello/
```

## 回归测试

QEMU headless 下跑 7 个场景：

```
[PASS] 桌面启动
[PASS] disk fsck
[PASS] 文件管理器
[PASS] 主题取色
[PASS] F10 新建文件
[PASS] F4 重启 (optional)
[PASS] 动态链接 hello → Hello, World!
```

CI 里 push 每次跑 smoke boot；tag push 跑全量 GUI 回归。本地也可以直接 `python3 tools/gui_test.py --scenario all`。

## 贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)。

简版：发 PR，标题加 `[feat]`/`[fix]`/`[refactor]`/`[docs]`/`[chore]`，附 serial.log / 截图 / 回归结果。
