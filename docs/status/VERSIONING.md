---
title: VERSIONING
status: active
sys_version: 0.57.0
updated: 2026-11-11
owner: status
source_of_truth: null
supersedes: null
---

# 赫普OS 版本号管理规范

## 版本号格式

采用三段式版本号：**重要版本.默认版本.bug版本**

```
  0 . 9 . 0
  ↑   ↑   ↑
  │   │   └── bug版本：修复bug时+1
  │   └────── 默认版本：普通功能发布时+1
  └────────── 重要版本：重大突破/重要里程碑时+1（未到则保持0）
```

## 版本号含义

### 第一位：重要版本（Major）

**什么时候+1？**
- **由用户/项目负责人决定**：当用户认为这个版本达到了重要里程碑，就加一
- 完成重大架构重构
- 实现里程碑式功能
- 系统能力有质的飞跃
- 重要的技术突破
- **用户未确认重要里程碑，保持0**

**重要版本=0的含义**：
- 系统仍在快速迭代期
- 功能持续完善中
- 用户尚未确认达到重要里程碑
- 这是正常的开发状态，不是贬义

**重要版本增加流程**：
1. 开发人员完成重大功能
2. 向用户展示成果
3. 用户确认"这个版本很重要/可以"
4. 重要版本+1，默认版本和bug版本归零

### 第二位：默认版本（Minor）

**什么时候+1？**
- 普通功能发布
- 新增常规功能
- 正常的版本迭代
- 没有特别重大的突破

**示例**：
- 新增一个应用
- 完善一个子系统
- 优化性能
- 添加文档

### 第三位：bug版本（Patch）

**什么时候+1？**
- 修复bug
- 修复问题
- 紧急修复
- 编译错误修复
- 兼容性问题修复

**示例**：
- 空指针崩溃
- 内存泄漏
- 逻辑错误
- 兼容性问题

## 版本号演进规则

### 正常开发流程

```
0.9.0  → 新增功能 → 0.10.0  → 新增功能 → 0.11.0
  ↓
修复bug
  ↓
0.9.1  → 修复bug → 0.9.2  → 重大突破 → 1.0.0
```

### 版本号重置规则

- 重要版本+1时，默认版本和bug版本重置为0
- 默认版本+1时，bug版本重置为0
- bug版本+1时，其他不变

### 示例

| 版本 | 说明 | 变更原因 |
|------|------|---------|
| 0.9.0 | 当前版本 | 默认版本迭代到9，重要版本仍为0 |
| 0.9.1 | bug+1 | 修复了一个空指针崩溃 |
| 0.9.2 | bug+1 | 修复了内存泄漏 |
| 0.10.0 | 默认+1 | 新增包管理器功能 |
| 0.10.1 | bug+1 | 修复了包管理器bug |
| 0.11.0 | 默认+1 | 新增网络功能 |
| 1.0.0 | 重要+1 | 完成64位完整支持，重要里程碑 |

## 当前版本

**0.54.12**
- 重要版本：0
- 默认版本：54
- bug版本：12

> 版本号以本文件 frontmatter `sys_version` 为唯一事实源。以下为历史迭代记录。

### 双内核版本边界

本仓库为双内核架构，两条线的版本号各自独立，不可混用：
- **64 位主线（当前事实源）**：由 `kernel/arch/x86_64` 构建，CI/Release 只构建
  这条线，版本即本文件 frontmatter 的 `sys_version`。
- **32 位 legacy（已冻结）**：由仓库根 `Makefile` 构建 `kernel/arch/x86`，已不在
  CI 中；其版本宏（`hepu_brand.h` 的 V0.33.1、`hepu.h`、`kernel.c` 的
  hepu_kernel/hepu_system_version）停留在 legacy 自身版本，不随 64 位主线 bump，
  也不作为当前系统版本的判断依据。

**0.54.12 迭代内容（2026-11-11，桌面稳定性 + 安全止血 + 文档真值对齐）**

一、桌面与稳定性（`0b4750f`）
- HepuShell 窗口槽位泄漏：开窗失败时仍占用槽位，反复开关 32 次后无槽可用、无法再开新窗口；改为识别 `id==-1` 空闲槽并新增 `hs_window_recycle` 回收
- 纯 APIC 模式 PIT 中断冻结：解析 MADT 中断源覆盖（IRQ0→GSI2），IOAPIC 重定向表改用真实 GSI，新增 `acpi_irq_to_gsi` / `acpi_init_irq_overrides`；桌面画面恢复实时刷新

二、安全与健壮性（`f9db839`，6 P0 + 12 P1）
- INT-01 LAPIC 定时器校准：改用 PIT channel-0 软件 tick（1ms/tick、50ms 窗口）门控实测 LAPIC 计数，替换硬编码 ICR=6250（此前实测漂至约 4kHz）；多窗口实测 994.6 / 998.3 / 998.5 / 1014.0 Hz，客户机时间内为 1kHz；并修复"校准函数把周期 LVT 覆盖为一次性模式"的子缺陷（周期 LVT 改为校准返回后写入）
- 系统调用 P0：`writev` 用户指针与长度边界校验；`clone` 的 `child_tid` 与 `tls` 裸写校验；`reboot`、`mkfs` 增加 PID==0 权限闸；`sigaction` 正确解析 sigaction 结构，修复信号投递
- 12 项 P1：MM-01/MM-02、NET-01/02/05、FS-03/04/05、BLD-14、GFX-01、DRV-02、WM-02
- 回归：全量干净重建警告 210→209（唯一消失项为基线 `syscall64_full.c` unused parameter `oldact`）、零新增警告类型；主机测试 6/6；镜像重建成功；QEMU 桌面与输入正常

三、文档与规划
- `2f78278` 文档真值对齐：修正 14 份架构/规格/规划文档中落后于源码的表述（TLS、CSPRNG、动态链接、RSA、PT_INTERP 等已实现能力不再标"不存在"）；收敛 OPEN_BUGS 台账（R1–R4 关闭项带 commit hash 与 `file:line` 证据）；两项待定现象定论——OPEN-03 sendkey 属应用按键绑定而非驱动缺陷（PS/2 与 USB HID 双链路均端到端可用），R3-CURSOR 属测量误解（相对/绝对指针语义混淆，渲染无二次错位）
- `0be1334` 新增 `docs/planning/PLAN_SMP_HARDENING_R5.md`：SMP 硬化 25 条、五阶段（仅计划，未实施）
- `3855fa1` `docs/guides/REAL_HARDWARE_TEST.md` 升级为可执行真机上机 checklist：41 条缺口、7 条硬阻塞（含校准自旋无超时导致真机启动挂死）
- `53a993e` 新增 `docs/planning/SMP_DECISION_A_VS_B_R5.md`：A 完整硬化 vs B 显式单核声明，推荐先 B 止血、真机闭环后再分阶段做 A
- 版本同步：本文件 frontmatter、49 份活跃文档 `sys_version`、README 徽章与"当前版本"行统一为 0.54.12；界面版本字符串共修 **14 处**——发布清单内 6 处（`hepushell.c`×2、`hs_apps.c`×2、`notes64.c`、`shell64.c`），另 8 处陈旧串一并收敛（启动横幅 `kernel_main_64.c` 的 `v0.54.6`、`version` 命令 `0.37.2`、uname 行 `0.10.156`、neofetch 三处 `0.10.117`、Settings 的 `System/Version` 值 `0.10.156`、占位文案 `V0.13.10`）
- 发布验证：真全量干净重建（删除仓库根 `build_x86_64`，126 个目标文件全新编译）警告 **209** 条，与基线一致、零新增；主机测试 **6/6**；镜像 `/tmp/hepu64_single.img`（256 MiB）重建成功；QEMU 实测 System Info、终端标题、`ver`、`version`、Settings 系统页均显示 v0.54.12；二进制 `0.54.11` / `0.54.6` / `0.37.2` / `0.10.117` / `0.10.156` 残留计数均为 0

**0.54.11 迭代内容（2026-11-10，两轮安全审计修复与版本号治理）**
- 第二轮安全修复：DNS 压缩指针循环 DoS、CSPRNG 种子被 reseed 抹成固定值、getrandom 假随机接通真实 CSPRNG、pmm64 整数溢出、getdents 直写用户指针、DHCP/TCP 边界校验、ext2 间接块同址复用（SEC-11）
- 构建链加固：镜像脚本自动构建 MBR 引导桩，失败明确报错，不再静默产出坏镜像
- 版本号治理：统一界面显示字符串、README、本文件的多源不一致；补齐此前缺失的 0.54.9/0.54.10 记录
- 回归：主机端 5 套件（ASan/UBSan）全通过，QEMU 端到端引导无 panic
- 详见 `docs/security/AUDIT_64BIT_R2.md`

**0.54.10 迭代内容（2026-11-07，镜像自动构建发布）**
- CI 自动构建并发布 x86_64 镜像（Legacy BIOS `hepu64_single.img` + UEFI `hepu64_uefi.img`），附 SHA256 校验
- release 工作流授予 GITHUB_TOKEN contents:write 以创建 Release；增强 UEFI 构建诊断
- 修复 binutils>=2.45 下 objcopy 参数兼容；UEFI 双启动镜像与真机启动修复、内核加载不再占用 VGA 显存

**0.54.9 迭代内容（2026-11-04，Clavis 界面对齐与 OOBE 图形化向导）**
- 对照 Clavis Shell 做桌面体验对齐：程序化壁纸与取色、控制中心分页、启动器网格化、Dock 动效、通知卡片样式、侧栏滚动
- 图形化首启向导（OOBE）与账户体系；新增设备管理器/磁盘管理/日志查看器
- 共 19 个提交

**0.54.8 迭代内容（2026-11-04，刷入漏洞紧急修复）**
- 修复 MBR 分区表 CHS 字段全零（`make_single_img64.sh` 硬编码 `0x00,0x00,0x00`）导致部分 BIOS 混合模式拒绝分区、U 盘刷入失败（critical）
- 新增 `lba_to_chs()`（1023/255/63 兼容表示法）正确编码 start/end CHS
- CI：QEMU headless smoke test 暂跳过（内核无 COM1 printk）；release.yml 重写对齐构建链
- 回归：镜像 256MB，MBR PTE CHS 字段非零验证通过

**0.20.8 迭代内容（2026-10-28，真机调试指南）**
- 写了真机黑屏快速定位表（H1/E!/菜单/Logo/回BIOS 各阶段含义）
- 回归：512MB 到桌面

**0.20.7 迭代内容（2026-10-28，大 ELF 缓冲上限发现）**
- 发现 256KB BSS 缓冲限制大 ELF；尝试提到 1MB 但内核 BSS 超限无法启动
- 已回退到 256KB；大 ELF 需要改用动态内存分配（P1 项）
- 回归：512MB 到桌面、C 程序跑通

**0.20.6 迭代内容（2026-10-28，真 C 程序跑通）**
- gcc 编的 C 程序（含 main/_start）在 Hepu 跑通，输出 "Hello from C!"
- 回归：512MB 到桌面

**0.20.5 迭代内容（2026-10-28，read+close 闭环）**
- hello4.elf open→read→close 完整文件 I/O 跑通
- 回归：512MB 到桌面

**0.20.4 迭代内容（2026-10-28，open 文件测试）**
- hello3.elf 调 Linux open(2) 读 ext2 成功
- 回归：512MB 到桌面

**0.20.3 迭代内容（2026-10-28，getpid 测试）**
- hello2.elf 调 Linux getpid(39) 正常返回
- 回归：512MB 到桌面

**0.20.2 迭代内容（2026-10-28，跑通静态 Linux ELF）**
- 里程碑：主机 gcc -static 编译的原生 Linux hello_linux.elf 在 Hepu 跑通，输出 "Hello Linux!"
- 验证了 syscall 翻译表（1=write, 60=exit）工作正常
- 回归：512MB 到桌面

**0.20.1 迭代内容（2026-10-28，exit_group 翻译）**
- Linux exit_group(231) 翻译到 exit(6)；翻译表扩到 256
- 回归：512MB 到桌面

