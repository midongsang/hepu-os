---
title: Android HAL 移植方案（ANDROID_HAL_PORTING）
status: active
sys_version: 0.55.1
updated: 2026-11-02
owner: planning
source_of_truth: null
supersedes: null
---

# Android HAL 移植方案

> 定位：把 [DESIGN](../architecture/DESIGN.md) 0.9.4 的"HAL 缝隙"落成可执行计划。
> 架构约束以 DESIGN 为准，执行顺序、产物与验收以本文为准。

## 一、结论先行

1. **HAL 移植不是第一件事，也不是工作量主体。** 标准 HAL 服务是动态链接的
   Android 进程，它必须先能加载、能注册、能通过 Binder 被调用，之后才谈把设备接进来。
2. 顺序由依赖链锁死：

```
Android 用户态域（bionic + linker + 大 VA + init/servicemanager）
  → Binder 传输（broker + /dev/binder 外壳）
    → 设备能力模型（令牌 + 共享缓冲 + 事件 + fence）
      → HAL 适配器（标准 HAL 接口 → 赫普设备能力）
```

3. 工作量结构大致是：用户态域与传输占大头，设备能力次之，单个 HAL 适配器最小。
   把 HAL 当主线，会低估前置成本。
4. 三个前置各要一个最小闭环。缺任一条，HAL 只能做成同域伪实现：可演示，不可交付。
5. 首个可验收 HAL 选 `light` 或 `vibrator`；真正的门槛是显示三件套与 `audio.core`。

## 二、范围与边界

**目标**

- 不改 AOSP framework、Binder、AIDL，让标准 HAL 服务运行在赫普的独立域；
- HAL 服务对 Android 侧暴露标准接口，对赫普侧只通过设备能力访问硬件；
- 每个 Android 进程（含 HAL 服务）是一个独立隔离域，符合 DESIGN 0.8.5 与公理 4。

**不在范围**

- 内核级 Binder 驱动；
- 改造框架通信层对接赫普原生 IPC（平台分支，见 DESIGN 0.9.5）；
- 闭源 GPU、基带、TEE、Widevine 的驱动级移植；
- GMS、CTS、Play Integrity 认证本身。

**交付物**

- 决策记录（版本、传输、参考设备、域粒度）；
- HAL × 能力矩阵；
- 能力底座 PoC 与传输 PoC；
- 每个 HAL 的适配实现、VINTF 片段与验收记录。

## 三、四个前置决策

| 决策项 | 选项 | 建议 | 理由 | 不选的后果 |
|--------|------|------|------|------------|
| 目标版本与传输 | AIDL 单栈 / HIDL 加 AIDL 双栈 | Android 14/15，新增 HAL 一律 AIDL | AIDL 走 `/dev/binder` 与 `servicemanager`，与 broker 同一传输 | HIDL 另需 `/dev/hwbinder` 与 `hwservicemanager`，多一套命名空间与生命周期 |
| 参考设备 | 手机 / 开发板 / QEMU | QEMU x86_64 → 开发板 → 手机 | 先把 HAL 缝隙与运行时打通，避免同时处理闭源 BSP | 手机 BSP 会把 HAL 工作与驱动逆向混在一起，无法判断失败原因 |
| 验收口径 | 接口可见 / 可调用 / framework 真实使用 | 三层全过 | 只有 `lshal` 可见只是"接口通了" | 容易把桩实现当成移植完成 |
| 域粒度 | 每 HAL 一域 / 按类合并 | 一个 HAL 服务进程一个域，`sensors` multihal 等原生合并的除外 | 与 Android 进程边界一致，故障与权限可单独收敛 | 合并过多会放大权限面；合并过少会增加内存与启动开销 |

补充：`light` 在 QEMU 上没有真实 LED。可用键盘 LED 或测试设备作为可观测后端，
但不能把"调用了接口"当作验收，必须看到硬件状态变化或等效的可观测结果。

## 四、依赖链与现状核对

