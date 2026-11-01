# 赫普OS 开发者指南

> 版本：V8.78 | 更新日期：2026-10-06

## 目录

1. [项目简介](#项目简介)
2. [开发环境搭建](#开发环境搭建)
3. [获取源码](#获取源码)
4. [编译构建](#编译构建)
5. [运行测试](#运行测试)
6. [调试技巧](#调试技巧)
7. [项目结构](#项目结构)
8. [编码规范](#编码规范)
9. [系统调用开发](#系统调用开发)
10. [驱动开发](#驱动开发)
11. [应用开发](#应用开发)
12. [常见问题](#常见问题)

---

## 项目简介

赫普OS（Hepu OS）是一个自研微内核操作系统，采用C语言核心内核+汇编最底层+Rust用户态渐进引入的技术路线。

### 核心特性

- **微内核架构**：内核仅包含调度、内存管理、IPC等核心功能
- **自研引导器**：HepuBoot支持UEFI和传统BIOS
- **三模式显示降级**：文本模式→VGA→VBE
- **Secure Boot**：支持安全启动签名验证
- **图形子系统**：可插拔架构，支持GOP/VGA/VBE/virtio-gpu
- **网络协议栈**：完整TCP/IP协议栈+TLS 1.2
- **文件系统**：ext2/FAT32/VFS抽象层
- **安全机制**：内存隔离、系统调用校验、加密存储

### 当前版本

- 内核版本：V8.78
- 引导器版本：HepuBoot v0.09
- 支持架构：x86（32位）、x86_64（框架）
- 内核大小：约4.1MB

---

## 开发环境搭建

### 必需工具

```bash
# 基础编译工具
sudo apt install build-essential gcc-multilib nasm

# 交叉编译工具链
sudo apt install gcc-i686-linux-gnu binutils-i686-linux-gnu

# UEFI开发
sudo apt install gnu-efi sbsigntool

# 虚拟机测试
sudo apt install qemu-system-x86 ovmf

# 磁盘工具
sudo apt install sgdisk mtools dosfstools

# 其他工具
sudo apt install git make python3 zip
```

### 国内镜像源（推荐）

```bash
# 阿里云apt源
sudo sed -i 's|archive.ubuntu.com|mirrors.aliyun.com|g' /etc/apt/sources.list
sudo apt update
```

### Rust工具链（用户态开发）

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
rustup target add i686-unknown-linux-gnu
```

---

## 获取源码

```bash
git clone <repository-url> hepu-os
cd hepu-os
```

### 目录结构

```
hepu-os/
├── kernel/                 # 内核源码
│   ├── arch/x86/          # x86架构相关
│   ├── arch/x86_64/       # x86_64架构相关
│   ├── mm/                # 内存管理
│   ├── fs/                # 文件系统
│   ├── net/               # 网络协议栈
│   ├── drivers/           # 设备驱动
│   ├── gfx/               # 图形子系统
│   ├── crypto/            # 加密算法
│   ├── ipc/               # 进程间通信
│   ├── sched/             # 调度器
│   ├── games/             # 内置游戏
│   └── include/           # 头文件
├── user/                   # 用户态
│   ├── apps/              # 用户应用
│   ├── lib/               # 用户库
│   ├── ldso/              # 动态链接器
│   └── rust/              # Rust用户态程序
├── tools/                  # 工具
│   └── hepboot/           # HepuBoot引导器
├── docs/                   # 文档
├── build_v250/             # 构建输出
└── Makefile               # 构建脚本
```

---

## 编译构建

### 完整编译

```bash
make
```

### 清理构建

```bash
make clean
```

### 仅编译内核

```bash
make kernel
```

### 编译引导器

```bash
cd tools/hepboot/uefi
make
```

### 编译Rust用户态程序

```bash
cd user/rust
cargo build --release
```

### 构建输出

编译成功后生成：
- `build_v250/hepu-kernel.bin` - 内核二进制（约4.1MB）

---

## 运行测试

### QEMU虚拟机运行

```bash
# UEFI模式
qemu-system-x86_64 \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -drive format=raw,file=build/hepu-os-uefi-v0.08.img \
  -m 512M \
  -vga std \
  -net nic,model=rtl8139 -net user

# 传统BIOS模式
qemu-system-x86_64 \
  -drive format=raw,file=build/hepu-os-uefi-v0.08.img \
  -m 512M \
  -vga std
```

### 真机测试

1. 将IMG镜像写入U盘：
```bash
sudo dd if=build/hepu-os-uefi-v0.08.img of=/dev/sdX bs=4M status=progress
```

2. 从U盘启动，在BIOS中选择UEFI启动

### Secure Boot测试

```bash
# 签名引导器
cd tools/hepboot/uefi
./sign_bootloader.sh

# 生成密钥（首次）
cd secureboot
./generate_keys.sh
```

---

## 调试技巧

### 串口调试

```bash
# QEMU添加串口输出
qemu-system-x86_64 ... -serial stdio

# 内核中使用
vga_printf("调试信息: %d\n", value);
```

### GDB远程调试

```bash
# QEMU启动GDB服务器
qemu-system-x86_64 ... -s -S

# 另一个终端连接
gdb build_v250/hepu-kernel.bin
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

### 日志级别

内核支持多级日志：
- `vga_puts()` - 普通输出
- `vga_printf()` - 格式化输出
- 错误前缀：`[ERROR]`、`[WARN]`、`[INFO]`、`[DEBUG]`

### 常见调试宏

```c
#define DEBUG_PRINT(fmt, ...) \
    vga_printf("[DEBUG %s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
```

---

## 编码规范

### C语言规范

1. **缩进**：4个空格，不使用Tab
2. **命名**：
   - 函数：`snake_case`（如 `mm_init`）
   - 变量：`snake_case`（如 `page_count`）
   - 宏：`UPPER_SNAKE_CASE`（如 `PAGE_SIZE`）
   - 类型：`snake_case_t`（如 `process_t`）
3. **注释**：函数头使用 `/* */` 块注释，行内使用 `//`
4. **行宽**：建议不超过100字符
5. **括号**：函数大括号另起一行，控制语句同行

### 函数头注释模板

```c
/* ============================================================================
 * 函数名称
 *
 * 描述：函数功能描述
 *
 * 参数：
 *   param1 - 参数说明
 *   param2 - 参数说明
 *
 * 返回值：
 *   0 - 成功
 *   -1 - 失败
 * ========================================================================== */
```

### 提交信息规范

```
<类型>(<范围>): <简短描述>

类型：feat(新功能)、fix(修复)、docs(文档)、refactor(重构)、test(测试)
范围：kernel、fs、net、drivers、gfx、boot、docs

示例：
feat(kernel): 添加系统调用 SYS_foo
fix(fs): 修复ext2挂载时的块大小校验
docs(docs): 更新开发者指南V8.78
```

---

## 系统调用开发

### 添加新系统调用步骤

1. **分配系统调用号**：编辑 `kernel/include/constants.h` 和 `kernel/include/syscall.h`
2. **声明函数**：在对应头文件中声明
3. **实现函数**：在 `kernel/arch/x86/sys_*.c` 中实现
4. **注册到系统调用表**：编辑 `kernel/arch/x86/syscall.c`
5. **用户态封装**：在 `user/lib/hepu/syscall.c` 中添加封装
6. **更新文档**：在 `docs/API_REFERENCE.md` 中记录

### 示例

```c
// constants.h
#define SYS_foo 221
#define SYS_MAX 222

// syscall.h
int sys_foo(int arg);

// sys_foo.c
int sys_foo(int arg) {
    // 参数校验
    if (arg < 0) return -EINVAL;
    // 实现逻辑
    return 0;
}

// syscall.c
syscall_table[SYS_foo] = sys_foo;
```

### 安全要求

- 所有用户指针必须使用 `copy_from_user` / `copy_to_user`
- 数组索引必须检查边界
- 内存分配必须检查返回值
- 锁操作必须配对（加锁/解锁）

---

## 驱动开发

### 驱动框架

赫普OS采用统一驱动框架：

```c
// 驱动结构
typedef struct {
    const char *name;
    int (*init)(void);
    int (*probe)(pci_device_t *dev);
    int (*remove)(void);
    int (*suspend)(void);
    int (*resume)(void);
} driver_t;

// 注册驱动
driver_register(&my_driver);
```

### PCI驱动开发

1. 实现 `probe` 函数检测设备
2. 实现 `init` 函数初始化硬件
3. 注册中断处理函数
4. 实现读写接口

### 字符设备

```c
// 注册字符设备
int dev_register(const char *name, dev_ops_t *ops);

// 设备操作
typedef struct {
    int (*open)(void);
    int (*close)(void);
    int (*read)(void *buf, int count);
    int (*write)(const void *buf, int count);
    int (*ioctl)(int cmd, void *arg);
} dev_ops_t;
```

---

## 应用开发

### 系统应用

系统应用位于 `kernel/arch/x86/`，直接编译进内核：

```c
// 应用入口
void my_app_init(void);
void my_app_draw(int x, int y, int w, int h);
void my_app_handle_key(char key);
```

### 用户态应用

用户态应用位于 `user/apps/`，编译为独立可执行文件：

```c
#include <hepu.h>

int main(int argc, char **argv) {
    printf("Hello, Hepu OS!\n");
    return 0;
}
```

### Rust用户态应用

```rust
// user/rust/src/bin/myapp.rs
fn main() {
    println!("Hello from Rust!");
}
```

### 图形应用开发

使用图形子系统API：

```c
#include <graphics.h>

void draw() {
    gfx_init(800, 600);
    gfx_fill_rect(0, 0, 800, 600, (gfx_color_t){30, 30, 40, 255});
    gfx_draw_string_font(10, 10, "Hello", (gfx_color_t){255, 255, 255, 255}, 16);
}
```

---

## 常见问题

### Q: 编译报错 "undefined reference to ..."

A: 检查是否在Makefile中添加了新的源文件，以及函数声明是否正确。

### Q: QEMU启动黑屏

A: 检查：
1. 是否使用正确的BIOS（UEFI用OVMF）
2. 内核是否正确写入镜像
3. 引导器是否签名（Secure Boot环境）

### Q: 真机无法启动

A: 检查：
1. BIOS是否启用UEFI模式
2. Secure Boot是否需要签名
3. U盘是否使用GPT分区表
4. 尝试传统BIOS模式

### Q: 内核大小超过4MB

A: 引导器已支持最大16MB内核，检查引导器版本是否为v0.09+。

### Q: 中文显示乱码

A: 确保使用DroidSansFallbackFull字体（TrueType格式），Noto Sans CJK是CFF格式不支持。

### Q: 网络不通

A: 检查：
1. QEMU网卡模型（推荐rtl8139）
2. 驱动是否加载
3. IP配置（DHCP或静态）

---

## 参考资源

- [API参考文档](API_REFERENCE_V8.05.md)
- [系统架构设计](SYSTEM_ARCHITECTURE_V8.40.md)
- [开发路线图](DEVELOPMENT_ROADMAP.md)
- [错误历史](BOOTLOADER_BUG_HISTORY.md)
- [安全审计报告](SECURITY_AUDIT_V7.65.md)

---

## 联系方式

- 项目主页：[待补充]
- 开发者邮件：[待补充]
- 社区论坛：[待补充]
- Issue追踪：[待补充]

---

*本文档随版本更新，如有疑问请查阅最新版本。*