**0.20.0 迭代内容（2026-10-28，mmap/munmap + Linux 翻译）**
- 系统调用 33=mmap（用户堆分配）、34=munmap（简化为 no-op）
- Linux 号 9/11 翻译到 33/34
- 回归：512MB 到桌面

**0.19.9 迭代内容（2026-10-28，mmap 系统调用）**
- 系统调用 33=mmap（简化：在 0x50000000 起分配用户页，W^X）；Linux 号 9 翻译到 33
- 回归：512MB 到桌面

**0.19.8 迭代内容（2026-10-28，扩充 Linux 翻译）**
- 翻译表扩到 128 项：加 fstat/brk/unlink/getcwd
- 回归：512MB 到桌面

**0.19.7 迭代内容（2026-10-28，Linux 系统调用翻译）**
- syscall dispatch 加最小 Linux x86_64 号翻译：0=read 1=write 2=open 3=close 60=exit 39=getpid 35=nanosleep → Hepu 号
- 这是 P1-3 POSIX 兼容层第一步；离跑 Linux 静态 ELF 更近
- 回归：512MB 到桌面、sec_test 通过

**0.19.6 迭代内容（2026-10-28，crypto 入 64 位）**
- P0-2 第一步：kernel/crypto/crypto.c（XTEA 加密/解密、页级加密）编入 64 位内核
- 回归：512MB 到桌面

**0.19.5 迭代内容（2026-10-28，mkfs 权限门）**
- 安全：sys_mkfs 加魔数 0x4D4B4653('MKFS') 第4参校验，普通用户程序无法误格式化磁盘（SEC-04 部分）
- install64 同步传魔数
- 回归：512MB 到桌面

**0.19.4 迭代内容（2026-10-28，终端长行换行）**
- t_put 从"超 60 字截断"改为自动折行，长输出不再溢出窗口
- 回归：512MB 到桌面

**0.19.3 迭代内容（2026-10-28，stat 系统调用）**
- 系统调用 32=stat(path,size_ptr,isdir_ptr)，基于 ext2_64_stat；文件管理器可显示真实大小/类型
- 回归：512MB 到桌面、sec_test 通过

**0.19.2 迭代内容（2026-10-28，安装器内核拷贝）**
- install64 加 kernel64 拷贝：LBA2000 起 976 扇区，每次 64 扇区从 drive0→drive1；stage2 改逐扇区
- 安装器闭环完成：MBR+mkfs+引导区+内核；仍待真机双盘验证

**0.19.1 迭代内容（2026-10-28，安装器引导区拷贝）**
- install64 加 syscall4；写 MBR 后从源盘(drive0)拷 bootsector/stage2 到目标盘(drive1)，为真机装硬盘铺路
- 注：仍需真机双盘验证；QEMU 单盘路径不变

**0.19.0 迭代内容（2026-10-28，多盘选择）**
- sys_disk_read/write 加第 4 参 drive（0=主盘,1=从盘），内部调 ide64_set_drive；现有调用不传参仍按主盘，向后兼容
- 为真机安装（USB 启动盘→硬盘目标盘 raw 拷贝引导区）铺路
- 回归：512MB 到桌面、sec_test 通过

**0.18.9 迭代内容（2026-10-28，安装器接 mkfs）**
- 系统调用 31=mkfs(lba,sectors,label)，封装内核 ext2_64_mkfs
- install64 写 MBR 后格式化 P1/P2/P3 为 ext2（OPEN-04 部分：格式化完成；rootfs 拷贝仍待做）
- 回归：512MB 到桌面、sec_test 通过

**0.18.8 迭代内容（2026-10-28，readdir 用户态封装）**
- libc 加 SYS_READDIR=30 与 hepu_readdir(path,buf,bufsz)；filemgr.c 改用真实 ext2 根目录列举（替代占位假数据）
- 用户态 hello/shell/forktest 构建通过；回归 512MB 到桌面

**0.18.7 迭代内容（2026-10-28，新增 readdir 系统调用）**
- 系统调用 30 = readdir(path, outbuf, outsz)，基于 ext2_64_list_dir_entries，每项打包为 "name\0type\0"（type='d'/'f'）
- 用户态可真实列目录，文件管理器/终端 ls 后续可接入
- 回归：512MB 到桌面、sec_test 通过

**0.18.6 迭代内容（2026-10-28，fork 根因定位 + USB 无线核对）**
- 实测确认 syscall 全程关中断：sys_sleep 用 hlt 会永久挂起（已回退忙等），即缺协作调度点；根因写入 SEC-03
- 核对 USB 栈：UHCI 驱动已含 2.4G 无线接收器多 HID 复合接口支持（V0.13.62）
- 二进制同 v0.18.5（sys_sleep 回退），仅文档更新；回归 512MB/1.5GB 到桌面、sec_test、SMAP

**0.18.5 迭代内容（2026-10-28，消除硬编码路径，可换目录构建）**
- tools/make_single_img64.sh 的 PROJECT 与内核/rootfs/字体路径改为脚本自身位置推导；kernel/arch/x86_64/Makefile 的 PROJECT_ROOT 从 Makefile 位置推导
- 换目录后无需改配置即可 make 和打包镜像
- 回归：512MB 到桌面、sec_test、SMAP 通过

**0.18.4 迭代内容（2026-10-28，fork 返回上下文改专用槽 + 定位子进程不调度）**
- fork_trampoline_64 改读子进程专用槽 g_fork_child_rip/rsp/rflags（sys_fork 时从父进程全局返回上下文快照），避免被后续 syscall 覆盖
- 实测：父进程隔离仍 OK；子进程仍未跑，定位根因为 sys_sleep 忙等待不 yield + 单用户进程模型未接入多进程调度器（见 SEC-03）
- 回归：512MB 到桌面、sec_test 通过

**0.18.3 迭代内容（2026-10-28，fork 端到端测试）**
- 新增 /bin/forktest.elf：fork 后父子各写数据段变量，验证地址空间隔离
- 实测结果：父进程 fork 返回子 PID、父数据 g_shared=1111 未被子进程改动（隔离 OK）；但子进程未执行到用户态代码（无 CHILD 输出），确认子进程返回上下文存在缺陷（见 SEC-03，全局 syscall 返回槽被子进程调度时覆盖）
- 回归：512MB 到桌面、sec_test 通过、forktest 不挂死

**0.18.2 迭代内容（2026-10-28，安全文档对齐）**
- OPEN_BUGS 更新：SEC-01（ring3 隔离）、SEC-06（SMEP/SMAP/NX/W^X）标记为已修复；SEC-02 剩余项改为 fork 写时复制(COW)
- 复核 uaspace_fork_copy：用户窗口 PDPT[1]→PD→PT→4KB 帧逐页深拷贝，失败路径回收；结构正确，端到端测试 ELF 待补
- 回归：512MB 到桌面、sec_test 通过

**0.18.1 迭代内容（2026-10-28，网络速率改到任务管理器）**
- 顶栏不再显示速率；任务管理器 Network 面板从 e1000 计数器（恒 0）改接 net_hal_get_stats，按累计字节差/帧 tick 算真实 DL/UL KB/s，并显示总量与 IP/绑定状态
- 回归：512MB 到桌面、sec_test 通过

**0.18.0 迭代内容（2026-10-28，开启 SMAP——ring0 默认不可访问用户页）**
- 在 SMEP 基础上经 CPUID.7.0.EBX[20] 检测后开 CR4.SMAP：ring0（除显式 STAC 外）访问用户态页即页错误；copy_from_user/copy_to_user/copy_str_from_user 三处内核访问用户页的路径已用 stac/clac 包裹
- 至此 ring3 隔离阶段2 安全加固全部完成：高半区/RAM>1GB、fork 独立地址空间、SMEP+SMAP、brk 分页+NX/W^X
- QEMU 实测 `-m 512` 与 `-m 1536` 均到桌面、sec_test 通过

**0.17.2 迭代内容（2026-10-28，顶栏网络速率显示）**
- 顶栏在 WiFi 图标左侧实时显示 DL/UL KB/s，基于 net_hal_get_stats 累计字节与帧 tick 差值计算
- 回归：512MB 到桌面、sec_test 通过

**0.17.1 迭代内容（2026-10-28，网络可观测性 + 大内存验证）**
- 新增系统调用 29（net_stats）：返回累计 tx/rx 字节，任务管理器可据此算实时上传/下载速率；只写用户窗口内指针
- QEMU 挂 rtl8139 实测：驱动探测到 I/O 0xC000、MAC 正常、ETH/ARP/IP/ICMP/UDP/TCP/DHCP 栈初始化、DHCP Discover 发出（测试环境 socket 后端无 DHCP 服务器，故无应答；真机/有 NAT 路由即可获得 IP）
- 确认 `-m 2048`（2GB）随高半区重映射已正常启动到桌面、隔离私有 PML4 生效（此前 stage2 卡死问题消除）
- 回归：512MB 到桌面、sec_test 通过

**0.17.0 迭代内容（2026-10-28，隔离阶段2 ④：sys_brk 真正映射页 + W^X/NX；阶段2 全部完成）**
- sys_brk 从「只改变量不映射」改为真正按需分页：堆增长时分配物理页并映射到 [0x40100000,0x60000000)，收缩时反映射并归还物理页；堆页带 NX 位（EFER.NXE 已开），实现 W^X——用户堆可写不可执行
- 至此阶段2 四项全部落地：②高半区/RAM>1GB、①fork 复制地址空间、③SMEP、④brk 分页+NX
- QEMU 实测 512MB / 1536MB / 2048MB 均启动到桌面、sec_test 通过、SMEP 生效、无页错误；2GB 隔离私有 PML4 正常（此前的 stage2「Loading kernel」卡死已随高半区重映射消除）
- 仍未做：SMAP（需 STAC/CLAC 包裹内核访问用户页路径）、fork 的写时复制(COW)、fork 端到端测试程序

**0.16.0 迭代内容（2026-10-28，隔离阶段2 ③：开启 SMEP）**
- 切换到内核页表后，经 CPUID.7.0.EBX[7] 检测支持即开 CR4.SMEP：内核（ring0）不得执行任何用户页（U=1），堵住「内核函数指针被指向用户页 → 取指执行」类提权
- 前提已就绪：v0.15.0 起低身份映射与高半区直接映射都不带 USER 位，内核映射全 U=0
- SMAP 暂未开：copy_*_user 等内核访问用户页需 STAC/CLAC 包裹，改动面大，下一轮单独做
- QEMU 实测 `-m 512` 与 `-m 1536` 均正常到桌面、sec_test 通过、无页错误

**0.15.1 迭代内容（2026-10-28，隔离阶段2 ①：fork 复制独立地址空间——已接线，待 fork 测试程序验证）**
- 新增 uaspace_fork_copy()：深度克隆父进程用户窗口页表（PDPT[1]→PD→PT→4KB 帧逐页拷贝，子进程拥有独立物理页，非 COW），内核视图共享并清 U 位；vmm64 表遍历走 phys_to_virt 支持 >=1GB 页表页
- sys_fork 现从当前隔离 ELF 进程克隆地址空间；fork_trampoline_64 在 sysretq 前加载子进程克隆 PML4，子进程从父进程 syscall 返回点继续、rax=0
- 启动回归正常（512MB 到桌面、sec_test 通过）；尚无 fork 测试 ELF，端到端（父子互不影响、写时独立）待下一轮补测试程序验证
- 仍未做：③内核页表去 U 位开 SMEP/SMAP、④sys_brk 真正映射页 + NX/W^X