| 环节 | 现状 | 缺口 |
|------|------|------|
| Android 运行时 | ELF 加载器只支持 ET_EXEC 与 static-PIE，拒绝 `PT_INTERP`；用户窗口固定 1 GB | 动态链接、bionic、linker、大 VA 与 ASLR 预期 |
| Binder 传输 | `hsipc` 是核内命名通道；`pipe`/`futex` 是进程内设施 | 能力令牌与句柄表、同步会合、跨域共享页、死亡事件 |
| 设备能力 | H-HAL 是核内驱动注册表（函数指针加设备树）；devfs 仅 `null`/`zero`/`fb0`/`console`/`mem` | 面向域的设备授权、共享缓冲、fence、IRQ 事件化、DMA 与 IOMMU |
| HAL 宿主 | 无 AIDL/HIDL 运行时，无 VINTF 清单，无 HAL 服务宿主 | HAL 服务进程、注册发现、生命周期、VINTF 生成 |

关键源码位置：

- ELF 与地址空间约束：[DESIGN 22](../architecture/DESIGN.md) 第 22 章；
- 核内 IPC：[hsipc.c](../../kernel/arch/x86_64/hsipc.c)；
- 驱动注册表：[hal.c](../../kernel/hal/hal.c)、[hhal.h](../../kernel/include/hhal.h)；
- 设备树：[hal_device.c](../../kernel/hal/hal_device.c)；
- 设备文件：[devfs.c](../../kernel/fs/devfs.c)；
- 驱动路线与 IOMMU 现状：[DESIGN 15](../architecture/DESIGN.md) 第 15 章。

## 五、设备能力模型（M1 要实现的原语）

### 5.1 对象模型

| 对象 | 含义 | 关键字段 |
|------|------|----------|
| Device | 一个可被枚举的硬件实例 | 稳定 ID、总线、MMIO/IRQ/DMA 资源、状态 |
| Capability | 对设备的一组受限权利 | 设备 ID、权利集合（MMIO/IRQ/DMA/复位/电源）、约束 |
| Handle | 域内对能力的引用 | 每域句柄表、引用计数、撤销状态 |
| Buffer | 可跨域共享的缓冲 | 大小、格式、修饰符、stride、usage、物理页 |
| Event | 可等待的设备事件 | 来源、时间戳、超时、取消 |
| Fence | 跨域顺序保证 | timeline、信号值、等待与超时 |

原则：**跨域只传句柄、ID、能力令牌，禁止裸指针**（DESIGN 23.2）。

### 5.2 原语清单与验收

| 原语 | 服务的 HAL | 最小验收 | 现状 |
|------|------------|----------|------|
| 设备对象与稳定 ID | 全部 | PCI/ACPI/DT 枚举出资源集 | 设备树已有，资源字段有限 |
| 能力令牌与每域句柄表 | 全部 | 只授予目标设备；域退出即撤销 | 未提供 |
| 域内 MMIO 映射 | `light`、`vibrator`、`power`、`sensors` | 寄存器读写生效，越界拒绝 | 未提供 |
| IRQ 事件化 | `composer`、`audio`、`sensors` | 中断变事件，可阻塞等待与超时 | 仅核内回调，未到域 |
| 共享缓冲与格式/修饰符 | gralloc、`camera`、`audio` | 跨域分配、映射、释放，元数据随缓冲走 | 未提供 |
| fence 与 timeline | `composer`、`camera`、`audio` | 生产者信号、消费者等待，顺序可验证 | 未提供 |
| 快速消息队列（FMQ） | `sensors`、`audio`、`camera` 快路径 | 共享内存加事件通知的无锁环形队列 | 未提供 |
| DMA 与 IOMMU | `composer`、`camera`、`wifi` | 设备只能访问授权窗口 | 无 IOMMU；仅核内 DMA 缓冲原型 |
| evdev 语义 | 输入（非 HAL） | `EVIOC*` ioctl、`input_event`、epoll | 未提供 |
| 热插拔与设备事件 | 全部 | 到达/移除可被域订阅 | 有 hotplug 原型，未到域 |
| 复位与恢复 | 全部 | HAL 崩溃可重启，内核不受影响 | 未提供 |
| 电源、时钟、稳压 | `power`、`audio`、`camera` | 按设备上下电、门控时钟 | 未提供 |

