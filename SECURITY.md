# 安全策略 · SECURITY

## 报告安全漏洞

赫普OS 是一个**小型自研操作系统**，未上市/未接入互联网服务端。我们不做 CVE 编号流程，也不运行 HackerOne 之类的漏洞赏金平台。

收到漏洞报告后会按以下流程处理：

1. **72 小时内回复**，确认接收、复现状态、优先级。
2. **下个 tag 内修复**。如果涉及内核级内存破坏、权限逃逸、TLS/密码学实现错误，会单独发一个 hotfix tag（如 `v0.54.8-hotfix`）。
3. **修复后公开**：在 Release Notes 里写清楚漏洞类型、影响范围、根因、修复方式。

### 怎么报

- **GitHub Issue**（推荐，公开方便讨论）：开 `bug` 模板，标题前加 `[SECURITY]`
- **私有邮件**（不想公开时）：找项目维护者邮箱（见 README 作者）

**请不要**把尚未修复的漏洞发给第三方 CVE 机构。

---

## 已知高风险面（刻意暴露 / 尚未加固）

| 模块 | 风险 | 状态 |
|---|---|---|
| `syscall64_full.c` 的 `sys_open`/`sys_openat` | 不校验路径是否真的存在就返回 fd | **刻意**：让 glibc ld.so 缓存探测不中断；上层程序自行容错 |
| `sys_mmap` 文件映射 | 只读实现（PROT_WRITE 会让 COW 错） | **限制**：glibc 的 `MAP_DENYWRITE` 我们当 `PROT_READ` 处理，能跑动态链接但不能做真 mmap 写入 |
| `fork()` COW | 当前 fork 会把整个用户页表复制一份，未做 COW | **待实现**：`P1-4` 优先级 |
| TLS（动态链接） | 用 glibc 自带 TLS；内核侧没有独立 TLS 段管理 | **已工作**：PT_LOAD TLS 段由 ld.so 自行 setup_tls；内核只负责摆位 |
| `seccomp64.c` | syscall 过滤框架已接入，但实际规则极少 | **可扩展** |
| 网络栈 `tcp.c`/`ip.c` | 无连接限速、无 SYN 洪泛防护、无端口随机化 | **实验性**：未上线到 Release 镜像 |
| 密码学（`kernel/crypto/`） | AES/SHA/RSA 硬件不加速、ECDH/ECDSA 是纯软件 | **实验性** |
| 物理内存随机化 | **未实现** | `P1-6` |
| ASLR（PIE + mmap 随机化） | 用户态 PIE 已工作（内核不做 RELATIVE 自举），mmap arena 固定地址 | **部分实现** |

### 不关心的"漏洞"

- **我们就是跑在 QEMU 里的教学实验系统**，虚拟机逃逸对我们不是安全问题。
- **-mno-sse 内核**里不能跑 SSE 指令；如果 glibc 里某个路径硬用了 SSE，属于已知限制不是 bug（P1-7 里 ld.so 的入口 `movaps` 已通过 RSP 对齐绕过）。
- **ext2 文件系统**是自研实现，不追求 e2fsprogs 全校验通过，只要我们自己的 `disk fsck` 自检通过。

---

## 密钥管理

Secure Boot 的 `PK.key` / `KEK.key` / `db.key` **故意不放进 git**（见 `.gitignore` + `v0.54.7` 瘦身 commit）。每个部署实例用 `tools/hepboot/uefi/secureboot/generate_keys.sh` 本地生成一对，**私钥自己保管，别上传别共享**。