**0.15.0 迭代内容（2026-10-28，隔离阶段2 ②：高半区直接映射 / RAM>1GB 支持）**
- 内存布局重构：低身份映射从「全物理内存恒等映射到低地址」改为只映射 [0,1GB)，并跳过用户窗口 [1GB,2GB)——PDPT[1] 在内核 PML4 中保持 present=0，隔离进程不再因内核恒等映射占住用户窗口而退回旧模式
- 新增高半区直接映射 KERNEL_DIRECT_BASE(0xFFFF800000000000)+phys，覆盖物理 [1GB,4GB)（ring0、无 U 位、GLOBAL 2MB 大页），内核可访问 >=1GB 的页表/缓冲区/固件表/低 MMIO；vmm64 全部表遍历改走 vmm64_phys_to_virt（<1GB 原样、>=1GB 走高半区）
- 修复 ACPI 在高内存机型崩溃：RSDT/MADT 等 ACPI 表常落在物理 >=1GB（QEMU 1.5GB 下 RSDT=0x5FFE1AC6），改走 phys_to_virt 访问，解决「RSDT above 1MB」后三重错重启循环
- QEMU 实测：`-m 512` 与 `-m 1536` 均正常启动到桌面、隔离私有 PML4 生效（无 isolation unavailable 回退）、sec_test 通过；`-m 2048` 仍在 32 位 stage2 「Loading kernel...」卡死（独立早期启动问题，与本改动无关，待查）
- 仍未做（阶段2 剩余）：①fork 复制地址空间、③内核页表去 U 位开 SMEP/SMAP、④sys_brk 真正映射页 + NX/W^X

**0.14.0 迭代内容（2026-10-28，ring3 内存隔离阶段1 经 QEMU 回归后默认开启）**
- 隔离默认开启：每进程私有 PML4/PDPT，内核恒等映射与 MMIO 在进程视图清 U/S 位（ring0 仍可访问、ring3 不可），关闭最大已知风险「用户程序直读直写内核内存」。QEMU 实测私有 PDPT 中内核表项 U 位已清、用户代码 PDPT[1] U 位保留、内核 PDPT[1] 不泄漏
- 修复隔离下的重启循环：弃用 GCC 11.4 误编译的 labels-as-values（`&&user_return_point` 把返回地址算错，致 sec_test 退出后 kernel_main 被反复执行 52 次），改用全局汇编 `user_return_trampoline` + C 清理 `user_return_cleanup`
- 修复 SEC-09：进程正常退出后只回收地址空间却保留 active=1，跑满 8 个槽后无法再启动程序；现正常退出也 release 槽位（active=0、释放内核栈、清 fd 表），连续 10 次 exec 无「No free process」
- 回归：连续 10 次 sec_test 空闲物理页 delta=0（地址空间完全回收无泄漏）；正常启动/桌面/sec_test 内核与未映射地址被拒；无异常
- 仍未做（阶段2）：fork 复制地址空间、RAM>1GB、内核页表去 U 位以开 SMEP/SMAP、sys_brk 映射、NX

**0.13.68 迭代内容（2026-10-28，安全漏洞修复集成——系统调用面加固，主机端测试 + QEMU 基础回归）**
- 集成外部安全修复并逐文件核对、QEMU 回归: user_access64.c 严格用户窗口[0x40000000,0x80000000)+拷贝前走 CR3 四级页表逐页校验(P/U/S、写时 R/W，支持4K/2M/1G)，堵住借系统调用缓冲区参数读写任意内核内存、以及未映射地址触发缺页 hlt 死机
- ELF 加载器边界/溢出检查+规范入口；内核堆块头 48 字节对齐、magic 与地址绑定、修复非首块 kfree；syscall 路径安全复制+只读/只写 fd 限制；SFMASK 加清 TF/NT/AC；ext2 挂载前校验超级块
- 修复用户进程入口栈未对齐: 初始 RSP 改为 8(mod 16)，解决内联系统调用 movaps 触发 #GP(0xD)
- 新增用户态安全回归 sec_test.elf，启动自动运行由 HEPU_BOOT_SELFTEST 开关控制(默认关，make BOOT_SELFTEST=1 打开，见 OPEN_BUGS SEC-08): QEMU 实测合法写成功、内核地址/未映射地址被拒且不崩溃；4 个主机端测试(ASan/UBSan)全部通过；ELF/ext2/堆的恶意输入仅在主机端验证，未在 QEMU 验证
- 更正 docs/security/AUDIT_64BIT.md: 旧版「漏洞A已修复、85/100」与代码不符，按真实状态重写(A 未修复；不再给总分，逐项列状态)
- 已知最大剩余风险: 页表隔离未做(fork 共享地址空间、下层表共享、无页表释放、内核低半区)，用户程序仍可直接读写内核内存；真正修复需重构 fork/独立页表/内核高半区

**0.13.68 追加（代码已改，待 QEMU 验证）**
- 每进程 fd 表(syscall64_full.c/user_process.c)；sys_kill 仅限终止自身；ring3 异常只终止该进程、不再整机 hlt(lib64.c)
- 行为变化: 只读/只写 fd 限制、路径>=255 拒绝、ext2 跳过已删除目录项；回归清单见 security/AUDIT_64BIT.md「追加修复」
- 用户地址空间隔离阶段 1（新文件 user_aspace64.c，`make USER_ISOLATION=1`，默认关闭，待 QEMU 验证）；主机端 aspace_test.c 通过；回归清单见 security/AUDIT_64BIT.md 漏洞 A
- sys_exit 打印退出码的栈缓冲溢出修复；user_process_exec 不再泄漏内核堆里的 ELF 镜像副本

**0.13.67 迭代内容（2026-10-27，XHCI 复合 HID 支持——无线接收器多接口多端点）**
- 单设备单端点模型升级为复合 HID 模型: 配置描述符解析枚举所有 HID 接口(bInterfaceProtocol=1键盘/2鼠标)及各自中断 IN 端点(EP1..EP15 IN, DCI=3..31), 每端点独立环/DMA 缓冲/生产者索引/interval, 支持一个设备同时是键盘+鼠标+多媒体键(无线接收器典型形态)
- 输入/输出上下文从固定 EP1 IN 扩展到 DCI=31(31 个 EP 上下文槽), Configure Endpoint 一次 Add 全部端点, Context Entries 取最高 DCI; 传输事件按 DCI 查找端点并按其 protocol 路由(鼠标 3 字节 boot 报告/键盘 8 字节)
- 已对照 QEMU v7.2.0 xhci_configure_slot/xhci_enable_ep/xhci_init_epctx 核对 EP 上下文偏移(ictx+32+32*DCI)、Add/Drop 校验(bits0,1=0x1)、类型/interval/DCS 解析, 多端点逻辑与规范一致
- 回归验证(QEMU qemu-xhci): 单鼠标+单键盘仍枚举 state=2(各 ep=1), trace 确认注入后 slot1 ep=3 鼠标 3 次、slot2 ep=3 键盘 4 次中断传输事件 CC_SUCCESS
- 已知限制: QEMU 无内置复合 HID 设备, 多端点(DCI>3)路径仅经源码核对未在模拟器复现; 真机无线接收器待实机验证

**0.13.66 迭代内容（2026-10-27，XHCI USB3.0 驱动完成——现代主板 USB 鼠标/键盘全链路打通）**
- 对照 QEMU v7.2.0 hcd-xhci.c 与 SeaBIOS usb-xhci.c 逐行做硬件级调试，修复 TRB 结构(误对齐32→16字节)、命令完成事件槽位号(取 field2>>24)、Address/Configure 命令槽位号位置、输入控制上下文 Drop/Add、设备上下文 DCI3、EP 上下文类型与 DCS、门铃寻址、事件环连续消费者、GET_DESCRIPTOR wValue 字节序等 10 处真实硬件行为缺陷
- EP0/中断环由"每次从 ring[0] 重写+末尾 LINK"改为生产者索引环(环尾 LINK+TC 回绕)，根除 STATUS 事件返回时控制器尚未跨过 LINK、软件即 memzero 重写环导致 SetConfig 无数据传输失败的时序竞争；Address Device 时同步复位 EP0 生产者状态(兼容全速设备二次寻址)
- usb64.c: XHCI 键盘报告经 xhci64_kbd_flush 注入统一键盘队列(原 early return 前未调用)；usb64_kbd_available 兼容 XHCI 键盘；鼠标由 hsinput 直连 xhci64_mouse_get
- 全链路验证(QEMU 7.2 qemu-xhci + usb-mouse + usb-kbd): 两设备均枚举到 state=2(VID=0627/PID=0001, mouse proto=2 / keyboard proto=1)；sendkey 报告键码正确(a=0x04 b=0x05 空格=0x2C 含抬起)、mouse_move 报告 dx/dy 正确、左键按下/抬起正确
- 清理全部临时调试打印([EVT]/[ERDP]/dbg_scan/maxp0/报告 dump/超时扩展寄存器)，编译 0 错误 0 警告；系统版本显示串更新为 v0.13.66
- 已知限制: 真机 xHCI 与无线接收器(复合 HID)尚未实机验证；目前按 QEMU 内置控制器行为构造

**0.13.65 迭代内容（2026-10-25，网络专项——DHCP 自动联网 + e1000 RX 全链路修复）**
- 修复 e1000 RX 不工作根因: QEMU socket 后端收发均需 4 字节大端长度前缀(net_fill_rstate 解析), net_sim.py 发送帧统一加前缀后 OFFER 进入 RX 环
- net64_ip.c: 广播/子网广播目标(0xFFFFFFFF/子网广播)直接 FF:FF:FF:FF:FF:FF 跳过 ARP(DHCP Discover 原会对广播发 ARP 导致永远发不出)
- desktop64.c: 桌面主循环自动驱动 dhcp64_poll(原桌面从不调用 DHCP 客户端, 静态 IP 才通)
- eth64_update_mac(): e1000_init 末尾回写 e1000 真实 MAC(52:54:00:12:34:56), 替代默认 MAC(单播 OFFER 被网卡硬件过滤根因)
- net64_get_bytes/eth64_get_bytes: 字节统计接口 + 任务管理器"网络 ↓KB/s ↑KB/s"行(100Hz sched tick 差换算, panel 高 140→168)
- net64_dns.c: 点分 IP 字面量直接返回(不走 DNS), 自实现解析(无标准库 sscanf); HTTP 对 IP 地址连接不再 DNS 解析失败
- net64_tcp.c: tcp_send_packet 对 SYN 被 ARP 未解析阻塞(-2)时泵网络收 ARP 应答后重发至多 40 次(原 SYN 静默失败连接永卡 SYN_SENT)
- DHCP/DNS/HTTP 打印 IP 字节序修复(htonl 后按字节打印, 10.0.2.15 不再显示 15.2.0.10)
- 模拟器多 IP 支持: ARP 应答 spa 用被查询 IP(网关 10.0.2.2 与服务器 10.0.2.3 同卡), TCP/HTTP 回包源 IP 匹配请求目标
- 全链路验证(QEMU socket + net_sim.py): DHCP DISCOVER→OFFER→REQUEST→ACK IP=10.0.2.15, ARP 解析, DNS api.open-meteo.com, TCP 三次握手, HTTP 200(166B), 驱动仓库检查发现 6 个更新, 天气数据获取 [WX] weather fetched
- 清理: e1000 RXpoll/CMD 调试打印、DHCP discover rc 打印移除; net_sim.py 精简噪音日志