### 5.3 实现顺序

```
设备对象与稳定 ID
  → 能力令牌 + 每域句柄表
    → MMIO 映射 + IRQ 事件化
      → 共享缓冲 + fence
        → FMQ
          → DMA + IOMMU
            → 热插拔 + 复位恢复
              → 电源与时钟
```

硬约束：无 IOMMU 时不得把可 DMA 设备直接交给用户态域。显示与相机可先用
"内核中介缓冲"跑通，但这不是最终形态，也不能宣传为"用户态驱动已隔离"。

## 六、HAL 清单与优先级

| 级别 | HAL | 为什么在这个级别 | 依赖 |
|------|-----|------------------|------|
| T0 骨架 | `servicemanager`/`hwservicemanager`、属性服务、VINTF | 不是 HAL，但注册与发现全靠它们 | Binder 传输 |
| T1 最小闭环 | `light`、`vibrator` | 无缓冲、无 DMA、接口简单，用来验证缝隙 | MMIO 与令牌 |
| T2 显示与音频 | `graphics.allocator`、`graphics.mapper`、`graphics.composer`、`audio.core` | 决定能否有可用桌面与声音 | 共享缓冲、fence、IRQ 事件、DMA |
| T3 输入与电源 | evdev（非 HAL）、`power`、`health`、`thermal` | 输入不是 HAL；其余为可用性必需 | 事件与设备节点 |
| T4 感知与连接 | `sensors`、`camera`、`wifi`、`bluetooth`、`gnss` | 体验核心，但依赖更重 | 缓冲、DMA、固件 |
| T5 通信与安全 | `radio`、`nfc`、`keymaster`、`gatekeeper`、`drm`、`biometrics` | 依赖基带、TEE、许可证，属硬边界 | 厂商与授权 |

需要纠正的认知：**触摸与按键不是 HAL。** Android `InputReader` 直接读
`/dev/input/event*`，需要的是 evdev 字符设备语义，不是实现一个 HAL。

## 七、关键 HAL 的映射

| HAL | 标准接口要点 | 赫普后端 | 关键原语 | 第一个可测形态 | 验收 |
|-----|--------------|----------|----------|----------------|------|
| `light` | AIDL，多个逻辑灯，无缓冲 | 寄存器或 PWM | MMIO 与令牌 | QEMU 键盘 LED 或测试设备 | `lshal`、VTS、灯状态变化 |
| `vibrator` | AIDL，时长与振幅，无缓冲 | 定时器与 PWM | MMIO 与令牌 | 测试设备 | `lshal`、VTS、可观测输出 |
| `power`/`health` | AIDL，枚举与回调 | 调度、睡眠、电池与温度 | 设备事件、suspend | QEMU ACPI 电源与虚拟电池 | VTS、休眠唤醒、读数 |
| gralloc 与 mapper | 分配、导入、lock/unlock、格式与修饰符 | 缓冲对象与域间映射 | 共享缓冲、格式元数据、令牌 | 帧缓冲或测试缓冲 | gralloc VTS、跨域往返 |
| `composer` | display 配置、layer、validate/present、vsync | 显示控制器：plane、mode、vsync | MMIO、IRQ 事件、fence、DMA | QEMU 帧缓冲或 virtio-gpu | SurfaceFlinger 起、合成正确、无撕裂 |
| `audio.core` | stream、port、patch、周期事件 | PCM 环形缓冲、路由、音量 | 共享缓冲、IRQ 事件、时钟 | QEMU AC97 或 virtio-snd | AudioFlinger 播放与录音回环 |
| `sensors` | multihal、poll 事件队列 | 传感器事件流与时间戳 | FMQ、事件队列 | 虚拟传感器 | VTS、framework 读数 |
| `camera` | provider 与 device、每帧请求与 fence | 采集、ISP、3A | 共享缓冲、fence、DMA 与 IOMMU | 虚拟相机（测试图源） | 预览通路，再接真传感器 |
| `wifi`/`bluetooth` | AIDL，芯片或 HCI 访问 | 802.11 设备或 HCI 传输 | DMA、固件加载、事件 | USB 蓝牙或虚拟网卡 | 扫描连接、扫描配对 |
| `gnss` | AIDL，位置与状态回调 | 定位设备或外部源 | 设备事件 | 虚拟定位源 | VTS、位置更新 |
| `radio`/`keymaster`/`drm` | AIDL 或 HIDL，厂商实现 | 基带、TEE、Widevine | 厂商专有 | 按授权推进 | 不设源码级承诺 |

