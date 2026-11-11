---
title: API_REFERENCE
status: active
sys_version: 0.57.0
updated: 2026-11-11
owner: specs
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# 赫普OS API参考文档

## 概述

本文档记录赫普OS的系统调用和标准库函数，供应用开发者参考。

- 内核版本：0.54.6
- 系统调用数量：104个（详见 [SYSCALL_ABI.md](SYSCALL_ABI.md)）
- 标准库：libhepu（stdio/string/stdlib/unistd）

---

## 一、系统调用

### 1.1 进程管理

| 系统调用 | 编号 | 原型 | 说明 |
|---------|------|------|------|
| SYS_EXIT | 5 | `void exit(int status)` | 进程退出 |
| SYS_YIELD | 6 | `void yield(void)` | 让出CPU |
| SYS_GETPID | 7 | `pid_t getpid(void)` | 获取进程ID |
| SYS_FORK | 20 | `pid_t fork(void)` | 创建子进程 |
| SYS_WAITPID | 21 | `pid_t waitpid(pid_t pid, int *status, int options)` | 等待子进程 |
| SYS_EXECVE | 22 | `int execve(const char *path, char *const argv[], char *const envp[])` | 执行程序 |
| SYS_KILL | 19 | `int kill(pid_t pid, int sig)` | 发送信号 |

### 1.2 内存管理

| 系统调用 | 编号 | 原型 | 说明 |
|---------|------|------|------|
| SYS_MALLOC | 3 | `void *malloc(size_t size)` | 内存分配（内核态） |
| SYS_FREE | 4 | `void free(void *ptr)` | 内存释放（内核态） |
| SYS_BRK | 23 | `int brk(void *addr)` | 设置程序断点 |
| SYS_MMAP | 24 | `void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)` | 内存映射 |
| SYS_MUNMAP | 25 | `int munmap(void *addr, size_t length)` | 解除内存映射 |

### 1.3 文件操作

| 系统调用 | 编号 | 原型 | 说明 |
|---------|------|------|------|
| SYS_OPEN | 8 | `int open(const char *path, int flags, int mode)` | 打开文件 |
| SYS_READ | 9 | `ssize_t read(int fd, void *buf, size_t count)` | 读取文件 |
| SYS_WRITE | 10 | `ssize_t write(int fd, const void *buf, size_t count)` | 写入文件 |
| SYS_CLOSE | 11 | `int close(int fd)` | 关闭文件 |
| SYS_LSEEK | 14 | `off_t lseek(int fd, off_t offset, int whence)` | 文件定位 |
| SYS_UNLINK | 26 | `int unlink(const char *path)` | 删除文件 |
| SYS_RENAME | 27 | `int rename(const char *old, const char *new)` | 重命名文件 |
| SYS_MKDIR | 28 | `int mkdir(const char *path, int mode)` | 创建目录 |
| SYS_RMDIR | 29 | `int rmdir(const char *path)` | 删除目录 |
| SYS_CHDIR | 30 | `int chdir(const char *path)` | 改变当前目录 |
| SYS_GETCWD | 31 | `char *getcwd(char *buf, size_t size)` | 获取当前目录 |
| SYS_ACCESS | 32 | `int access(const char *path, int mode)` | 检查文件访问权限 |

### 1.4 IPC通信

| 系统调用 | 编号 | 原型 | 说明 |
|---------|------|------|------|
| SYS_IPC_SEND | 12 | `int ipc_send(pid_t pid, const void *msg, size_t len)` | ❌ 64 位未注册（遗留设计号） |
| SYS_IPC_RECV | 13 | `int ipc_recv(pid_t *pid, void *msg, size_t max_len)` | ❌ 64 位未注册（遗留设计号） |
| SYS_PIPE | 16 | `int pipe(int pipefd[2])` | 创建管道（64 位已实现） |
| SYS_SHMGET | 17 | `int shmget(key_t key, size_t size, int shmflg)` | ❌ 64 位未注册（无共享内存系统调用） |
| SYS_SHMAT | 18 | `void *shmat(int shmid, const void *shmaddr, int shmflg)` | ❌ 64 位未注册 |

### 1.5 网络操作

| 系统调用 | 编号 | 原型 | 说明 |
|---------|------|------|------|
| SYS_SOCKET | 40 | `int socket(int domain, int type, int protocol)` | 创建套接字 |
| SYS_CONNECT | 41 | `int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)` | 连接 |
| SYS_BIND | 42 | `int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)` | 绑定 |
| SYS_LISTEN | 43 | `int listen(int sockfd, int backlog)` | 监听 |
| SYS_ACCEPT | 44 | `int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)` | 接受连接 |
| SYS_SEND | 45 | `ssize_t send(int sockfd, const void *buf, size_t len, int flags)` | 发送数据 |
| SYS_RECV | 46 | `ssize_t recv(int sockfd, void *buf, size_t len, int flags)` | 接收数据 |
| SYS_SENDTO | 47 | `ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)` | 发送到指定地址 |
| SYS_RECVFROM | 48 | `ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)` | 从指定地址接收 |
| SYS_SETSOCKOPT | 49 | `int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)` | 设置套接字选项 |
| SYS_GETSOCKOPT | 50 | `int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)` | 获取套接字选项 |

