---
title: RUST
status: active
sys_version: 0.54.12
updated: 2026-11-01
owner: planning
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# 赫普OS Rust用户态开发指南

## 概述

赫普OS采用 **C语言（核心内核）+ 汇编（最底层）+ Rust（用户态服务，逐步引入）** 的技术路线。

本文档介绍如何在赫普OS上开发和运行Rust用户态应用。

## 技术栈

| 组件 | 版本/规格 |
|------|-----------|
| Rust工具链 | nightly 1.100.0+ |
| 目标架构 | i686 (x86 32位) |
| 自定义目标 | `hepu-i686.json` |
| 链接器 | rust-lld (LLVM) |
| 系统调用ABI | Linux i386 (int 0x80) |
| ELF加载地址 | 0x08048000 |
| 用户栈地址 | 0x40010000 |

## 环境搭建

### 1. 安装Rust nightly工具链

使用国内镜像源（中科大）：

```bash
# 设置环境变量
export RUSTUP_DIST_SERVER=https://mirrors.ustc.edu.cn/rust-static
export RUSTUP_UPDATE_ROOT=https://mirrors.ustc.edu.cn/rust-static/rustup

# 安装nightly
curl --proto '=https' --tlsv1.2 -sSf https://mirrors.ustc.edu.cn/rust-static/rustup/sh | sh -s -- --default-toolchain nightly

# 安装组件
rustup component add rust-src llvm-tools-preview
```

### 2. 配置cargo国内镜像

`~/.cargo/config.toml`:

```toml
[source.crates-io]
replace-with = 'ustc'

[source.ustc]
registry = "sparse+https://mirrors.ustc.edu.cn/crates.io-index/"
```

### 3. 项目工具链配置

项目根目录 `rust-toolchain.toml`:

```toml
[toolchain]
channel = "nightly"
components = ["rust-src", "llvm-tools-preview"]
```

## 项目结构

```
user/rust/
├── Cargo.toml              # 工作区配置
├── hepu-i686.json          # 自定义目标JSON
├── hepu-i686.ld            # 链接脚本
├── hepu-sys/               # 系统调用绑定库
│   ├── Cargo.toml
│   └── src/lib.rs
├── hello-rust/             # 示例应用
│   ├── Cargo.toml
│   └── src/main.rs
└── target/                 # 构建产物
```

## 自定义目标说明

### hepu-i686.json

```json
{
  "llvm-target": "i686-unknown-none",
  "data-layout": "e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-f64:32:64-f80:32-n8:16:32-S128",
  "arch": "x86",
  "os": "none",
  "env": "",
  "vendor": "unknown",
  "linker": "rust-lld",
  "linker-flavor": "ld.lld",
  "pre-link-args": {
    "ld.lld": ["--image-base=0x08048000", "-Thepu-i686.ld"]
  },
  "features": "+soft-float",
  "panic-strategy": "abort",
  "executables": true,
  "relocation-model": "static",
  "code-model": "small",
  "disable-redzone": true,
  "eliminate-frame-pointer": false
}
```

**关键参数**：
- `--image-base=0x08048000`：ELF加载基址，与Linux兼容
- `os: "none"`：无操作系统，no_std环境
- `panic-strategy: "abort"`：panic时直接abort

### 系统调用ABI

Rust应用编译为ELF格式，被赫普OS识别为Linux兼容进程（`compat_abi==1`）。`int 0x80`的调用号会被内核从Linux号翻译为HEPU号（`linux_abi_translate`）。

**必须使用Linux i386系统调用号，不能使用HEPU原生号！**

常用Linux i386系统调用号：

| 调用号 | 名称 | 参数 |
|--------|------|------|
| 1 | exit | ebx=exit_code |
| 3 | read | ebx=fd, ecx=buf, edx=count |
| 4 | write | ebx=fd, ecx=buf, edx=count |
| 5 | open | ebx=path, ecx=flags, edx=mode |
| 6 | close | ebx=fd |
| 13 | time | ebx=tloc |
| 20 | getpid | - |
| 45 | brk | ebx=brk_addr |
| 158 | sched_yield | - |
| 162 | nanosleep | ebx=req, ecx=rem |

