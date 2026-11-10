---
title: COMPATIBILITY_LAYER
status: active
sys_version: 0.54.11
updated: 2026-11-01
owner: planning
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# Hepu OS 多平台兼容层规划（2026-10-23）

> 依据：docs/design/DESIGN.md 兼容层三条最高约束（用户态化/统一沙盒/预期管理）、
> docs/planning/DEVELOPMENT_ROADMAP.md 阶段3（POSIX 兼容层）、当前源码实测。

## 一、现状盘点（源码实测）

| 组件 | 文件 | 规模 | 实现度 |
|---|---|---|---|
| 多格式注册 binfmt | kernel/compat/binfmt.c | 160 | ELF32 完整；PE/Mach-O 仅 probe |
| ELF32 加载器 | binfmt_elf.c | 548 | 完整（静态 ELF） |
| ELF 动态链接 | binfmt_elf_dynamic.c | 484 | .so 加载/符号解析基础版 |
| PE 加载器 | binfmt_pe.c + stub + util | 354+75+162 | 子集：用户态 stub 桥接 write/read/exit 等，内核零改动 |
| Mach-O 加载器 | binfmt_macho.c | 346 | demo 级：固定 patch 偏移回填，仅跑内置 demo |
| Linux ABI 翻译 | linux_abi.c | 92 | ELF32 int 0x80 调用号翻译 |
| Darwin ABI 翻译 | darwin_abi.c / syscall.c | 47 | Mach trap 号（0x2000000 基址）翻译 |
| APK 兼容层 | compat_layer.c | 394 | 仅配置框架（模式/档案/沙箱等级），无真实执行 |
| APK 解析器 | apk_parser.c | 216 | 假解析（返回写死包名/版本），无 ZIP/APK 真实解析 |
| Android API | android_api.c | 259 | 内存桩（Activity/Intent/Prefs），无 Android 运行时 |
| .hug 依赖管理 | hug_depman.c | 377 | 依赖解析/安装流程可用 |
| POSIX 层 | — | 0 | 未建成（路线图标注高优先级未完成） |
| H-HAL 驱动包 | — | 0 | 文档概念（D-PK 机型驱动包），无实现 |

## 二、目标与硬约束（DESIGN.md）

1. 兼容层**用户态化**：外部格式（EXE/ELF/Mach-O）加载与 ABI 翻译禁止入内核，故障只影响自身
2. **统一沙盒**：.hug/EXE/Linux/macOS 应用必须同过一套沙盒；外部格式权限**降级**执行
3. **预期管理**：尽力兼容不承诺 100%；应用标注兼容等级（完全/基本/运行异常/不支持）；兼容等级由实测判定

## 三、分层架构蓝图

```
应用层       Linux 软件 / Windows 软件 / Android APK / macOS 应用 / .hug 原生
            ┌─────────────────────────────────────────────────────────┐
兼容沙盒层   统一沙盒（内存/CPU/文件/网络限额，权限降级）                │
            ┌─────────────────────────────────────────────────────────┐
加载层       binfmt: ELF32(✓) → PE32(子集) → Mach-O(demo) → APK(✗)    │
            ┌─────────────────────────────────────────────────────────┐
ABI 翻译层   linux_abi(✓) / pe_abi(部分 stub) / darwin_abi(demo)      │
            ┌─────────────────────────────────────────────────────────┐
API 供给层   POSIX 子集(缺) → PE Kernel32/User32 stub(缺) → Android   │
            API stub(桩) → Mach dyld stub(缺)                         │
            ┌─────────────────────────────────────────────────────────┐
系统调用层   HEPU syscall（唯一出口，int 0x80 统一翻译）               │
驱动兼容层   H-HAL（虚拟设备抽象：存储/输入/网络/GPU 翻译）            │
```

## 四、软件安装测试计划（按 QEMU 可验证性排序）

**阶段 T1（立即，系统稳定期内可做）：真实可执行的最小闭环**
- T1.1 ELF32 静态可执行文件安装+运行：编译静态 hello / 静态 busybox 单命令（ls/echo/cat），
      放入 IDE 盘或 USB 盘 → 终端执行 → 验证 ELF 加载 + int 0x80 翻译 + 文件读写
- T1.2 ELF 动态链接：自定义 libhello.so + 主程序，验证 .so 加载/符号解析/依赖
- T1.3 PE stub demo：写/读/退出最小 PE，验证 PE 加载 + 用户态 stub 桥接
- T1.4 Mach-O demo：内置 demo 运行验证
- T1.5 APK 假解析：现有解析器输出核对（验证桩接口，非真实安装）

**阶段 T2（POSIX 层建成后）：真实 Linux 软件**
- busybox 完整静态版（多命令覆盖 POSIX 调用面）
- 文本/终端类：nano、vim 静态版
- 简单 GUI（需图形栈就绪）：小游戏（扫雷/贪吃蛇类静态 X11 应用 → 未来走 HEPU 图形翻译）

**阶段 T3（PE 导入表翻译后）：Windows 软件**
- 最小 Win32 控制台程序（WriteFile/GetStdHandle）
- 简单 PE GUI（需要 GDI stub）

**阶段 T4（远期）：Android/macOS**
- APK：真实 ZIP 解析 + dex → 自研字节码解释器（ART 移植级别，最大工程）
- macOS：Mach-O 泛化加载 + dyld stub 扩展

## 五、驱动与依赖开发清单（系统稳定后按序）

**依赖层（软件运行必需，优先）**
- P-1 POSIX 系统调用补齐：open/read/write/close/lseek/fork/pipe/signal/mmap/munmap/execve/
      stat/ioctl/time 系列（当前 ELF 程序可调用的子集）
- P-2 动态链接完善：dlopen/dlsym/dlclose、依赖树解析、rpath/LD_LIBRARY_PATH、
      .so 重定位（PLT/GOT 完整）