### 1.6 时间与系统

| 系统调用 | 编号 | 原型 | 说明 |
|---------|------|------|------|
| SYS_SLEEP | 15 | `void sleep(uint32_t ms)` | 睡眠（毫秒） |
| SYS_TIME | 33 | `time_t time(time_t *t)` | 获取系统时间 |
| SYS_GETTIMEOFDAY | 34 | `int gettimeofday(struct timeval *tv, struct timezone *tz)` | 获取精确时间 |
| SYS_HEPU_INFO | 0 | `int hepu_info(struct hepu_info *info)` | 获取系统信息 |
| SYS_REBOOT | 35 | `int reboot(int cmd)` | 系统重启/关机 |

### 1.7 设备控制

| 系统调用 | 编号 | 原型 | 说明 |
|---------|------|------|------|
| SYS_IOCTL | 36 | `int ioctl(int fd, unsigned long request, void *argp)` | 设备控制 |
| SYS_DUP | 37 | `int dup(int oldfd)` | 复制文件描述符 |
| SYS_DUP2 | 38 | `int dup2(int oldfd, int newfd)` | 复制文件描述符到指定编号 |
| SYS_FCNTL | 39 | `int fcntl(int fd, int cmd, ...)` | 文件控制 |

---

## 二、标准库函数

### 2.1 stdio.h

| 函数 | 原型 | 说明 |
|------|------|------|
| putchar | `int putchar(int c)` | 输出字符 |
| puts | `int puts(const char *s)` | 输出字符串 |
| printf | `int printf(const char *fmt, ...)` | 格式化输出 |
| sprintf | `int sprintf(char *buf, const char *fmt, ...)` | 格式化到缓冲区 |
| snprintf | `int snprintf(char *buf, size_t size, const char *fmt, ...)` | 安全格式化到缓冲区 |
| vsprintf | `int vsprintf(char *buf, const char *fmt, va_list ap)` | 可变参数格式化 |
| vsnprintf | `int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)` | 安全可变参数格式化 |
| vprintf | `int vprintf(const char *fmt, va_list ap)` | 可变参数输出 |
| getchar | `int getchar(void)` | 读取字符 |
| fgets | `char *fgets(char *s, int size, void *stream)` | 读取一行 |

### 2.2 string.h

| 函数 | 原型 | 说明 |
|------|------|------|
| strlen | `size_t strlen(const char *s)` | 字符串长度 |
| strcpy | `char *strcpy(char *dst, const char *src)` | 字符串复制 |
| strncpy | `char *strncpy(char *dst, const char *src, size_t n)` | 受限字符串复制 |
| strcmp | `int strcmp(const char *a, const char *b)` | 字符串比较 |
| strncmp | `int strncmp(const char *a, const char *b, size_t n)` | 受限字符串比较 |
| strcat | `char *strcat(char *dst, const char *src)` | 字符串拼接 |
| strncat | `char *strncat(char *dst, const char *src, size_t n)` | 受限字符串拼接 |
| strchr | `char *strchr(const char *s, int c)` | 查找字符 |
| strrchr | `char *strrchr(const char *s, int c)` | 反向查找字符 |
| strstr | `char *strstr(const char *haystack, const char *needle)` | 查找子串 |
| memcpy | `void *memcpy(void *dst, const void *src, size_t n)` | 内存复制 |
| memset | `void *memset(void *dst, int val, size_t n)` | 内存设置 |
| memcmp | `int memcmp(const void *a, const void *b, size_t n)` | 内存比较 |
| memmove | `void *memmove(void *dst, const void *src, size_t n)` | 安全内存移动 |
| atoi | `int atoi(const char *s)` | 字符串转整数 |
| atol | `long atol(const char *s)` | 字符串转长整数 |
| isdigit | `int isdigit(int c)` | 是否数字 |
| isalpha | `int isalpha(int c)` | 是否字母 |
| isalnum | `int isalnum(int c)` | 是否字母数字 |

### 2.3 stdlib.h