## 八、里程碑与验收

### 8.1 前置阶段

| 阶段 | 内容 | 验收 |
|------|------|------|
| P0 冻结 | 四个决策、HAL 清单、能力矩阵 | 每行有接口版本、后端原语、验收命令、缺口 |
| P1 能力底座 | 令牌与句柄表、MMIO、IRQ 事件、共享缓冲、fence | 两个测试域完成授权、映射、事件、缓冲闭环，越权被拒 |
| P2 传输闭环 | `/dev/binder` 外壳、broker、注册发现、一次 AIDL 往返 | 自编译 HAL 服务可注册，客户端按接口名找到并同步调用 |
| P3a Android native 用户态 | 动态 ELF（`PT_INTERP`/bionic）、init、属性服务、标准 `servicemanager` | 一个现成 HAL 二进制可加载并出现在 `lshal` |
| P3b Framework 运行时 | zygote、ART、system_server、核心系统服务 | `service list` 与 `dumpsys activity` 正常 |

说明：P2 允许先用赫普侧服务注册表承担发现语义，P3a 再替换为标准
`servicemanager`。这只是缩短反馈环，不改变最终形态。

### 8.2 HAL 阶段

| 阶段 | 内容 | 验收 |
|------|------|------|
| H1 最小 HAL | `light` 或 `vibrator`，接真实设备能力 | `lshal`、VTS、framework 调用三层全过 |
| H2 显示 | gralloc、mapper、composer | SurfaceFlinger 起、图层合成、vsync、无撕裂 |
| H3 音频 | `audio.core` | AudioFlinger 播放与录音回环 |
| H4 输入与基础 | evdev、`power`、`health`、`thermal` | 触摸键鼠、休眠唤醒、电量温度 |
| H5 感知与连接 | `sensors`、`camera`、`wifi`、`bluetooth`、`gnss` | 各模块 VTS 与 framework 路径 |
| H6 通信与安全 | `radio`、`nfc`、`keymaster`、`gatekeeper`、`drm`、`biometrics` | 按可获得授权推进，明确未获授权项 |

依赖关系：H1 依赖 P1+P2+P3a+P3b；H2 与 H3 依赖全部前置；H4 起可并行，
但共享同一套能力底座，不允许各 HAL 私接设备。

## 九、并行工作流

三条工作流可以并行，汇合点固定在 H1：

- A 运行时与传输：P2、P3a、P3b；
- B 设备能力：P1；
- C HAL 契约：按 AOSP 的 AIDL 定义生成桩，写后端适配层，用 B 的测试域验证后端。

契约先行只用于验证注册与调用路径。接口桩必须换成真实设备能力与真实 framework
客户端，才能计入验收；不得把桩实现写成"已完成 HAL"。

## 十、风险与决策点