系统调用约定：`eax=调用号, ebx/ecx/edx/esi=参数, int 0x80, 返回值在eax`。

## hepu-sys库

`hepu-sys`是Rust用户态应用的系统调用绑定库，提供：

### 核心功能

- **系统调用封装**：`syscall0/1/2/3/4`原始系统调用
- **进程管理**：`exit()`, `getpid()`, `yield_cpu()`
- **IO操作**：`print()`, `read()`, `write()`, `open()`, `close()`
- **内存管理**：基于`brk`的简单分配器（`HepuAlloc`）
- **时间操作**：`time()`, `sleep_ms()`
- **Panic处理**：自定义panic handler，输出崩溃信息到stdout

### i686内联汇编注意事项

i686上LLVM内部使用`esi`寄存器，不能直接作为内联汇编操作数。需要用`xchg`技巧：

```rust
unsafe fn syscall4(num: u32, arg1: u32, arg2: u32, arg3: u32, arg4: u32) -> u32 {
    let ret: u32;
    core::arch::asm!(
        "xchg {arg4:e}, esi",  // 把arg4放到esi
        "int 0x80",
        "xchg {arg4:e}, esi",  // 恢复esi
        in("eax") num,
        in("ebx") arg1,
        in("ecx") arg2,
        in("edx") arg3,
        arg4 = in(reg) arg4,
        lateout("eax") ret,
        options(nostack),
    );
    ret
}
```

## 开发第一个Rust应用

### 1. 创建应用目录

```bash
mkdir -p user/rust/myapp/src
```

### 2. Cargo.toml

```toml
[package]
name = "myapp"
version = "0.1.0"
edition = "2021"

[dependencies]
hepu-sys = { path = "../hepu-sys" }
```

### 3. src/main.rs

```rust
#![no_std]
#![no_main]

use hepu_sys as hepu;

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let code = main();
    hepu::exit(code);
}

#[no_mangle]
pub extern "C" fn main() -> i32 {
    hepu::print("Hello from my Rust app!\n");
    0
}
```

### 4. 编译

```bash
cd user/rust
cargo build -Z build-std=core,alloc \
  -Z build-std-features=compiler-builtins-mem \
  -Z json-target-spec \
  --target ./hepu-i686.json
```

产物：`user/rust/target/hepu-i686/debug/myapp`

### 5. 集成到initrd

将编译产物复制到`build_v250/myapp.elf`，并在Makefile的initrd依赖列表中添加。

或者直接在Makefile中添加构建规则（参考hello-rust.elf）。

## Makefile集成

在项目根目录运行`make`即可自动构建Rust应用：

```makefile
$(BUILD_DIR)/hello-rust.elf: user/rust/hello-rust/src/main.rs user/rust/hepu-sys/src/lib.rs $(RUST_TARGET)
	@echo "  CARGO hello-rust"
	@cd user/rust && cargo build -Z build-std=core,alloc -Z build-std-features=compiler-builtins-mem -Z json-target-spec --target ./hepu-i686.json 2>&1 | tail -1
	@cp $(RUST_BUILD_DIR)/hello-rust $@
```

## 运行与调试

### QEMU运行

```bash
qemu-system-i386 -m 256M -nic none -vga std \
  -drive file=build/hepu-os-v1.02.img,format=raw,if=ide,snapshot=on \
  -drive file=build/ext2_disk.img,format=raw,if=ide,snapshot=on \
  -boot order=c -serial file:/tmp/serial.log
```

在Shell中输入：
```
run hello-rust
```

### 调试技巧

1. **串口输出**：应用的`print()`输出会同时显示在VGA和串口（COM1, 0x3F8）
2. **反汇编**：`objdump -d target/hepu-i686/debug/hello-rust`
3. **ELF信息**：`readelf -a target/hepu-i686/debug/hello-rust`
4. **内核诊断**：可在`binfmt_elf.c`中临时添加`serial_puts`诊断（注意栈平衡！）

## 常见问题

### Q1: 应用无输出

