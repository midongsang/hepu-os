# P3 远期项：真实阻塞点与前置条件（2026-11-18 核对）

本文只写**已经用代码确认**的阻塞点，不写"将来也许"。每条给：现状证据、为什么现在做不了、解锁它需要什么。

---

## P3-2 GPS 定位 + 天气联网

**现状**：`kernel/arch/x86_64/gps64.c` 289 行，`grep` 全文无 `http`/`net64`/`socket` 调用 —— 只有驱动骨架，没有取数路径。

**为什么做不了**：GPS 需要 NMEA 数据源（串口/蓝牙模块）。QEMU 没有 GPS 设备，接了也无数据可验。天气部分已由 `hpdesk.c` 的 HTTP 链路覆盖（`t.weather.itboy.net`，见 0.61.0）。

**解锁条件**：一个真实的 GPS 模块（USB/串口 NMEA）+ 真机。到那一步只需把 `gps64.c` 的 `gps_get_fix()` 输出接进桌面天气卡，代码量不大。

---

## P3-3 驱动在线下载

**现状**：`kmod64.c` 已编入 Makefile，但无 HTTP 下载路径（`grep 'http' kmod64.c` 为空）。

**为什么做不了**：它依赖两件都未就绪的事 —— ① HTTPS 端到端（见 P3-5，底层握手是模拟桩）；② 驱动签名校验（P0-6，未开始，只有 XTEA 对称加密在 crypto 里）。**没有签名校验的在线驱动下载是安全债，不能先做。**

---

## P3-4 Rust 用户态服务

**现状**：`Makefile:210-232` 已有完整机制 —— `HAVE_CARGO := $(shell which cargo)`，有 cargo 时构建 12 个 Rust ELF（`hello-rust/cat/echo/sysinfo/file-server/...`），无 cargo 时整体跳过（避免"没装 Rust 导致 32 位内核编不出来"）。

**为什么做不了**：本机 `command -v cargo rustc` 为空。而且这套构建要 `nightly` + `-Z build-std=core,alloc` + 自定义 `hepu-i686.json` target，装齐的成本是几百 MB + 网络。

**解锁条件**：装 rustup + nightly + 对应 target；然后 `make` 会按现有规则产出 12 个 ELF 并打进 initrd。**机制本身已验证设计正确（有 cargo/无 cargo 两条分支都在），缺的只是工具链。**

---

## P3-5 安全审计闭环（TLS 接 HTTP）

**现状（关键）**：`kernel/net/tls.c:183` 的 `hepu_tls_handshake()` **是模拟握手**——

```c
/* 简化：模拟握手完成 */
hepu_tls_generate_random(session->server_random, 32);
session->cipher_suite = TLS_CIPHER_ECDHE_RSA_WITH_AES_128_GCM_SHA256;
session->encrypted = 1;
session->state = TLS_STATE_HANDSHAKE_DONE;
```

它构造了 ClientHello（`hepu_tls_build_client_hello`），但**没有把 ClientHello 发到网络**、**没有收 ServerHello/Certificate**、**没有做密钥交换**、**没有验证证书链**，直接生成随机密钥并标记"握手完成"。

**为什么"接 HTTP"没意义**：即使给 `net64_http.c` 加 `https://` 路由并调 `hepu_tls_connect`，加密出来的数据对端无法解密、对端证书也从未校验 —— 得到的是"看起来像 HTTPS 的垃圾流"，比明文更危险（给人已加密的错觉）。

**解锁顺序（必须先做底层，不能反过来）**：
1. `hepu_tls_handshake` 改成真实现：发 ClientHello → 收 ServerHello/ServerKeyExchange/Certificate → 验 X.509 链（crypto 里 RSA/ECDHE/AES-GCM 原语已有，`tls_handshake.c` 571 行是骨架）；
2. 记录层加解密接入 `tls_record.c`（`encrypted=1` 只是标志位，实际 `hepu_tls_send/receive` 目前不发网络包）；
3. 之后才在 `net64_http.c` 加 https 路由。

**验收**：能对一个真实服务器完成握手（`openssl s_server` 做对端即可），拿到可解密的应用数据。

---

## P3-6 64 位 VFS 抽象层

**现状**：已分派给"文档/epoll/VFS"线在做（`kernel/fs/vfs64.c` + ext2 后端 + 只读伪设备验收）。本文不重复。

---

## P3-1 兼容层（Windows / Android / Mach-O）

**现状**：Linux 兼容层按进程 `env_type` 已做（P1-2）；Windows 只有 `gen_winhello.py` 生成器，Mach-O 只有 `gen_macho.py`，**都没有加载器**。

**为什么做不了（短期）**：这是"另一个操作系统级工程" —— PE 加载器要处理导入表/TLS 回调/SEH，Android 要 ART 运行时或至少 Houdini 式转译，Mach-O 要处理 dyld 链。不是补一个文件能完成的。

**可先做的切面**：只做"PE 最小加载器"（能加载一个无导入、无 TLS 的 EXE 并跑 `main`），验证 `kernel/shared/compat/` 的目录结构与 env_type 判定是否够用。这个切面有明确验收，不需要全量兼容。