| 函数 | 原型 | 说明 |
|------|------|------|
| malloc | `void *malloc(size_t size)` | 内存分配 |
| free | `void free(void *ptr)` | 内存释放 |
| realloc | `void *realloc(void *ptr, size_t size)` | 重新分配 |
| calloc | `void *calloc(size_t nmemb, size_t size)` | 分配并清零 |
| exit | `void exit(int status)` | 进程退出 |
| abort | `void abort(void)` | 异常终止 |
| getpid | `pid_t getpid(void)` | 获取进程ID |
| fork | `pid_t fork(void)` | 创建子进程 |
| waitpid | `pid_t waitpid(pid_t pid, int *status, int options)` | 等待子进程 |
| execve | `int execve(const char *path, char *const argv[], char *const envp[])` | 执行程序 |
| rand | `int rand(void)` | 随机数 |
| srand | `void srand(unsigned int seed)` | 设置随机数种子 |
| itoa | `char *itoa(int value, char *str, int base)` | 整数转字符串 |
| getenv | `char *getenv(const char *name)` | 获取环境变量 |

### 2.4 unistd.h

| 函数 | 原型 | 说明 |
|------|------|------|
| read | `ssize_t read(int fd, void *buf, size_t count)` | 读取 |
| write | `ssize_t write(int fd, const void *buf, size_t count)` | 写入 |
| open | `int open(const char *path, int flags, int mode)` | 打开 |
| close | `int close(int fd)` | 关闭 |
| lseek | `off_t lseek(int fd, off_t offset, int whence)` | 定位 |
| unlink | `int unlink(const char *path)` | 删除 |
| rename | `int rename(const char *old, const char *new)` | 重命名 |
| mkdir | `int mkdir(const char *path, int mode)` | 创建目录 |
| rmdir | `int rmdir(const char *path)` | 删除目录 |
| chdir | `int chdir(const char *path)` | 改变目录 |
| getcwd | `char *getcwd(char *buf, size_t size)` | 当前目录 |
| access | `int access(const char *path, int mode)` | 访问检查 |
| brk | `int brk(void *addr)` | 设置断点 |
| sbrk | `void *sbrk(intptr_t increment)` | 增加断点 |
| dup | `int dup(int oldfd)` | 复制描述符 |
| dup2 | `int dup2(int oldfd, int newfd)` | 复制到指定编号 |
| pipe | `int pipe(int pipefd[2])` | 创建管道 |
| ioctl | `int ioctl(int fd, unsigned long request, void *argp)` | 设备控制 |
| sleep | `unsigned int sleep(unsigned int seconds)` | 睡眠（秒） |
| usleep | `int usleep(useconds_t usec)` | 睡眠（微秒） |

---

## 三、错误码（errno.h）

| 错误码 | 值 | 说明 |
|--------|-----|------|
| EPERM | 1 | 操作不允许 |
| ENOENT | 2 | 文件不存在 |
| ESRCH | 3 | 进程不存在 |
| EINTR | 4 | 系统调用被中断 |
| EIO | 5 | I/O错误 |
| ENXIO | 6 | 设备不存在 |
| E2BIG | 7 | 参数列表过长 |
| ENOEXEC | 8 | 可执行文件格式错误 |
| EBADF | 9 | 文件描述符错误 |
| ECHILD | 10 | 无子进程 |
| EAGAIN | 11 | 资源暂时不可用 |
| ENOMEM | 12 | 内存不足 |
| EACCES | 13 | 权限拒绝 |
| EFAULT | 14 | 地址错误 |
| EBUSY | 16 | 设备忙 |
| EEXIST | 17 | 文件已存在 |
| ENODEV | 19 | 设备不存在 |
| ENOTDIR | 20 | 不是目录 |
| EISDIR | 21 | 是目录 |
| EINVAL | 22 | 参数无效 |
| ENFILE | 23 | 系统文件表满 |
| EMFILE | 24 | 进程文件表满 |
| ENOSPC | 28 | 磁盘空间不足 |
| EROFS | 30 | 只读文件系统 |
| EPIPE | 32 | 管道破裂 |
| ENOSYS | 38 | 功能未实现 |
| ENOTEMPTY | 39 | 目录非空 |
| ENOTSOCK | 88 | 不是套接字 |
| ECONNREFUSED | 111 | 连接被拒绝 |
| ETIMEDOUT | 110 | 连接超时 |

---

## 四、网络协议栈API

### 4.1 套接字API（BSD Socket兼容）

```c
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* 创建套接字 */
int sockfd = socket(AF_INET, SOCK_STREAM, 0);  /* TCP */
int sockfd = socket(AF_INET, SOCK_DGRAM, 0);   /* UDP */

/* 连接服务器 */
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port = htons(80);
addr.sin_addr.s_addr = inet_addr("192.168.1.1");
connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));

/* 发送/接收 */
send(sockfd, "GET / HTTP/1.0\r\n\r\n", 18, 0);
recv(sockfd, buffer, sizeof(buffer), 0);

/* 关闭 */
close(sockfd);
```