**可能原因**：
- `/dev/console`节点丢失（根文件系统挂载后）→ 已修复，binfmt_elf会自动重新创建
- write系统调用号错误 → 必须用Linux号（write=4），不能用HEPU原生号

### Q2: Invalid Opcode异常

**可能原因**：
- 系统调用号错误，调用了不存在的系统调用
- 应用入口点错误，检查ELF entry是否为0x08048000

### Q3: General Protection Fault

**可能原因**：
- 栈未正确设置，检查用户栈地址0x40010000
- 段寄存器设置错误

### Q4: 编译错误：target not found

i686-unknown-none不是官方目标，必须使用自定义目标JSON：
```bash
cargo build --target ./hepu-i686.json -Z json-target-spec
```

### Q5: 链接错误：compiler_builtins缺少memcmp等函数

添加编译标志：
```bash
-Z build-std-features=compiler-builtins-mem
```

### Q6: 链接错误：-Ttext-segment不支持

rust-lld不支持`-Ttext-segment`，使用`--image-base=0x08048000`代替。

## 渐进式迁移路线图

### 第一阶段：验证（已完成）
- ✅ Rust工具链安装
- ✅ 自定义目标JSON和链接脚本
- ✅ hepu-sys系统调用绑定库
- ✅ hello-rust示例应用成功运行
- ✅ Makefile集成

### 第二阶段：完善hepu-sys库
- 完善内存分配器（支持free、realloc）
- 实现nanosleep（当前用yield循环代替）
- 添加更多系统调用封装（mmap, ioctl, fcntl等）
- 实现简单的字符串处理库
- 添加单元测试

### 第三阶段：更多示例应用
- 文件读写示例
- 网络通信示例（需网络栈支持）
- 多线程示例（需线程支持）
- 简单的Shell工具（cat, echo, ls等）

### 第四阶段：用户态服务Rust化
- 文件服务（file-server）
- 网络服务（net-server）
- 图形合成服务（graphics-server）
- AI运行时（ai-runtime）
- 隐私数据金库（privacy-vault）

## 参考资料