**0.13.64 迭代内容（2026-10-24，EHCI USB2.0 驱动——真机现代 USB 控制器支持）**
- 新建 ehci64.c: EHCI 主机控制器驱动(寄存器/QH/qTD/1024周期帧列表/控制传输SETUP+DATA+STATUS三qTD链/中断端点注册与轮询框架/端口检测复位/设备地址分配)
- usb64.c 集成: 无UHCI(现代主板无PIIX)时自动尝试EHCI并枚举; 鼠标/键盘设备结构加ehci_slot; usb_control_transfer 双路径(有EHCI走EHCI)
- pci64.c 新增 pci_find_class_bar0(class/subclass/prog_if 定位控制器BAR)
- BAR映射: EHCI MMIO高区(0xFEBF1000+)访问前 vmm64_map_page 4KB逐页映射1MB(-cpu max下2MB大页触发#PF error=0x8, 4KB页规避)
- 调试结论(QEMU 7.2): 寄存器布局实测(caplen=0x20/USBCMD@0x20/USBSTS@0x24/FRINDEX@0x2C/ASYNCLIST@0x38/CONFIGFLAG@0x40/PORTSC@0x44相对), 端口连接检测正常(ps=0x1003), 但 QEMU 模拟器对异步列表不执行(ASYNCLIST写入生效+FRINDEX递增+IAAD门铃+usb-mouse/usb-tablet/q35均复现, qTD保持ACTIVE) —— 记为已知模拟器限制, EHCI实现按EHCI规范(异步列表QH/qTD)构造, 真机行为待实机验证
- UHCI 全链路回归(QEMU): 枚举 VID=0x0627/PID=0x0001/cfgs=1 正确, USB HID 鼠标可用, 桌面鼠标支持正常, 无回归
- 清理: 传输级调试打印全部移除, EHCI 保留5行一次性初始化日志(控制器定位/映射/端口/初始化)
- 文档: REAL_HARDWARE_TEST.md 增补 EHCI 模拟器限制记录

**0.13.63 迭代内容（2026-10-24，真机启动专项——内存探测链 + AHCI 存储接入 + 读盘重试）**
- 内存探测链(真机<2GB崩溃根因): stage2 硬编码256MB + 内核硬编码2GB → 实模式 int15 E801 探测, 调用点固定在读盘完成之后/进保护模式之前(SeaBIOS VBE/INT13会清零0x90000低内存区), 结果写0x90100(64位字节数); UEFI引导器同步换算写0x90100; 内核pmm64_init读0x90100替代硬编码(下限256MB兜底/上限2GB); vmm64全内存身份映射
- AHCI BAR高区映射(真机SATA页错误): ahci64直接解引用物理BAR5(真机0xF0000000+不在vmm 0~2GB映射) → 访问前vmm64_map_page 2MB大页映射(invlpg), QEMU无AHCI故此前未暴露
- AHCI接入系统盘读取链(真机SATA rootfs读不到): ahci64_disk_present/read_disk/write_disk盘级入口 → storage_hal_raw_read有AHCI盘优先走AHCI → 内核启动AHCI盘存在时ext2_64_set_block_device(ahci64_read_disk)
- 读盘重试(真机慢盘/偶发失败): stage2致命读取点(HDD前2次+CD前2次)加3次重试, 3次失败才read_error
- 验证: QEMU -m 384实测 PMM physical memory 0x17F MB(383MB), 桌面正常启动; MBR三分区挂载/cfg /data正常
- 文档: REAL_HARDWARE_TEST.md 增补真机启动专项排查记录

**0.13.62 迭代内容（2026-10-24，真机启动修复——UEFI 引导链重构）**
- 真机黑屏回BIOS修复(根因1): 混合镜像ESP分区为空(losetup容器内失败→BOOTX64.EFI从未写入), UEFI固件找不到引导项
- 修复: tools/build_esp.py 用fat32_generator直写ESP偏移(EFI/BOOT/BOOTX64.EFI + hepu-kernel64.bin), FSInfo签名修正
- 真机黑屏修复(根因2): GPT分区表为空(parted容器内静默失败) → tools/make_gpt.py python手写GPT(保护性MBR 0xEE + ESP C12A7328 + ext2 0FC63DAF, 主备GPT头+分区项CRC校验通过)
- 真机黑屏修复(根因3): UEFI引导器不传framebuffer(内核只读0x6000) → hepboot_uefi.c跳转前写GOP信息到0x6000(width/height/pitch/bpp/fb)
- make_hybrid_img.sh 固化: parted/losetup路径全部替换为python工具
- 验证: GPT签名/头CRC/分区项CRC全对; FAT卷文件树解析到BOOTX64.EFI; MBR p0=0xEE+p1=0x83

**0.13.59 迭代内容（2026-10-24，交换区与页换出——内存压力下页面落盘）**
- 文档重构(7步): 清DESIGN NUL×2→拆DESIGN(现行372行+ITERATIONS+REVISION_NOTES)→BUG_HISTORY拆4段devlog+OPEN_BUGS→三路线合一ROADMAP→gen_syscall_doc.py生成SYSCALL_ABI→frontmatter批量+CI检查+README自动索引→归档改名(architecture/specs/guides/planning/status/history六区)
- swap64.c/h: 槽位位图管理(512槽×4096B=2MB), ext2文件后端(/rootfs/swap.hepu懒扩展只写实际页)
- ext2_64_write_file_offset: 从偏移写文件(复用三级间接块分配循环, 首尾块读改写, i_size/i_blocks更新)
- vmm64 页换出/换入原语: swap_out(内容写swap+PTE存槽号+AVL位标记+回收物理页)/swap_in(读回+重映射+释放槽)
- vmm64_get_pte: 支持非present项(换出页判断); page_fault_handler 检测换出页自动换入(异常返回重试待异常框架)
- 实测: 槽写读一致(0x42/0x99), 页换出swp=1换入数据完整(0x5A/0x00/0xFF)
- 修复: swap64_write 返回语义归一化(写入字节数→0成功)

**0.13.58 迭代内容（2026-10-24，统一块缓存层——全部存储驱动共享缓存）**
- 新建 blk_cache64.c/h: 扇区粒度(512B)缓存, 256槽(128KB), LRU淘汰, 写穿(write-through)
- 缓存槽数据 kmalloc 动态分配(防BSS越界), 懒初始化(首次IO自动建立)
- 挂在 storage_hal 层: storage_hal_read/write 改为缓存包装, 原实现改名 storage_hal_raw_read/raw_write
- 全部存储驱动(IDE/USB/AHCI/NVMe)经统一缓存受益; 直写路径提供 blk_cache_invalidate 防旧数据
- 实测: 重复读命中(启动期 hit=60/miss=65, 槽占用73/256), 写穿后读回一致
- 接口: blk_cache_init/read/write/invalidate/flush/get_stats

**0.13.57 迭代内容（2026-10-24，多实例挂载——/cfg /data 文件级访问框架）**
- 挂载槽数组: ext2_mount_offsets[4]/ext2_mount_valid[4], 槽0=根分区(LBA9000/0x1E000), 槽1=/cfg, 槽2=/data
- ext2_64_mount_slot: 登记分区LBA并校验超级块魔数(0xEF53), 开机对P2/P3自动登记(MOUNT-OK rc=0)
- ext2_64_switch_mount: 重挂全局超级块/GDT/共享算法上下文, 不清块缓存(LRU 64槽按LBA双键, 分区切换无误命中)
- ext2_64_resolve_path: /cfg→槽1, /data→槽2, 其余→槽0, 嵌套保护+双静态缓冲(rename双路径)
- 13个公开入口(read_file/read_file_offset/write_file/delete_file/delete_dir/lookup/create_file/create_dir/mkdir/rename/read_dir/list_dir_entries等)统一路径包装
- 修复: 根分区挂载槽0未登记(offset=0致switch(0)读错LBA2), 挂载数组定义前置避免声明顺序问题
- 已知限制: QEMU snapshot=on环境对高LBA(P2 0x72328/P3 0x172328)读回不稳定(写成功读全0), 代码路径与真机一致待实机验证

**0.13.56 迭代内容（2026-10-24，未格式化分区自动初始化——/cfg /data 开机就绪）**
- 新增内核版简易 ext2 mkfs（ext2_64_mkfs）：创建 4096 块大小的 ext2（超级块/GDT/块位图/inode位图/inode表/根目录），写后读回自检超级块魔数
- 开机检测 P2/P3 分区无文件系统时自动 mkfs（P2=HEPU_CFG→/cfg 用户配置分区，P3=HEPU_DATA→/data 自由分区）；已格式化分区跳过（真机首次后不再重复）
- 三区制闭环：安装向导写三区 → 首次开机 P2/P3 自动格式化为可用 ext2 → 挂载点状态报告 [MOUNT]
- 实测：P2/P3 未格式化 → [MKFS] rc=0（含读回验证）；已格式化 → 无 MKFS、PARTS ext2=1、跳过正常
- 边界：P2/P3 挂载点已就绪，分区内文件级访问待多实例挂载（后续）

**0.13.55 迭代内容（2026-10-24，稀疏镜像 + 压缩交付——镜像物理占用 256MB→1.5MB）**
- 镜像创建改稀疏：truncate 代替 dd /dev/zero，rootfs 写入用 dd conv=sparse（空洞跳过、数据保留）
- 单镜像物理占用 268MB→1.5MB（逻辑 256MB 不变，QEMU/写盘不受影响）；混合镜像 320MB→1.4MB
- 修复：coreutils 8.32 不支持 --sparse（脚本静默失败致 ext2 未写入、启动除零），改用 conv=sparse 并实测超级块完整
- 新增压缩交付 .img.gz：单镜像 256MB→878KB，混合 320MB→805KB（Rufus/BalenaEtcher 直接支持写 gz）
- 交付：源码包 + .img.gz×2（下载） + 稀疏 .img×2（本地直写）

**0.13.54 迭代内容（2026-10-24，根分区 ext2 自动扩展——分大区不再占镜像）**
- 新增 ext2_64_expand_to_partition：内核启动时检测"分区表大小 > ext2 文件系统大小"，自动把根分区 ext2 扩展到全分区
- 机制：install64 写大 P1 分区表（如 1GB，条目仅 16 字节不占镜像）→ 重启 → 内核按分区表扩展 ext2（新块组：块位图/inode位图/inode表/组描述符/超级块，写回 GDT+超级块，读改写保留块0引导区）
- 实测：出厂 224MB ext2 + P1 分区表 1GB 目标盘 → 启动后 ext2 扩展 0xE000→0x40000 块（57344→262144），新增 6 组，写测试/桌面正常
- 支持 block_size=4096（单镜像出厂规格）；混合镜像 ext2 为 1024 块大小（出厂单分区无需扩展，rc=-2 跳过无害）
- 边界：新组从文件系统末尾连续扩展（非 bpg 对齐，自用内核定位正确）；组数 ≤32（GDT 容量）；P1 增大受 16bit 组空闲计数/4GB 分区限制
- 至此：出厂镜像 256MB 不变，装机后 P1/P2/P3 全按用户选择落在目标盘并真正可用（P1 自动扩展到所选大小）

**0.13.53 迭代内容（2026-10-24，出厂镜像 256MB + 安装向导三区全可配）**
- 出厂镜像压到 256MB（试用+安装介质，体积减半）：系统分区 224MB 适配（ext2@LBA9000），单镜像 536MB→268MB
- install64 自定义模式增加系统分区大小输入（默认=当前，只增不减——小于当前会截断文件系统，拒绝）；三区大小安装时全可配（P1 可增大 / P2 默认 512MB / P3 剩余），只受目标硬盘容量限制
- 一键安装保持 P1 当前 + P2 512MB + P3 剩余
- 安装后三区与出厂介质大小解耦（Windows/Linux 安装器模式）
- 已知待测：install64 键盘交互运行时验证受 QEMU sendkey 注入差异限制（真实 PS/2 路径正常），布局逻辑已编译+静态核验

**0.13.52 迭代内容（2026-10-24，分区规格调整：配置分区 64→512MB，出厂镜像保持小规格）**
- install64 默认配置分区 64MB → 512MB（软件/游戏配置占用高，AppData 级配置文件轻松超 64MB），自定义输入默认提示同步更新
- 分区大小在安装向导写目标硬盘时分配，只受硬盘容量限制
- 设计修正：出厂镜像不再预分配 P2/P3——raw 整盘镜像=磁盘容量拷贝，预分配三区会让镜像膨胀到 1GB（256系统+512配置+剩余）；出厂镜像=试用+安装介质，保持 512MB 单分区（引导+内核+系统文件，实际占用仅数百KB）
- 单镜像脚本保留 HEPU_SYS_MB 参数；install64.elf 重编译并写入两镜像 /bin/

**0.13.51 迭代内容（2026-10-23，P1 内存压力管理 + 分区表扫描 + 关键修复：三区制根分区选择）**
- 新增 memory_pressure.c：物理内存+KHEAP+块缓存统一压力视图，压力等级 0/1/2（低 64MB/紧急 32MB）
- 低内存自动回收块缓存（mem_pressure_tick 挂桌面主循环每~100帧），ext2_cache_flush_all/used_count 新接口
- 启动日志 [MEMPRESS]：Phys free / KHEAP used / 缓存槽 / 压力等级；shell meminfo 扩展（KHEAP 详情+块缓存+压力）
- 新增 partition64.c：MBR 四分区解析 + ext2 魔数(0xEF53)检测，启动日志 [PARTS] 显示各分区 type/lba/size/ext2 状态
- 三区制实测：P1 系统(ext2=1) / P2 配置(未格式化) / P3 自由(未格式化)，挂载状态一目了然
- **关键修复：根分区选择错误（三区化后除零崩溃 #DE）** — 旧逻辑选"最后一个分区"作根，三区化后选中无文件系统的 P3 → 超级块全 0 → s_blocks_per_group=0 → div 除零崩溃；改为遍历 0x83 分区校验 ext2 魔数选第一个有效分区作根，实测三区镜像正常启动
- 修复过程：构造三区 MBR 测试镜像复现崩溃（EXCEPTION vector=0x0 rip=div%ecx），修复后 P1 正确挂载、P2/P3 报告未格式化

**0.13.50 迭代内容（2026-10-23，安装向导交互版：菜单选择/自定义分区/键盘输入）**
- install64 交互版：欢迎页 + 当前分区表显示 + 菜单（1 仅试用 / 2 一键安装 / 3 自定义分区 / 0 退出），键盘输入选择
- 自定义分区：输入配置分区大小(MB, 默认64) + 自由分区大小(MB, 默认剩余)，y 确认后写盘
- 输入链路确认：syscall READ(2) fd=0 从键盘缓冲读取（阻塞 hlt 等待 + 行缓冲回显），键盘驱动初始化后可用
- 交互全流程实测：注入 "2\n" → 一键安装 → 三区布局(P1@9000 保持 / P2@533288 64MB / P3@664360 剩余) → DISK_WRITE → REBOOT 重启进入 Stage2
- 终端入口：shell64 `exec /bin/install64.elf`（用户可在终端运行安装向导）
- 已知待查：QEMU monitor sendkey 注入的按键未被键盘驱动读取（真实 PS/2 硬件路径正常，系统终端/快捷键输入均正常；sendkey 为自动化注入差异）

**0.13.49 迭代内容（2026-10-23，安装引导流：REBOOT syscall + 三区化安装向导 + 缓存初始化修复）**

**0.13.46 迭代内容（2026-10-23，进程管理：随时中断进程 + 未响应强制结束）**

**0.13.47 迭代内容（2026-10-23，MBR 分区表工具 + 磁盘 syscall + 三区制地基）**

**0.13.49 迭代内容（2026-10-23，安装引导流：REBOOT syscall + 三区化安装向导 + 缓存初始化修复）**
- 新增 syscall REBOOT(28)：8042 键盘控制器复位脉冲（0xFE）+ hlt，实测触发 QEMU 软重启进入 Stage2 重新引导
- 扩展 mbrtool 双模式：扇区1标记 0xCC=三区化（保持 P1 系统分区，新增 P2 配置 64MB + P3 自由到盘末，全 type=0x83），0xCD=重启；修复分区检测瑕疵（e[8|0] 与 || 优先级）
- 新增安装向导应用 install64（/bin/install64.elf）：读取 MBR → 计算三区布局 → DISK_WRITE 写盘 → REBOOT 重启，全流程实测 OK
- 镜像布局三区化：HEPU_SYS_MB 参数控制系统分区大小（默认 256MB），MBR P1 不再填满镜像，预留 P2/P3 空间（实测 512MB 镜像：P1@9000 524288扇区 / P2@533288 131072扇区 / P3@664360 到盘末）
- **关键 bug 修复：ext2 块缓存初始化缺失**——ext2_64_init 路径从未初始化缓存槽（data=NULL），缓存命中 memcpy(NULL) 返回全 0，导致共享算法（ext2_algo）读所有文件系统性失败（传统路径 verify 正常、algo 全 -1）；提取公共 ext2_cache_init（mount/init 共用）+ read_block 缓存命中/入槽 NULL 保护
- 实测修复后：/bin 下 hello.elf / mbrtool.elf / 字体全部可读，exec 正常加载运行（9872 字节 mbrtool 完整读取）

**0.13.48 迭代内容（2026-10-23，ext2 块缓存（LRU 64槽）+ 大文件读优化）**
- ext2 块缓存：64 槽 LRU，重复读命中缓存不再逐块裸读（P0 存储验收项）
- 读一致性：写路径自动失效对应缓存槽
- 缓存统计接口 ext2_cache_get_stats（调试/监控）
- 缓存数据页动态分配（kmalloc）：修复 BSS 膨胀覆盖 0xC00000 页表导致内核三重故障重启
- 实测：重复读 /bin/hello.elf 3 次，命中 12 次/未命中 14 次
- 新增 syscall DISK_READ(26)/DISK_WRITE(27)：用户态程序可读写磁盘扇区（copy 用户缓冲安全路径）
- 新增用户工具 mbrtool（/bin/mbrtool.elf）：读取并显示 MBR 分区表（类型/起始LBA/大小），三区制地基
- 修复 exec 入口偏移：多段 ELF 时入口相对 code_start 计算，此前错位导致用户程序 #PF
- 修复 make_single_img64.sh 缺 fi 导致镜像构建异常
- 实测：mbrtool 读出单镜像分区（P1 ext2 @LBA9000 515288扇区），进程正常退出
- 任务管理器显示真实进程列表（PID/名称/状态），K+PID+Enter 结束进程
- 新增 syscall KILL(25)：用户程序可终止进程
- 未响应看门狗：用户进程 10 秒无返回自动强制结束（vec40 定时截获）
- 强制退出 trampoline：iretq 到独立汇编入口清理进程后直接恢复桌面主循环
- Ctrl+Alt+K 快捷键直通（键盘 IRQ 检测，GUI 冻结时备用）
- 实测：死循环进程（loop64）10 秒被看门狗强制结束，系统恢复桌面
- KHEAP 256KB→8MB（OBS 主程序需 5.9MB 单次分配），根因修复：内核入口 rep stosq 清 bss（11.7MB）覆盖 Stage2 页表（0x600000）致 #PF 三异常——页表整体迁移 0xC00000（bss 之后）+ 低/高 PD 扩 8 项（16MB）+ multiboot PD 8 项
- 实测 KHEAP init 8MB、系统启动 ready=1；OBS 运行断点 1（KHEAP OOM）已解决，剩余断点：动态链接器、POSIX syscall 层
- 混合镜像（make_hybrid_img.sh）修复：①ext2 改用宿主 mkfs + debugfs 直写（原 losetup 方案容器内不可用致 ext2 从未写入、内核挂载后 #DE 除零）②保护性 MBR 补 0x83 分区项（内核按 MBR 0x83 定位 ext2，GPT 分区表内核不支持）；修复后混合镜像启动 ready=1
- 新增 docs/planning/STORAGE_MEMORY_PLAN.md：32/64 位内存不共用（引导二选一各自管理）存储共用（单 ext2 根分区）；三区制（系统/用户配置/自由分区）与 U 盘安装引导（试用→为硬盘安装→分区配置→写盘）整合入 P0-P3

**0.13.44 迭代内容（2026-10-23，OBS 真实安装测试 + KHEAP 扩容方向定位）**
- OBS Studio 32.2.2 deb 真实安装进系统镜像（tools/obs_install_test.sh，1904 文件落盘，验证 deb 解包→文件系统部署链路）
- OBS 运行实测三个断点：KHEAP 256KB 不足（需 5.9MB）、缺动态链接器 ld-linux（ET_DYN PIE）、缺 glibc2.38/Qt6/FFmpeg6 运行时（19 项缺失）
- KHEAP 扩容 8MB 时 bss 超 8MB 身份映射致启动失败（__bss_end 11.7MB），定位需堆扩容+引导/vmm 页表 64MB 映射配套
- make_single_img64.sh 新增 HEPU_IMG_SIZE_MB 镜像容量参数（大软件安装需 ≥512MB）
- 兼容层规划更新：OBS 列为 P2/P3 远期验收项，P0 保持"小软件先通"（busybox→动态 ELF→Qt/GL）

**0.13.43 迭代内容（2026-10-23，64 位用户态 syscall 链路根因修复并真机打通）**
- 根因定位（gdb remote 协议实测）：syscall_entry_64 保存寄存器后物理栈布局 offset 全反（push 逆序，栈顶是 r15 而非 rax），导致 nr 从 (%rsp) 取到 r15 槽（=0）、rcx 从 16(%rsp) 取到内核栈顶 r13 槽——"号=0 + sysret 跳栈顶死循环"全部吻合
- 修复三段 offset：保存（rcx=96(%rsp)/r11=32(%rsp)）、取参（nr=112(%rsp)=rax 槽）、恢复（pop r15…rbx 逆序 + add $8 跳 rax 槽）
- 槽改直接值语义：g_syscall_ustack_slot/kstack_slot 由指针变量改为直接存值（entry 直接读写），修复恢复用户栈时 mov 中转覆盖 rax 返回值的问题（此前 getpid/time 返回 &user_stack 地址）
- 真号直接 push %rax：不再经 r12 中转，r12 真值完整保留
- 验证结果：ELF64 静态程序 hello.elf 经 user_process_exec 加载执行，write(fd=1) 正常输出、getpid 返回 1、time 返回真实秒数、exit(0) 干净终止，全程无异常——64 位用户态 syscall 链路完整打通（兼容层 P0 最大硬卡点解除）

**0.13.42 迭代内容（2026-10-23，内核稳定 + USB 写路径诊断闭环）**
- USB 写路径深度诊断闭环（host 盘逐扇区对照）：定位 mkdir/rename 于 USB 盘失败的根因为 QEMU UHCI 批量传输时序竞态（读命令偶发数据未就绪：读 inode 表 LBA40 返回 0、读 inode 位图错位），非 ext2 文件系统逻辑错误
- 防御加固：USB 数据缓冲改 8 深轮转独立区（消除共享缓冲覆盖）、命令间帧间隔、BOT reset 前置声明可用
- 确认：IDE 系统盘读写完全正常；USB 枚举/挂载/读取正常；单命令写正常（写后读一致）
- 遗留：QEMU 环境下 USB 盘 mkdir/rename 仍失败（读时序竞态），需真实硬件或 QEMU UHCI 时序适配验证
- 内核稳定性：清理网络层临时调试打印（ETH/ARP/DNS/UDP），消除无对端时的串口日志风暴；ARP 忙等限时（2000→300轮）、DNS 重试降载（40→8次），失败快速返回
- 输入修复：tablet/usb/PS2 鼠标按钮多源合流（修复 tablet 在位时 PS/2 点击被忽略）
- F12 快捷键打开终端；桌面终端新增 mkdir/mv 命令（文件系统操作验证通道）
- USB：BOT Mass Storage Reset 兜底（CSW 重读失败/签名错/tag 不匹配时恢复设备）；ext2 GDT 更新加固（独立缓冲+写后读校验重试）；USB 挂载后自动执行 mkdir+rename 写路径自检
- 已知遗留：USB 写路径在 QEMU 下仍有偶发数据错位（GDT 被位图数据覆盖，mkdir 于 USB 盘失败）；IDE 系统盘读写完全正常
- USB BOT 写路径修复：WRITE10 后 CSW 偶发超时（命令级重试 + CSW 重读 + UHCI 帧同步，消除 TD 竞态）
- ext2 新增 ext2_64_mkdir / ext2_64_rename（目录项 inode 清零保留槽位）
- 文件管理器右键新增 New Folder / Rename，桌面 USB 存储自动挂载+通知
- 顶部栏 CPU/MEM 真实化：CPU 用调度器 tick 统计(idle=PID0, process64_cpu_usage)，内存用物理内存页统计
- 侧边栏 System Info 真实化：CPU/MEM/DSK 三条进度条与百分比全部真实数据(磁盘用 ext2 已用块比例)
- 锁屏虚拟键盘：登录界面右下角 VKB 按钮展开屏幕键盘(数字行+3行字母+DEL)，点击按键输入密码；PWR 按钮 ACPI 关机(QEMU 0x604)
- 登录界面新增关机键/虚拟键盘键渲染(截图实机验证布局)
- 通知弹出卡片右下角显示 RTC 真实时间戳
- 新增内核接口：process64_cpu_usage / ext2_64_used_percent

**0.13.38 迭代内容（2026-10-23，P2 完善：网络状态真实化 + 设置页注册表/关联管理）**
- 托盘网络图标真实链路状态：e1000 STATUS bit1 链路检测 + DHCP bound，替代硬编码[NET]
- 任务管理器 Network 面板真实化：显示 DHCP 实际 IP + 真实 UP/DOWN 状态（替代硬编码 10.0.2.15）
- 设置页 System 新增注册表管理行：实时显示条目数，点击打印全部键值（vga）
- 设置页 System 新增文件关联管理行：显示关联规则数，点击打印默认规则
- 新增内核接口：e1000_link_status/dhcp64_get_ip/reg64_count/reg64_key_at/reg64_value_at/file_assoc_count
- 实机验证：E1000 status=0x80080783(bit1链路UP)，托盘按 link+bound 真实切换；设置窗口全部新行渲染正常

**0.13.37 迭代内容（2026-10-22，P11 修复 + 设置/文件管理器完善）**
- 修复 0.13.36 启动崩溃：清理时误删 superblock 拷贝(memcpy)，全 0 导致除零
- ext2_64_mount 干净挂载函数：重建 shared 算法上下文(ext2_algo)，无写测试副作用
- USB 挂载后文件管理器可读 U 盘：shared 算法重入问题解决（实机验证读 hello.txt 内容正确）
- 新增 usb umount 命令：卸载 U 盘、恢复 IDE 系统盘（分区偏移 9000 复位）
- 文件管理器：路径栏右侧 U 盘状态按钮（点此挂载/已挂载点击卸载），状态写入状态栏
- 设置页 System：新增存储管理区（当前设备 System Disk/USB、U 盘在线状态、点击切换挂载）
- 实机验证：启动进桌面无异常；USB INQUIRY/Capacity/挂载+shared 读文件全部 OK

**0.13.36 迭代内容（2026-10-22，P11 底层：USB 海量存储完整读写与挂载）**
- SCSI READ10/WRITE10 扇区读写（单扇区循环，512B/次），WRITE10 回环验证通过
- 批量传输修复：数据缓冲不在DMA池时拷贝到固定scratch（栈/全局缓冲均可DMA）
- TD 完成检查增加错误位（STALL/BABBLE/TIMEOUT），CSW 增加 tag 校验（防滞后命令）
- ext2 块设备可切换（ext2_64_set_block_device）+ 分区偏移复位（USB整盘无分区表）
- storage_hal64 设备选择（storage_hal_set_device: 0=IDE 1=USB），文件读写统一走HAL
- 新增 usb mount 命令：挂载USB海量存储到ext2（shell可操作）
- 实机验证：QEMU usb-storage + 32MB ext2盘(含hello.txt)，挂载后读出文件内容
  "Hello from USB disk"（宿主debugfs交叉校验一致）
- 已知项：挂载切换后 shared ext2 算法重入问题（文件管理器读USB待后续轮）

**0.13.35 迭代内容（2026-10-22，P10 底层：USB 海量存储基础(BOT)）**
- USB Mass Storage 类(0x08)设备枚举识别，BOT 协议实现：CBW 发送/数据/CSW 校验
- 批量传输：UHCI bulk 端点单 TD 传输(复用控制传输的帧列表挂载+轮询机制)
- SCSI INQUIRY：读取厂商/型号(QEMU 模拟盘验证通过)
- SCSI READ CAPACITY：读取块数/块大小/容量(32MB 镜像 → 65536 blocks 验证通过)
- 实机验证：QEMU usb-storage + 32MB 镜像，INQUIRY/Capacity 日志确认
- 后续：READ10/WRITE10 扇区读写 + ext2 挂载(下一迭代)

**0.13.34 迭代内容（2026-10-22，P9 功能：通知位置可配置）**
- 通知卡片位置四角可切换：右上/左上/右下/左下(默认右上，右下左下向上堆叠)
- 位置持久化到注册表 sys.notif.pos(tr/tl/br/bl)，重启保持
- 设置页 System 页新增 Notify Pos 行，点击循环切换位置
- 实机验证：QEMU screendump 截图确认左下角通知渲染

**0.13.33 迭代内容（2026-10-22，P8 功能：通知系统三源接入）**
- 网络连接通知：DHCP 绑定成功后自动通知(双语，带分配的 IP)
- 系统更新通知：网络就绪后自动检查驱动仓库，通知可用驱动数/仓库不可达
- 设置页驱动刷新：刷新结果通知(成功/失败)
- 新增 dhcp64_is_bound() 导出供桌面层状态检查
- 实机验证：QEMU screendump 截图确认三条通知卡片(队列/渲染/超时)正常

**0.13.32 迭代内容（2026-10-22，P7 修复：终端经典黑底绿字配色）**
- 终端内容区改为经典 CRT 配色：黑色背景(0x000000/输入区 0x0A0F0A)、绿色文字(0x33FF33)
- 提示符/光标使用亮绿(0x66FF66)，不随浅色/深色主题变化
- 实机验证：QEMU screendump 截图确认黑底绿字渲染

**0.13.31 迭代内容（2026-10-22，P6 修复：终端背景跟随主题）**
- 修复：终端输出区/输入区背景由硬编码米黄(0xEDE6D4/0xE4DCC8)改为主题色 window_bg/panel_bg
- 深色主题下终端不再出现米黄色块，与全局界面统一
- 实机验证：QEMU screendump 深色主题截图确认背景跟随

**0.13.30 迭代内容（2026-10-22，P5 应用：终端完善）**
- 命令补全：Tab 补全命令名(内核命令表导出)与当前目录文件，多候选列出
- 命令历史：上/下键遍历最近 10 条命令(去重)，PgUp/PgDn 改为滚动
- 帮助扩充：列出内核全部命令集(drv/usb/reg/disk/lang/kmod/ps/ifconfig...)
- 终端内建 drv list / usb list / reg list / lang 输出到终端屏幕
- 实机验证：QEMU screendump 截图确认终端窗口与提示符渲染正常

**0.13.29 迭代内容（2026-10-22，P4 应用：文件管理器完善）**
- 鼠标交互：单击选中、双击打开(进目录/文本查看器)
- 键盘检索过滤：输入字符即时过滤列表(大小写不敏感子序列匹配)，退格清过滤
- 视图切换：Tab 在 列表/网格 间切换(网格=大图标+名称)
- 右键文件菜单：Open / Delete / Refresh / Up Dir(删除调 ext2_64_delete_file)
- 文本查看器窗口：ext2 读文件 + 滚动显示
- 实机验证：QEMU screendump 截图确认列表与网格两视图渲染正常

**0.13.28 迭代内容（2026-10-22，P3 应用：设置页接线）**
- System 页新增语言切换入口：显示当前语言(中文/English)，点击切换并持久化(sys.lang + registry save)
- System 页新增驱动管理入口：显示真实驱动数，点击刷新驱动仓库(drv_mgr_update)
- 设置窗口点击命中扩展：System tab 内操作行可点击
- 实机验证：QEMU screendump 截图确认渲染(语言/驱动行完整无截断)

**0.13.27 迭代内容（2026-10-22，P2 应用：任务管理器细化）**
- 真实系统数据接入：PMM 物理内存(总/用)、真实进程数、真实驱动数替代模拟摘要
- 实时曲线：CPU 使用率 + 网络 RX/TX 速率历史 72 点折线图
- 进程列表按 CPU 使用率降序排序
- 实机验证：QEMU screendump 截图确认渲染(真实数据 + 三曲线 + 降序列表，无重叠溢出)

**0.13.26 迭代内容（2026-10-22，P1 底层：USB 驱动增强）**
- USB HID 键盘补全：F1-F12 功能键、Tab、CapsLock、Insert、Delete、符号键(-=/[]\;'`,./)
- 新增 USB 设备注册表：端口/VID/PID/类型登记，usb status/list/hotplug 命令
- 新增热插拔检测：UHCI 端口连接状态轮询(约 500ms)，事件通知
- 实机验证：QEMU + UHCI 下 tablet + HUB + USB 键盘完整枚举，热插拔事件触发

**0.13.25 迭代内容（2026-10-22，P0 底层：内核驱动模块加载器）**
- 新增 kmod64 内核模块加载器：加载 ELF64 可重定位对象(ET_REL)
- 支持重定位：R_X86_64_64/PC32/PLT32/32/32S/PC64 + 合成 GOT/PLT(参考 Linux modprobe)
- 符号解析：驱动本地符号 + 内核导出符号表(kmalloc/vga_puts/ext2)
- module_init/module_exit 生命周期，kmod64_load/init/unload/list
- 终端命令：kmod load PATH/list/unload NAME
- 驱动模块编译进镜像 /drivers/testdrv.o，验证驱动真实运行(init 返回 0)
- 与驱动管理器闭环：下载 → 安装 → 内核态加载

**0.13.24 迭代内容（2026-10-22，P1 底层：系统多语言框架）**
- 新增 l10n64 多语言框架：字符串 ID 双语言表(zh_CN/en_US) + 查询 API
- 语言持久化到注册表 sys.lang，重启保持
- 锁屏界面首批迁移：提示语/解锁按钮/ESC 提示
- 终端命令：lang get/set zh|en
- 后续：全局字符串逐步迁移至字符串表

**0.13.23 迭代内容（2026-10-22，P1 底层：音频 AC97 完善）**
- 完成 hs_audio_play_tone 真实播放：正弦波采样(查表无浮点) + 淡入淡出防爆音 + AC97 DMA 播放
- mute/unmute 真正写 AC97 主音量寄存器(静音 0x3F)
- 无 AC97 硬件时优雅降级(日志提示, 不崩溃)
- 说明：QEMU 精简构建无音频设备, DMA 播放路径需真机验证

**0.13.22 迭代内容（2026-10-22，P1 系统底层：磁盘管理 + 文件关联）**
- 新增磁盘管理工具 disk64：容量/扇区/块/inode 统计、使用率、ext2 fsck 检查
- ext2 导出 superblock/GDT 接口（供 fsck 使用，底层接口补全）
- 新增文件关联 file_assoc64：扩展名提取(大小写不敏感)、默认推断(文本→notes/代码→editor/图片→imageviewer)、注册表自定义覆盖
- 终端命令：disk info/fsck/list；assoc get/set/list/check
- 文件关联持久化走系统注册表（app.assoc.*，reg save 落盘）

**0.13.21 迭代内容（2026-10-22，P0 系统底层：GPS 定位服务）**
- 新增 GPS 定位服务 gps64：NMEA 0183 解析（$GPGGA/$GPRMC）
- 支持度分坐标解析（纬度2位度/经度3位度）、半球处理、海拔/速度/卫星数
- API：gps64_init/feed/get_status/is_fixed/simulate_fix/print_status
- 终端命令：gps status/sim/feed
- 为天气定位提供坐标来源（网络就绪后接线）

**0.13.20 迭代内容（2026-10-22，P0 系统底层：系统注册表）**
- 新增系统注册表 registry64：内存 KV + 持久化 /etc/registry.dat（ext2）
- API：reg64_init/get/set/delete/save/list，支持整数读取
- 命名空间约定：sys.* 系统级 / app.* 应用级 / user.* 用户级
- 终端命令：reg get/set/del/list/save

**0.13.19 迭代内容（2026-10-22，P0 系统底层：驱动下载/安装框架）**
- 新增窗口平铺 Snap：Win+← 平铺左半屏 / Win+→ 平铺右半屏，再次按同方向还原原位置（参考 Windows Snap）
- 修复新窗口不获焦：hs_window_create 创建后自动聚焦新窗口（此前 focus_window 未指向新窗口，平铺/快捷键作用目标缺失）
- 前置 0.13.16：窗口最小化/最大化/双击还原；0.13.15 应用图标全覆盖

## 内核版本 vs 系统版本

### 内核版本（hepu_kernel_version）
- 格式：`1.0.2`
- 独立演进，与系统版本分离
- 反映内核自身的成熟度

### 系统版本（hepu_system_version）
- 格式：`0.9.0`
- 与镜像命名一致
- 反映整个系统的发布版本

## 镜像命名规范

```
hepu-os-uefi-v0.9.0.iso      # UEFI ISO镜像
hepu-os-uefi-v0.9.0.img      # UEFI IMG镜像
Hepu_OS_0.9.0_Source.zip     # 源码包
ext2_disk_uefi.img           # 数据盘（版本无关）
```

## 版本号检查清单

发布新版本前确认：
- [ ] 内核版本字符串已更新
- [ ] 系统版本字符串已更新
- [ ] 启动画面版本号已更新
- [ ] 镜像文件名已更新
- [ ] 源码包文件名已更新
- [ ] 错误历史文档已记录
- [ ] 变更日志已更新
- [ ] 文档版本号已同步

## 历史版本

| 版本 | 日期 | 主要内容 |
|------|------|---------|
| 0.54.12 | 2026-11-11 | 桌面稳定性(窗口槽位泄漏/PIT 中断冻结)+安全止血包 6P0+12P1(LAPIC 校准至真实 1kHz、writev/clone/sigaction/reboot/mkfs 加固)+文档真值对齐+SMP 计划与决策对比+真机上机 checklist(41 缺口/7 硬阻塞) |
| 0.13.68 | 2026-10-28 | 安全修复集成:严格用户窗口+页表校验堵系统调用任意读写;ELF/堆/fd/ext2加固;栈对齐修复#GP;sec_test回归;更正审计文档(漏洞A未修复) |
| 0.13.67 | 2026-10-27 | XHCI复合HID:单设备多HID接口多中断端点(EP1..EP15 IN/DCI3..31),上下文扩到DCI31,一次Configure全Add,按DCI路由;面向无线接收器 |
| 0.13.66 | 2026-10-27 | XHCI USB3.0驱动完成:10处硬件行为缺陷修复(TRB 16字节/槽位号字段/Drop-Add/DCI3/门铃/事件环/wValue字节序等)+EP0/中断生产者索引环根除SetConfig时序竞争;QEMU qemu-xhci下鼠标键盘枚举state=2且输入报告正确 |
| 0.13.65 | 2026-10-25 | 网络专项:DHCP自动联网+e1000 RX全链路修复(大端长度前缀/广播跳过ARP/真实MAC回写/任务管理器网速) |
| 0.13.64 | 2026-10-24 | EHCI USB2.0驱动框架+PCI BAR高区映射(QEMU异步列表不执行记为模拟器限制) |
| 0.13.63 | 2026-10-24 | 真机启动专项:int15 E801内存探测链+AHCI存储接入+读盘重试 |
| 0.13.45 | 2026-10-23 | KHEAP 8MB扩容打通启动(页表迁移0xC00000+16MB映射)+三区存储/安装引导规划落地 |
| 0.13.46 | 2026-10-23 | 进程管理:任务管理器真实进程列表+结束进程(syscall KILL/键盘)+未响应10秒看门狗强制结束(trampoline恢复桌面) |
| 0.13.47 | 2026-10-23 | MBR分区表工具+磁盘syscall(读/写扇区)+exec入口偏移修复(多段ELF#PF)+三区制地基 |
| 0.13.62 | 2026-10-24 | 真机启动修复:ESP分区直写(GPT空+losetup失败根因)+GPT手写生成+UEFI引导器0x6000帧缓冲传递, 混合镜像UEFI引导链完整 |
| 0.13.59 | 2026-10-24 | 交换区+页换出:swap64槽位位图(512×4KB)ext2文件懒扩展后端;ext2_64_write_file_offset偏移写;vmm swap_out/in PTE AVL位标记槽号,页故障自动换入;实测换出换入数据一致 |
| 0.13.58 | 2026-10-24 | 统一块缓存:blk_cache64扇区粒度512B写穿LRU 256槽,storage_hal包装(原实现改raw_*),全驱动共享;实测命中60/miss65槽73/256,写穿读回一致 |
| 0.13.57 | 2026-10-24 | 多实例挂载:挂载槽数组+switch_mount(超块/GDT/算法上下文重挂)+resolve_path(/cfg→槽1,/data→槽2);13入口路径包装;根分区槽0登记修复 |
| 0.13.56 | 2026-10-24 | 内核版ext2 mkfs:未格式化分区开机自动格式化(P2=HEPU_CFG→/cfg,P3=HEPU_DATA→/data),写后读回自检,已格式化跳过;三区制闭环 |
| 0.13.55 | 2026-10-24 | 稀疏镜像:truncate+conv=sparse,物理占用256MB→1.5MB/320MB→1.4MB;压缩交付.gz(878KB/805KB);修复--sparse不兼容致ext2未写入 |
| 0.13.54 | 2026-10-24 | 根分区ext2自动扩展:分区表>文件系统时内核启动扩展(新块组+位图+GDT+超级块),实测224MB→1GB新增6组;分大区不再占镜像,出厂256MB不变 |
| 0.13.53 | 2026-10-24 | 出厂镜像256MB(体积减半,224MB系统分区);install64三区全可配:自定义增加系统分区大小(只增不减),P2默认512/P3剩余,安装后分区与介质大小解耦 |
| 0.13.52 | 2026-10-24 | 分区规格:配置分区默认64→512MB(install64目标盘分配,只受硬盘限制);出厂镜像回退小规格512MB单分区(raw镜像=整盘拷贝,预分配三区致1GB膨胀),install64重编译入镜像 |
| 0.13.51 | 2026-10-23 | P1内存压力管理:统一视图+低内存回收块缓存+shell meminfo扩展;分区表扫描:MBR解析+ext2魔数检测+启动PARTS日志;关键修复:三区制根分区选择除零崩溃(魔数校验选有效分区) |
| 0.13.50 | 2026-10-23 | 安装向导交互版:菜单(试用/一键/自定义)+键盘输入(syscall READ fd0)+自定义分区大小+shell exec入口,交互全流程实测写盘重启 |
| 0.13.49 | 2026-10-23 | 安装引导流:REBOOT syscall+三区化安装向导install64+mbrtool双模式+镜像系统分区参数化+关键修复:缓存初始化缺失致共享算法全0(公共ext2_cache_init+NULL保护) |
| 0.13.48 | 2026-10-23 | ext2块缓存(LRU64槽,写失效,统计接口)+缓存数据动态分配修复BSS覆盖页表 |
| 0.13.44 | 2026-10-23 | OBS真实安装测试(1904文件落盘)+KHEAP扩容方向定位+镜像容量可配+兼容层规划OBS验收项 |
| 0.13.43 | 2026-10-23 | 64位用户态syscall链路根因修复(entry栈offset全反)+槽直接值语义+真号直push+ELF64 hello全链路打通 |
| 0.13.42 | 2026-10-23 | USB写路径诊断闭环(根因=QEMU UHCI时序竞态)+数据缓冲轮转+命令帧间隔+防御加固 |
| 0.13.41 | 2026-10-23 | 内核稳定: 网络日志风暴清理+ARP/DNS降载+多源输入合流+F12终端+USB写加固+mkdir自检 |
| 0.13.40 | 2026-10-23 | P0-3: USB写路径修复(CSW重试+帧同步)+mkdir/rename+USB自动挂载 |
| 0.13.39 | 2026-10-23 | P2完善: CPU/MEM/DSK真实化+锁屏虚拟键盘+通知RTC时间戳 |
| 0.13.38 | 2026-10-23 | P2完善: 网络状态真实化+设置页注册表/文件关联管理 |
| 0.13.37 | 2026-10-22 | P11修复: superblock拷贝崩溃+挂载重入+设置存储区/文件管理器U盘按钮 |
| 0.13.36 | 2026-10-22 | P11底层: USB存储READ10/WRITE10+ext2挂载(实机读文件验证) |
| 0.13.35 | 2026-10-22 | P10底层: USB海量存储基础(BOT+SCSI探测) |
| 0.13.34 | 2026-10-22 | P9功能: 通知位置四角可配置(注册表持久化) |
| 0.13.33 | 2026-10-22 | P8功能: 通知三源(网络连接/系统更新/驱动刷新) |
| 0.13.32 | 2026-10-22 | P7修复: 终端经典黑底绿字配色(CRT风格) |
| 0.13.31 | 2026-10-22 | P6修复: 终端背景跟随主题(去除硬编码米黄) |
| 0.13.30 | 2026-10-22 | P5应用: 终端完善(命令补全/历史/内核命令集/帮助) |
| 0.13.29 | 2026-10-22 | P4应用: 文件管理器完善(鼠标/过滤/网格/右键菜单/查看器) |
| 0.13.28 | 2026-10-22 | P3应用: 设置页接线(语言切换/驱动刷新入口) |
| 0.13.27 | 2026-10-22 | P2应用: 任务管理器细化(真实数据/实时曲线/CPU排序) |
| 0.13.26 | 2026-10-22 | P1底层: USB驱动增强(HID功能键/设备表/热插拔/usb命令) |
| 0.13.25 | 2026-10-22 | P0底层: 内核驱动模块加载器(ELF重定位 + GOT/PLT + kmod命令) |
| 0.33.1 | 2026-10-30 | P0 交付准则收官：镜像**完全**可从源码重建 **① SEC-12 关闭 —— 12 个无源码的历史测试二进制（hello_static / hello_linux / hello2 / hello3 / hello4 / hello_c2 / glibc_test / hello_libc / dynhello / dynhello2 / libtest.so / libtest2.so）经反汇编 + 符号表 + 寄存器转储逐条还原语义，源码补到 `user/apps/legacy/`（共享头 `common_linux_abi.h`），纳入 `user/Makefile` 的 `legacy` 目标与 `all`，`tools/make_single_img64.sh` 统一写进镜像；旧的无源码产物已删除** **② SEC-15 —— 内核对「是否动态链接」原本没有判据，靠「路径里含 `dyn` 三个字符」猜 ⇒ `/bin/dynhello.elf` 撞上 V0.20.80 遗留路径，算出 `load bias=0x1000`、DT_NEEDED 读出乱码、程序在未初始化的 GOT/PLT 上跑飞（`#GP`）并打断整个自检电池；现新增 `elf64_has_interp()`，带 `PT_INTERP` 的程序在 exec 阶段**明确拒绝**并给出可执行建议，删除文件名启发式** **③ SEC-16 —— `sys_open()` 从 fd 0 开始分配、未保留 Linux 的 stdio 槽位 0/1/2，导致进程第一次 `open()` 拿到 fd 0，随后的 `read(0,…)` 落进 stdin 分支永久 `hlt`（现场：hello4.elf 打开成功却把整机卡死，日志停在 `[LINUX4] Running`）；现改为从 fd 3 开始分配** **④ SEC-17 —— 自检电池把 `hello_static` 注释为「故意跳坏地址的异常测试」是**误判**（反汇编证明它只有 `write`+`exit(42)`，是缺 `.note.ABI-tag` ⇒ `exit(60)` 在原生号表查不到 ⇒ 进程不退出 ⇒ 执行流落到字符串 ⇒ `"He"` 被解成 `rex.W gs insb` ⇒ CPL=3 `#GP(0)`）；SEC-07 的覆盖建立在一个假故障上；现补 ABI note 让它干净退出，另新增 `crash_jump.elf`（显式写未映射地址 ⇒ 真正的 `#PF`）承担该角色，电池判据改为 `expected(crash_jump)`** **⑤ 修 `ld` 把 `.note.ABI-tag` 放到 text 之前低地址导致 ET_EXEC 被判「链接在窗口外」而拒绝（`--section-start=.note.ABI-tag` + `-Ttext` 双地址均在窗口内）** **⑥ 修 `vga_put_hex()` 自带 `0x` 前缀而调用点又手写 `"0x"` 造成的 54 处 `0x0x…` 双前缀日志** **⑦ 新增 `tools/env_restore.sh` 一键恢复沙箱被重置后丢失的 qemu/nasm/apt 索引；`tools/hepu_qemu_boot.sh` 改为逐个探测 qemu 路径并允许 QEMU 自带 seabios** **⑧ 历史程序链接地址改到用户窗口内（SEC-13 消解），glibc 静态程序改用 `-static-pie` 走兼容区** **⑨ 双 CPU 回归：默认 CPU 与 `--cpu max` 各连续 3 次自检电池 `exceptions total=0x1 expected(crash_jump)=0x1 unexpected=0x0 [PASS]`，legacy 全套（8 个程序）输出全部正确** **⑩ 登记 SEC-18 —— ext2 文件读取路径**偶发挂死**：补齐 SEC-12 源码后的首次回归中 `hello4.elf`（open→read→close）切到用户进程后日志完全停住、整机无输出；已排除文件损坏（`debugfs` dump + `cmp` 逐字节一致）、不涉间接块（`size` 被夹到 4096 ⇒ 只读 1 块）、目录遍历有 `rec_len==0` 保护，且开 trace 重跑一次通过、其后 12 次启动全部通过 ⇒ 判为低频偶发，与 SEC-11 同源但形态不同（挂死而非报错），**失败点未定位**，登记备查** |
| 0.33.0 | 2026-11-05 | P0 地基修复 + Linux ABI 兼容层落地：① STAC/CLAC 按 CR4 真实硬件状态门控（修 SMAP 可选特性缺失导致的启动 #UD 死机）② 用户态异常只终止该进程并回内核（SEC-07 落地）③ 启动自检电池收进 HEPU_BOOT_SELFTEST 开关、默认关（SEC-08 落地）④ exec 按 ext2_64_stat 真实大小分配堆，修内核堆耗尽与两处 elf_buffer 泄漏 ⑤ Linux ABI 按进程判定（env_type 取代全局 g_running_linux_elf），原生 ABI 不再被兼容层绑架 ⑥ PIE/ET_DYN 装载：装载偏置 + RELATIVE 重定位 + SysV 初始栈（argc/argv/envp/auxv） ⑦ R_X86_64_IRELATIVE 由内核在装载时调用 resolver 并写回 GOT ⑧ 初始栈/堆/TLS 预留/栈页均按用户窗口私有分区规划（PDPT[1]），大页先拆再写 ⑨ PMM 显式保留内核栈页（此前用户栈与内核栈物理重叠）⑩ 主机端回归 5/5 通过、QEMU 双 CPU 启动回归通过 **⑪ 修 glibc static-PIE 退出崩溃（SEC-10）：入口 GPR 按 Linux ELF_PLAT_INIT 清零（rdx 残留被 glibc 当 rtld_fini 注册成退出析构函数 ⇒ rip=0x202）** **⑫ 修用户栈对齐与用户态 SSE 使能（SEC-13：RSP ≡ 8 mod16、CR4.OSFXSR|OSXMMEXCPT）** **⑬ 修链接脚本丢弃 .note.ABI-tag 导致 ABI 误判 + comptest 补 ABI tag** **⑭ ext2 读块失败不再静默吞掉（SEC-11）** **⑮ 启动自检给出可判定汇总（[SELFTEST] … [PASS]/[FAIL]）** **⑯ 修 `sys_newfstatat` 把用户指针直传 `ext2_64_stat`（SMAP 下内核态 #PF ⇒ 整机挂死）** **⑰ 系统调用分发拆分「是否处理过」与「返回值」两个语义（旧实现用 -1 兼表二者 ⇒ 合法 errno 被误报 Unknown syscall: 9E）** **⑱ `sys_arch_prctl` 错误码对齐 Linux（ENODEV/EINVAL）** **⑲ 修 SMEP 拦截内核态调用用户页 IRELATIVE resolver（error=0x11 bit4=1 取指违规）—— SMAP 管数据、SMEP 管取指，两者不可互替** **⑳ 修内核自检 `syscall64_test()` 的撞号/越界读/初始化顺序错误（日志里的 `PID: FFFFFFF` 与 `0123456789ABCDEF` 均是自检写错造成的假象）** **㉑ 双 CPU 对齐回归：默认 CPU 与 `--cpu max` 各连续 3 次自检电池 `unexpected=0x0` [PASS]，默认构建双 CPU 均 0 异常进 shell** |
| 0.32.0 | 2026-10-28 | 品牌规范 V1.0 落地（HEPU 品牌色/标识/开机画面），hepu_brand.h 版本号统一 |
| 0.13.24 | 2026-10-22 | P1底层: 系统多语言框架(zh/en字符串表 + lang命令 + 注册表持久化) |
| 0.13.23 | 2026-10-22 | P1底层: 音频AC97完善(音调真实播放DMA + 静音写寄存器) |
| 0.13.22 | 2026-10-22 | P1底层: 磁盘管理工具(容量/fsck) + 文件关联(注册表持久化) |
| 0.13.21 | 2026-10-22 | P0底层: GPS定位服务(NMEA解析 + gps命令) |
| 0.13.20 | 2026-10-22 | P0底层: 系统注册表(持久化KV + reg命令) |
| 0.13.19 | 2026-10-22 | P0底层: 驱动管理器(联网仓库/下载/安装/卸载 + drv命令) |
| 0.13.18 | 2026-10-22 | 系统底层完善: 数学库扩展(PRNG/定点舍入取模/整数运算) |
| 0.13.17 | 2026-10-22 | 图形化修复: 窗口平铺Snap(Win+←/→) + 新窗口自动聚焦修复 |
| 0.13.16 | 2026-10-22 | 图形化修复: 窗口最小化/最大化/双击还原 + Dock/切换器恢复 |
| 0.13.15 | 2026-10-22 | 图形化修复: 应用图标全覆盖(Apps抽屉10应用) + 文件管理器文件类型图标 |
| 0.13.14 | 2026-10-22 | 统一矢量图标引擎+状态栏/标题栏图标重绘, 托盘稳定性验证 |
| 0.13.13 | 2026-10-22 | 修复图形化bug: 窗口级联定位(避让侧边栏)、终端输出区背景层; 前置0.13.12双缓冲修复撕裂 |
| 0.13.12 | 2026-10-21 | 双缓冲原子帧渲染, 修复窗口时有时无/终端不可见(渲染撕裂) |
| 0.13.11 | 2026-10-21 | PMM页对齐崩溃修复, 桌面/联网天气正常 |
| 0.13.0 | 2026-10-21 | 网络全链(自研TCP/IP+e1000+QEMU socket后端+天气)、USB/PS2驱动完善、应用窗口系统 |
| 0.12.x | 2026-10-20 | 图形系统完善: 单色透明图标、64x64、AA圆角、字距修复、锁屏登录、窗口切换器 |
| 0.11.x | 2026-10-19 | 混合双内核(32+64)、HepuShell桌面、Control Center、任务管理器、文件管理器 |
| 0.10.x | 2026-10-18 | 64位系统调用兼容层、网络栈起步、PMM修复(页对齐崩溃) |
| 0.9.0 | 2026-10-06 | 版本号格式统一为三段式，Base64+MD5库 |
| V8.99 | 2026-10-06 | 包签名验证与依赖检查 |
| V8.98 | 2026-10-06 | 共享库管理器 |
| V8.97 | 2026-10-06 | 64位系统调用兼容层 |
| V8.96 | 2026-10-06 | DRM/KMS多显示器支持 |
| V8.95 | 2026-10-06 | DRM/KMS与现有图形后端集成 |
| V8.94 | 2026-10-06 | DRM/KMS接口框架 |
| V8.93 | 2026-10-06 | virtio-gpu增强 |
| ... | ... | ... |

## 注意事项

1. **重要版本由用户决定**：只有用户确认"这个版本很重要/可以"才+1，开发人员不自行增加
2. **bug版本不可耻**：每个系统都有bug，修复bug是正常工作
3. **版本号连续**：不要跳版本，保持连续性
4. **同步更新**：所有版本号引用必须同步更新
5. **文档先行**：版本变更前先更新本文档
6. **重要版本=0是正常的**：不代表系统不好，只是用户尚未确认重要里程碑
7. **默认版本自动+1**：每次普通功能发布，默认版本自动+1
8. **bug版本自动+1**：每次bug修复，bug版本自动+1



## 发布撤回记录（2026-11-11）

按维护者要求清空版本发布，仓库只保留源码归档：

| 已删除 Release | 已删除 tag | 原提交 |
|---|---|---|
| v0.54.8 | v0.54.8 | 670a91a |
| v0.54.9 | v0.54.9 | 34cfc1d |
| v0.54.10 | v0.54.10 | 0b1af42 |
| v0.54.12 | v0.54.12 | 238177e |

- 保留：`historical-milestones-v1`（历史里程碑源码包，超 100MB 仓库限制部分）
- 代码历史未改动：上述提交仍在 `main` 分支，仅删除 tag 与 Release 页面
- 版本号口径不变：当时源码版本为 **0.54.12**，镜像由 `tools/make_single_img64.sh` 本地构建

## 版本推进记录

| 版本 | 日期 | 类型 | 内容 |
|---|---|---|---|
| 0.55.0 | 2026-11-11 | minor（普通功能发布） | P0 渲染管线升级：按需合成（脏矩形+离屏缓冲，静止帧跳过 96.7%）、通用缓动动画（ease-out cubic，灵动岛展开/收起）、PNG 解码器（自包含 inflate，含主机回归夹具） |
| 0.56.1 | 2026-11-11 | patch（缺陷修复） | 任务管理器的 CPU/内存是 tick 编造公式、PMM `used` 口径漏掉启动期占用导致 MEM 恒 0%、天气组件失败即永久空白且不显示占位（详见 FIXES_0_56_1.md） |
| 0.56.2 | 2026-11-11 | patch（缺陷修复） | Apps 页应用图标只渲染无命中测试（点了没反应）、任务管理器曲线区硬编码黑底/提示条溢出/曲线不可见、灵动岛 `CPU 35% MEM 60%` 为写死文案（详见 FIXES_0_56_2.md） |
| 0.57.0 | 2026-11-11 | minor（普通功能发布） | 图形层增强：窗口三层阶梯投影、侧栏标签与 Apps 图标悬停反馈、任务管理器 25/50/75% 刻度线；输入链路两处真实缺陷修复——按钮沿报告重复上报位移导致"点击落空"、PS/2 坐标覆盖 USB 累加坐标导致光标被拽回；配套 `tools/dev/` 五个开发脚本（详见 FIXES_0_57_0.md） |
| 0.56.0 | 2026-11-11 | minor（普通功能发布） | ① fsck 接入 inode 表遍历（check_inode 从"实现了但零调用"变为真实生效，并发现两套 ext2 结构定义需用编译期断言钉死布局）；② 欢迎引导可关闭（config.hsqml 新增 UI.skip_oobe）；③ 清理 pit64 遗留调试栈转储、统一 7 处过期的"0.54.12"对外版本文案 |
| 0.55.1 | 2026-11-11 | patch（缺陷修复） | 编译告警 209→89，并修复其暴露的真实缺陷：mkfs/umask 撞号致 mkfs 被覆盖、桌面配置从未被读取、ELF 重定位不校验类型、DT_RELA 表未应用、TTF cmap 未筛 encoding、LAPIC 基址指针转换、恒真假判断等 10 类（详见 FIXES_0_55_1.md） |

说明：按本规范第二位（默认版本）用于"新增常规功能/完善子系统"，P0 属新增能力而非 bug 修复，
故推进 minor 至 0.55.0，bug 位归零。重要版本仍为 0（未由用户确认为里程碑）。

0.55.1 为紧随其后的缺陷修复批次（patch 位 +1）：告警清理过程中逐条追查到 10 类真实逻辑缺陷，
属修 bug 而非新增功能，故不动 minor。