- P-3 PE 导入表（IAT）翻译 + Kernel32/User32 常用 API stub 扩展（当前仅 8 个 stub）
- P-4 Mach-O dyld stub 泛化（当前固定 demo patch）

**驱动兼容层（H-HAL，软件依赖硬件抽象时）**
- D-1 虚拟存储抽象：EXT2/FAT 设备无关接口（Windows/Android 应用常见文件路径）
- D-2 输入抽象：键盘/鼠标/触摸统一事件结构（兼容层转发）
- D-3 显示抽象：帧缓冲统一接口（未来 GPU/GDI 翻译的底）
- D-4 网络抽象：BSD socket 已存在 → 补非阻塞/多路复用（select/poll）
- D-5 时钟/定时器抽象：为 sleep/tick 类 API 提供统一源
- D-6 H-HAL 驱动包机制（D-PK）：机型/设备描述表 + 驱动动态加载（长期）

## 六、优先级路线（遵守"系统稳定后才开发驱动"）

```
P0 现在（系统稳定期收尾并行）：
    T1 安装测试闭环（ELF32 静态/动态 + PE stub + Mach demo）
    → 产出：兼容层"可安装可运行可判定"的验证基线 + 兼容等级表首版
P1 系统稳定后第一批：
    POSIX 系统调用补齐 → T2 busybox/终端软件真实安装
    PE IAT + Kernel32/User32 stub → T3 Windows 控制台程序
P2 中期：
    Mach-O 泛化、.so 依赖树、沙盒审计强化
P3 远期：
    Android 运行时（ZIP+dex+解释器）、H-HAL 驱动包、GDI/OpenGL 图形翻译
```

## 七、验收标准

- 每个兼容等级（完全/基本/运行异常/不支持）由**实测结论**填写，禁止未验证宣称
- T1 闭环：至少 1 个 ELF32 静态程序 + 1 个 .so 动态程序 + 1 个 PE stub demo 可安装并运行
- 沙盒约束：兼容程序崩溃不影响内核/其他进程（QEMU 验证）
- 驱动开发门槛：系统内核/图形/网络稳定基线回归全绿后方可启动 P1 驱动项

## 八、OBS 安装实测记录（V0.13.44）

**测试目标**：真实安装 OBS Studio 到 Hepu OS 文件系统并尝试运行（外部软件安装链路首测）。

**安装（成功 ✅）**
- 下载 OBS Studio 32.2.2 Ubuntu 24.04 deb（134MB），ar 解出 data.tar.gz，解出 1904 个文件（usr/local 布局）
- 新增 `tools/obs_install_test.sh`：debugfs 批量命令（mkdir+write 2006 条，单进程）写入镜像 ext2 根分区
- 实测：`/bin/obs`（6.4MB）、`/bin/obs-ffmpeg-mux`、`/bin/obs-nvenc-test`、`/lib/x86_64-linux-gnu/libobs.so.30`、libobs-scripting/frontend-api/opengl、`/lib/.../obs-plugins`、obs-scripting 全部落盘——**deb 文件布局可完整安装**
- 镜像容量要求：OBS 328MB → 根分区需 ≥400MB（原 256MB 镜像磁盘满导致 cp 静默失败；`make_single_img64.sh` 新增 `HEPU_IMG_SIZE_MB` 可配，测试用 512MB）

**运行（失败 ❌，三个断点，均已有根因）**
1. **KHEAP 容量**：`[KHEAP] OOM: req=5906432`——内核堆仅 256KB 静态数组，OBS 主程序 ELF 需映射 5.9MB。**V0.13.45 已解决**：KHEAP 扩容至 8MB 并打通启动链路——根因是内核入口 `rep stosq` 清 bss（8MB 时 `__bss_end` 11.7MB）**覆盖了 Stage2 页表（0x600000-0x607000）**导致 #PF 三异常复位；方案：Stage2 页表整体迁移至 0xC00000（12MB，bss 之后）+ 低/高 PD 扩至 8 项（16MB 映射）+ multiboot PD 8 项。实测 KHEAP init 8MB、系统启动 ready=1。**遗留**：8MB 堆可过 OBS 主程序 5.9MB 单次分配，但 OBS 完整运行仍需动态链接器（断点 2）与 POSIX syscall 层（断点 3）
2. **动态链接**：OBS 为 ET_DYN PIE，需 PT_INTERP `/lib64/ld-linux-x86-64.so.2`——系统无动态链接器，elf64.c 仅支持静态 ET_EXEC 直载
3. **运行时依赖**（ldd 实测 19 项缺失）：glibc 2.38+（宿主 22.04 仅 2.35 也缺）、Qt6 Core/Gui/Widgets/Svg/Xml/Network/DBus、FFmpeg6（avcodec/avformat/avutil）、mbedtls、python3.12、libpci

**依赖清单（OBS 完整运行所需最小集）**：ld-linux + glibc(2.38) + libstdc++(GLIBCXX_3.4.32) + Qt6 6 库 + FFmpeg6 3 库 + libobs 自身 + X11/GL（libGLX/libOpenGL 宿主有，系统内无）

**结论与路线调整**
- 安装侧（deb 解包→文件系统部署）**已验证可行**，可直接成为包管理第一步
- 运行侧暴露兼容层三大工程：① KHEAP 扩容+映射配套（P0 前置，需 8MB 堆 + 64MB 身份映射稳定化）② 动态链接器 ld-linux（P1）③ POSIX 系统调用层（P1，OBS 需 100+ syscall）
- 后续按"小软件先通"原则：先 T2 busybox（静态）→ 再动态 ELF → 最后 Qt/GL 应用；OBS 列为 P2/P3 远期验收项，不阻塞 P0