- [Rust Embedded Book](https://docs.rust-embedded.org/book/)
- [Writing an OS in Rust](https://os.phil-opp.com/)
- [Redox OS](https://www.redox-os.org/)（Rust编写的操作系统参考）
- [Linux i386 syscall table](https://syscalls.w3challs.com/?arch=x86)

## V6.60 更新 (2026-09-23)

### 已完成
1. **hepu-sys库完善**
   - 新增系统调用封装：lseek, unlink, rename, mkdir, rmdir, chdir, access, dup, pipe, getcwd, truncate, ftruncate, kill, sysinfo
   - 新增Seek和AccessMode常量模块
   - 内存分配器：alloc/dealloc已实现，支持空闲块合并

2. **Rust示例应用扩展（3个）**
   - `hello-rust`：brk内存分配测试，验证Rust no_std环境
   - `cat`：文件读写测试，验证open/read/write/close/lseek/access
   - `echo`：综合测试，验证write/heap分配/getpid/time

3. **关键bug修复**
   - BUG-17: boot.S中multiboot magic被BSS清零破坏（用0x8000/0x8004固定地址保存）
   - BUG-18: 内核BSS段膨胀到108MB导致可用内存为0（减小编译期数组+动态堆大小）

### 验证结果
- 所有3个Rust程序在UEFI引导下运行成功（rc=0）
- 内核自测全部PASS（BUDDY/VMA/ZRAM/HUGETLB/VRAM/IOSCHED/JOURNAL/CRYPTO/SNAPSHOT/UMEM/BLK-MQ/DMA/IDE/EXT2/SEC/PERM）
- 所有用户态程序运行成功（Linux ELF/PE32/Mach-O/Rust）

### 下一步
- P1: Rust文件服务试点（第一个Rust用户态服务）
- P1: BSD Socket API封装
- P2: Rust标准库基础函数扩展（strtol/atoi/sprintf等）

## V6.61 更新 (2026-09-23)

### 已完成
1. **hepu-sys库标准库函数扩展**
   - 字符串函数：strcmp, strncmp, strcpy, strncpy, strcat, strchr, strrchr, strstr
   - 数字转换：atoi, atol, strtol, strtoul, itoa, hextoa
   - 格式化输出：snprintf（支持%s/%d/%x/%c/%%）
   - FmtArg枚举用于格式化参数传递

2. **Rust示例应用扩展（第4个）**
   - `sysinfo`：系统信息工具，验证sysinfo系统调用、getcwd、字符串函数、格式化输出

3. **验证结果**
   - 所有4个Rust程序运行成功（hello-rust/cat/echo/sysinfo，全部rc=0）
   - 内核自测全部PASS
   - 所有用户态程序运行成功（共12个，含Linux ELF/PE32/Mach-O/Rust）

### Rust程序清单
| 程序 | 大小 | 验证功能 |
|------|------|----------|
| hello-rust | 33KB | brk内存分配、Rust no_std环境 |
| cat | 34KB | open/read/write/close/lseek/access |
| echo | 37KB | write/heap分配/getpid/time |
| sysinfo | 36KB | sysinfo/getcwd/字符串函数/格式化输出 |

### 下一步
- P1: Rust文件服务试点（第一个Rust用户态服务，通过IPC提供文件操作）
- P1: BSD Socket API封装
- P2: Rust标准库继续扩展（浮点支持、更多格式化选项）

## V6.62 更新 (2026-09-23) — 里程碑：第一个Rust用户态服务

### 重大突破：Rust文件服务（file-server）
- **第一个真正的Rust用户态服务**，通过IPC提供文件操作
- 服务主循环：接收IPC消息 → 解析请求 → 执行文件操作 → 返回响应
- 支持的消息类型：FILE_OPEN, FILE_READ, FILE_WRITE, FILE_CLOSE
- 处理10个请求后自动退出（测试模式）

### 内核侧修改
- Linux ABI翻译表新增IPC系统调用映射：
  - Linux 202 → SYS_IPC_SEND
  - Linux 203 → SYS_IPC_RECV
- 使Rust程序（Linux ELF兼容）可以调用赫普OS的IPC机制

### hepu-sys库新增IPC模块
- `IpcMessage`结构：与内核`ipc_message_t`对应（sender_pid/type/priority/length/data[256]）
- `ipc_send(target_pid, msg_type, data)`：发送IPC消息
- `ipc_recv()`：接收IPC消息（阻塞，返回Option<IpcMessage>）
- `IpcType`常量模块：FILE_OPEN=100, FILE_READ=101, FILE_WRITE=102, FILE_CLOSE=103, RESPONSE=200, ERROR=201

### Rust程序清单（5个）
| 程序 | 大小 | 类型 | 验证功能 |
|------|------|------|----------|
| hello-rust | 33KB | 工具 | brk内存分配、Rust no_std环境 |
| cat | 34KB | 工具 | open/read/write/close/lseek/access |
| echo | 37KB | 工具 | write/heap分配/getpid/time |
| sysinfo | 36KB | 工具 | sysinfo/getcwd/字符串函数/格式化输出 |
| **file-server** | **35KB** | **服务** | **IPC消息循环、文件操作服务** |

### 验证结果
- 所有4个Rust工具程序运行成功（rc=0）
- 内核自测全部PASS
- 所有12个用户态程序运行成功
- file-server已加入initrd，可手动启动测试

### 微内核架构意义
file-server的成功创建标志着赫普OS向微内核架构迈出了关键一步：
- 文件系统操作从内核移到用户态服务
- 服务崩溃不影响内核稳定性
- 可以用Rust等内存安全语言开发服务
- 通过IPC进行进程间通信

### 下一步
- P1: 完善file-server（支持更多文件操作、错误处理、并发请求）
- P1: 创建Rust文件服务客户端库
- P1: BSD Socket API封装
- P2: Rust网络服务试点

## V6.63 更新 (2026-09-23) — 文件服务完善+客户端库

### file-server v2 完善
- 新增文件操作：FILE_STAT, FILE_MKDIR, FILE_UNLINK, FILE_LSEEK
- 完善请求/响应协议：统一响应格式[ret_code:i32][extra_data]
- 错误处理：独立ERROR消息类型，错误计数统计
- 请求上限从10提升到20
- 辅助函数：read_u32/read_i32/extract_str/send_response/send_error

### 新增：fs-client（文件服务客户端库）
- `FileServiceClient`结构体：封装IPC通信
- 方法：open/read/close/mkdir/unlink
- 内部实现：request()方法统一处理"发送请求→等待响应→解析结果"
- 超时机制：最多等待100次ipc_recv，避免永久阻塞
- 测试程序：验证IPC消息结构、类型常量、客户端实例化、直接文件操作对比

### hepu-sys IPC扩展
- IpcType新增：FILE_MKDIR=105, FILE_UNLINK=106, FILE_LSEEK=107

### Rust程序清单（6个）
| 程序 | 大小 | 类型 | 验证功能 |
|------|------|------|----------|
| hello-rust | 33KB | 工具 | brk内存分配、Rust no_std环境 |
| cat | 34KB | 工具 | open/read/write/close/lseek/access |
| echo | 37KB | 工具 | write/heap分配/getpid/time |
| sysinfo | 36KB | 工具 | sysinfo/getcwd/字符串函数/格式化输出 |
| file-server | 39KB | 服务 | IPC消息循环、8种文件操作服务 |
| fs-client | 34KB | 客户端 | IPC客户端库、服务调用封装 |

### 微内核架构进展
- ✅ 用户态服务进程（file-server）
- ✅ 客户端库封装（fs-client）
- ✅ IPC请求/响应协议
- ⏳ 服务发现机制（如何获取server_pid）
- ⏳ 多客户端并发支持
- ⏳ 服务注册/注销机制

### 验证结果
- 所有5个Rust工具程序运行成功（rc=0）
- fs-client测试通过（IPC结构验证+直接文件操作对比）
- 内核自测全部PASS
- 所有13个用户态程序运行成功

### 下一步
- P1: 实现服务发现机制（服务注册表，通过PID或名称查找服务）
- P1: BSD Socket API封装
- P2: Rust网络服务试点
- P2: file-server并发请求处理

## V6.64 修复：系统调用返回值传递BUG (BUG-19)

### 问题
Rust用户态程序调用service_register(204)和service_lookup(205)时，返回值始终是系统调用号本身(204/205)，而不是内核函数的返回值。

### 根因
`kernel/arch/x86/interrupt.S`中isr128的返回值写入指令偏移量错误：
```asm
mov %eax, 36(%esp)   ; 错误！应该是32(%esp)
```

原偏移量36将返回值写入了eax槽后4字节(ecx槽)，导致eax槽未被更新，仍保持系统调用号。

### 修复
将偏移量从36改为32：
```asm
mov %eax, 32(%esp)   ; 正确
```

### 验证
- service_register返回0(成功) ✓
- service_lookup返回PID(14) ✓
- 所有13个用户态程序rc=0 ✓
- 所有自测PASS ✓

### 服务发现机制状态
- ✅ service_register: 注册服务到内核服务表(最多16个服务)
- ✅ service_lookup: 通过服务名查找服务PID
- ✅ 返回值传递正确
- ⏳ 下一步: file-server启动时自动注册，fs-client通过服务名自动连接

## V6.65 更新 (2026-09-23) — 端到端IPC测试+fork修复尝试

### 新增：ipc-e2e-test程序
- 自包含的IPC集成测试（6项测试）
- 验证服务注册、服务发现、IPC发送/接收、文件操作协议
- 测试结果：服务注册/发现/发送正常，接收需进一步调试

### hepu-sys库扩展
- 新增fork()系统调用封装（Linux fork=2）
- 新增waitpid()系统调用封装（Linux waitpid=7）
- 修复syscall_inline编译错误（LLVM不允许esi寄存器，改用syscall4_asm）

### fork修复尝试（BUG-20）
- **问题**：fork后子进程立即崩溃（GPF at eip=0x0000202B）
- **已尝试**：
  1. 为子进程创建独立页目录（paging_create_dir）
  2. 复制父进程用户空间映射（PDE 0-767）
  3. 映射子进程内核栈到新页目录
  4. 分配独立用户栈并复制内容
- **结果**：子进程仍然崩溃，内核栈复制或返回地址有问题
- **临时方案**：端到端IPC测试改用自包含模式

### Rust程序清单（7个）
| 程序 | 大小 | 类型 | 验证功能 |
|------|------|------|----------|
| hello-rust | 33KB | 工具 | brk内存分配、Rust no_std环境 |
| cat | 34KB | 工具 | open/read/write/close/lseek/access |
| echo | 37KB | 工具 | write/heap分配/getpid/time |
| sysinfo | 36KB | 工具 | sysinfo/getcwd/字符串函数/格式化输出 |
| file-server | 39KB | 服务 | IPC消息循环、8种文件操作服务 |
| fs-client | 34KB | 客户端 | IPC客户端库、服务调用封装 |
| ipc-e2e-test | ~40KB | 测试 | 端到端IPC集成测试（自包含） |

### 微内核架构进展
- ✅ 用户态服务进程（file-server）
- ✅ 客户端库封装（fs-client）
- ✅ IPC请求/响应协议
- ✅ 服务发现机制（service_register/lookup）
- ✅ 服务注册返回值正确（BUG-19修复）
- ⏳ 跨进程端到端IPC验证（依赖fork修复，BUG-20）
- ⏳ 多客户端并发支持
- ⏳ BSD Socket API封装

### 下一步
- P1: 调试ipc_recv消息内容问题
- P1: 修复fork子进程崩溃（BUG-20）
- P1: BSD Socket API封装
- P2: Rust网络服务试点

## V6.66 更新 (2026-09-23) — IPC修复成功+fork调试进展

### 重大修复: ipc_send接口修复
- **问题**: Rust侧ipc_send传递整个IpcMessage结构指针给内核，但内核sys_ipc_send期望的是(data_ptr, length)
- **根因**: 内核会把整个IpcMessage结构memcpy到msg->data中，导致接收方data内容错误
- **修复**: Rust侧ipc_send改为直接传递data.as_ptr()和data.len()，不再构造IpcMessage结构
- **验证**: ipc-e2e-test 6项测试全部PASS：
  - Test 1: 服务注册 (service_register返回0)
  - Test 2: 服务发现 (service_lookup返回PID=15)
  - Test 3: IPC发送 (ipc_send返回0)
  - Test 4: IPC接收 (received: type=100 sender=15 len=10, path: hello ipc)
  - Test 5: 文件操作协议 (open请求/响应正常)
  - Test 6: IpcMessage结构大小 (272字节)

### fork调试进展 (BUG-20)
- **已修复**: isr128入口保存current->kernel_esp（原只在调度切换时更新）
- **已修复**: 内核栈映射使用paging_get_phys获取正确物理地址
- **已修复**: 添加VGA文本显存映射
- **仍存在**: 子进程GPF at eip=0x00006005，iret弹出的eip被破坏
- **重要发现**: UEFI测试必须用`-drive IMG`启动，不能用`-cdrom ISO`（否则加载旧内核）

### Rust程序清单（8个）
| 程序 | 大小 | 类型 | 验证功能 |
|------|------|------|----------|
| hello-rust | 33KB | 工具 | brk内存分配、Rust no_std环境 |
| cat | 34KB | 工具 | open/read/write/close/lseek/access |
| echo | 37KB | 工具 | write/heap分配/getpid/time |
| sysinfo | 36KB | 工具 | sysinfo/getcwd/字符串函数/格式化输出 |
| file-server | 39KB | 服务 | IPC消息循环、8种文件操作服务 |
| fs-client | 34KB | 客户端 | IPC客户端库、服务调用封装 |
| ipc-e2e-test | ~40KB | 测试 | 端到端IPC集成测试（6项全PASS） |
| fork-test | ~34KB | 测试 | fork+waitpid验证（子进程崩溃，BUG-20） |

### 下一步优先级
1. P0: 修复fork子进程崩溃（BUG-20）—— 检查内核栈复制和eip
2. P0: BSD Socket API封装
3. P1: file-server启动时自动注册服务，fs-client通过服务名自动连接
4. P1: Rust网络服务试点

## V6.69 更新 (2026-09-24) — BSD Socket API封装完成

### 完成: BSD Socket API封装
- **内核侧**:
  - 新增SYS_SOCKETCALL=208系统调用（Linux socketcall多路复用）
  - Linux ABI映射: socketcall(102)→SYS_SOCKETCALL(208)
  - 支持子调用: SOCKET(1)/BIND(2)/CONNECT(3)/LISTEN(4)/ACCEPT(5)/SEND(9)/RECV(10)
  - 修复loopback连接对端关联（BUG-24）:
    - connect时创建服务器端连接socket，设置双向peer_fd
    - accept返回connect时创建的服务器端连接socket
    - 新增is_server_side和listen_fd字段

- **Rust侧 (hepu-sys库)**:
  - AddressFamily (AF_INET=2/AF_UNIX=1)
  - SocketType (SOCK_STREAM=1/SOCK_DGRAM=2)
  - Protocol (IPPROTO_TCP=6/IPPROTO_UDP=17)
  - SockAddrIn结构体（new()/loopback()，自动网络字节序转换）
  - socket()/bind()/listen()/accept()/connect()/send()/recv()/close_socket()

- **socket-test程序** (第10个Rust程序):
  - 8项测试全部PASSED:
    1. socket创建 - PASSED
    2. bind - PASSED
    3. listen - PASSED
    4. connect (loopback) - PASSED
    5. accept - PASSED (返回服务器端连接socket)
    6. send - PASSED (14字节)
    7. recv - PASSED (收到"Hello, Socket!"，数据匹配)
    8. 双向通信 - PASSED (服务器回复正确接收)

### Rust程序清单（10个）
| 程序 | 大小 | 类型 | 验证功能 |
|------|------|------|----------|
| hello-rust | 33KB | 工具 | brk内存分配、Rust no_std环境 |
| cat | 34KB | 工具 | open/read/write/close/lseek/access |
| echo | 37KB | 工具 | write/heap分配/getpid/time |
| sysinfo | 36KB | 工具 | sysinfo/getcwd/字符串函数/格式化输出 |
| file-server | 39KB | 服务 | IPC消息循环、8种文件操作服务 |
| fs-client | 34KB | 客户端 | IPC客户端库、服务调用封装 |
| ipc-e2e-test | ~40KB | 测试 | 端到端IPC集成测试（6项全PASS） |
| fork-test | ~34KB | 测试 | fork+waitpid验证（子进程exit(42)，PASSED） |
| file-server-e2e-test | ~45KB | 测试 | 跨进程文件服务端到端测试 |
| socket-test | 36KB | 测试 | BSD Socket API loopback测试（8项全PASS） |

### 下一步优先级
1. P1: 修复BUG-23调度器子进程延迟调度
2. P1: file-server-e2e-test端到端验证（依赖BUG-23）
3. P2: Rust网络服务试点（基于Socket API）
4. P2: UDP支持和非阻塞IO

---

## V6.82: Rust net-server网络服务（首个Rust网络服务）

### 概述
net-server是赫普OS首个Rust用户态网络服务，实现了HTTP服务器功能。

### 功能
- TCP监听端口8080（0.0.0.0）
- 处理HTTP GET请求
- 返回HTML响应（200 OK / 404 Not Found）
- 支持多连接循环（最大100连接）
- 注册到服务注册表（service_register）

### 技术实现
- 使用hepu-sys的BSD Socket API封装
- socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
- bind/listen/accept/send/recv完整流程
- SockAddrIn::new(0, 8080) = INADDR_ANY:8080

### 文件位置
- user/rust/net-server/（Cargo项目）
- user/rust/net-server/src/main.rs（约150行）

### 验证
- socket-test全部通过：socket/bind/listen/accept/send/recv
- net-server编译成功（17KB）
- 已加入ext2数据盘/app目录

### 下一步
- 添加HTTP POST/PUT/DELETE支持
- 静态文件服务
- 多线程/多进程并发处理
- 与file-server集成提供文件下载