### 4.2 网络工具命令

| 命令 | 说明 |
|------|------|
| `ifconfig` | 显示网络接口状态（MAC、链路、TX/RX统计） |
| `ping <ip>` | 发送ICMP Echo Request测试连通性 |
| `dhcp` | 启动DHCP客户端获取IP地址 |

### 4.3 支持的协议

| 层级 | 协议 | 状态 |
|------|------|------|
| 应用层 | HTTP（明文） | 已实现（`net64_http.c`）；WebSocket 仅 32 位遗留树、未编入 64 位；TLS 代码已编入但未接入 HTTP 调用链（见 OVERVIEW 3.4） |
| 传输层 | TCP、UDP、ICMP | 完整实现 |
| 网络层 | IPv4、ARP | 完整实现 |
| 链路层 | 以太网 | 完整实现 |
| 物理层 | RTL8139、e1000 | 驱动实现 |

---

## 五、图形子系统API

### 5.1 基础绘图

```c
#include <graphics.h>

/* 初始化图形子系统 */
gfx_init();

/* 绘制像素 */
gfx_put_pixel(x, y, color);

/* 绘制线条 */
gfx_draw_line(x0, y0, x1, y1, color);

/* 绘制矩形 */
gfx_draw_rect(x, y, w, h, color);
gfx_fill_rect(x, y, w, h, color);

/* 绘制圆形 */
gfx_draw_circle(cx, cy, r, color);
gfx_fill_circle(cx, cy, r, color);

/* 绘制文字 */
gfx_draw_string(x, y, "Hello", color, size);
```

### 5.2 字体系统

- 内置字体：DejaVu Sans（默认）、DroidSansFallback（CJK）
- 支持TrueType格式
- 支持抗锯齿渲染
- 支持字号缩放
- 支持字体回退机制

### 5.3 显示后端

| 后端 | 说明 | 状态 |
|------|------|------|
| GOP | UEFI Graphics Output Protocol | ❌ 未实现：64 位不依赖 UEFI GOP，Stage2 走 VBE 设置 framebuffer（`gfx64.c`/`vg64.c`） |
| VGA | VGA文本模式 | ✅ 文本终端回退（`term64.c`/`shell64.c`） |
| VBE | VESA BIOS Extensions | ✅ 默认图形后端（`gfx64.c` + `vg64.c`） |
| virtio-gpu | 虚拟GPU | ❌ 仓库内无源码，不在 64 位 Makefile |

---

## 六、开发环境

### 6.1 编译工具链

- 编译器：GCC（i686-elf-gcc 或系统gcc -m32）
- 链接器：LD（elf_i386）
- 汇编器：NASM/GAS
- 构建工具：Make

### 6.2 编译用户态应用

```bash
# C语言应用
gcc -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
    -I user/libhepu/include -c app.c -o app.o
ld -m elf_i386 -Ttext=0x20000000 -o app.elf \
    user/libhepu/crt0.o app.o \
    user/libhepu/string.o user/libhepu/stdio.o \
    user/libhepu/stdlib.o user/libhepu/unistd.o
objcopy -O binary app.elf app.bin
python3 tools/mkhug.py app.bin app.hug
```

### 6.3 Rust用户态应用

```bash
cd user/rust/hello-rust
cargo build --target hepu-i686.json
```

---

## 七、版本历史

| 版本 | 日期 | 主要变更 |
|------|------|---------|
| V8.05 | 2026-10-04 | 标准库完善（stdlib/unistd/errno） |
| V8.04 | 2026-10-04 | TLS客户端连接实现 |
| V8.03 | 2026-10-04 | e1000网卡驱动 |
| V8.02 | 2026-10-04 | TCP协议栈完善 |
| V8.01 | 2026-10-04 | DHCP客户端完善 |
| V8.00 | 2026-10-04 | 完整网络协议栈 |
| V7.99 | 2026-10-04 | IP协议层与真实Ping |
| V7.98 | 2026-10-04 | 网络驱动完善与ifconfig |
| V7.96 | 2026-10-04 | RTL8139网卡驱动 |
| V7.94 | 2026-10-04 | P0优先级开发完成 |

---

## 八、参考资料

- 错误历史：`docs/BOOTLOADER_BUG_HISTORY.md`
- 安全审计：`docs/SECURITY_AUDIT_V7.65.md`
- 开发路线图：`docs/DEVELOPMENT_ROADMAP.md`
- 真机测试指南：`docs/REAL_HARDWARE_TEST_GUIDE_V7.94.md`
- 图形子系统设计：`docs/GRAPHICS_SUBSYSTEM_DESIGN.md`
- HUG格式说明：`docs/HUG_FORMAT.md`