| 风险 | 触发条件 | 应对 | 需要谁定 |
|------|----------|------|----------|
| 跑现成镜像还是自建镜像 | 自建镜像无法复用 vendor HAL；现成镜像要求内核侧 `/dev/binder` 语义 | 先自建最小 AOSP 镜像（可裁剪、可插桩），HAL 接口保持标准；再评估同步上游 | 架构 |
| HIDL 遗产 | 目标设备只有 HIDL vendor 包 | 仅在复用旧包时保留 `/dev/hwbinder` 与 `hwservicemanager` | 架构 |
| SELinux 策略 | HAL 进程启动被拒或 VINTF 检查失败 | 开发期用 permissive 定位；认证期必须回到 enforcing，不能把 permissive 当交付 | 安全 |
| 显示后端 | 无 GPU 驱动或只有闭源 blob | QEMU 用帧缓冲或 virtio-gpu；软件合成先跑通；GPU 驱动另立项 | 图形 |
| 权限面放大 | 多个 HAL 合进一个域 | 回到"每 HAL 一域"，按需再合并 | 架构 |
| 许可与分发 | 搬用 GPLv2 内核代码或分发 vendor blob | 内核只自行实现通用原语；AOSP 保留声明；vendor blob 单独合规评估 | 法务 |

## 十一、明确不做

- 不改 AOSP framework、Binder、AIDL 去接赫普原生 IPC：那是平台分支；
- 不把 HAL 业务写进内核：内核只加通用原语；
- 不把同域伪 HAL、`lshal` 可见当作移植完成；
- 不在无 IOMMU 时给用户态域裸 DMA 权限；
- 不承诺闭源 GPU、基带、TEE、Widevine；
- 不先啃手机 BSP：参考设备从 QEMU 与开发板开始。

## 十二、第一批具体动作

| # | 动作 | 产物 | 完成判据 |
|---|------|------|----------|
| 1 | 冻结第三节四个决策 | 一页决策记录 | 四项都有明确选择与否决理由 |
| 2 | 建 HAL × 能力矩阵 | 附录 A 填满 | 每个目标 HAL 有接口版本、后端原语、验收命令、缺口 |
| 3 | P1 能力底座 PoC | 测试域与测试设备 | 授权、映射、事件、缓冲、fence 闭环，越权被拒 |
| 4 | P2 传输闭环 PoC | `/dev/binder` 外壳与 broker 原型 | 一次 AIDL 同步往返，服务可注册被发现 |
| 5 | 选 H1 的 HAL | `light` 或 `vibrator` 适配层 | 能在 QEMU 或测试设备上产生可观测状态变化 |

## 附录 A：HAL × 能力矩阵模板

| HAL | 接口版本 | 进程与域 | 后端原语 | 缓冲与 fence | 验收命令 | 缺口 | 负责 |
|-----|----------|----------|----------|--------------|----------|------|------|
| `light` |  |  | MMIO 与令牌 | 无 | `lshal`、VTS |  |  |
| `vibrator` |  |  | MMIO 与令牌 | 无 | `lshal`、VTS |  |  |
| `power` |  |  | 设备事件、suspend | 无 | VTS |  |  |
| `health` |  |  | 设备事件 | 无 | VTS |  |  |
| gralloc |  |  | 共享缓冲、格式元数据 | 有 | gralloc VTS |  |  |
| `composer` |  |  | MMIO、IRQ 事件、DMA | 有 | SurfaceFlinger 合成 |  |  |
| `audio.core` |  |  | 共享缓冲、IRQ 事件 | 有 | AudioFlinger 回环 |  |  |
| `sensors` |  |  | FMQ、事件队列 | 有 | VTS |  |  |
| `camera` |  |  | 共享缓冲、fence、DMA | 有 | 预览通路 |  |  |

## 附录 B：术语

- **HAL**：Android 硬件抽象接口。Treble 之后以 HIDL 或 AIDL 定义，vendor 实现，
  framework 调用。
- **VINTF**：Vendor Interface 清单，声明设备提供了哪些 HAL 实例与版本；
  与 framework 的 compatibility matrix 不匹配时，服务不会被认可。
- **FMQ**：Fast Message Queue，基于共享内存与事件通知的环形队列，用于 HAL 快路径。
- **fence**：跨域顺序原语，生产者信号、消费者等待，显示与相机靠它保证帧序。
- **gralloc / mapper**：图形缓冲的分配与映射接口。
- **composer / HWC**：显示合成接口，管理 display、layer、vsync 与 present。
- **evdev**：Linux 输入事件设备接口，Android `InputReader` 直接使用，不属于 HAL。
