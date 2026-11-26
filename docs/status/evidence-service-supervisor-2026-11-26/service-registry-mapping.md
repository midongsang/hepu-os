# 平行服务集合盘点与收敛映射表（2026-11-26）

范围：第二阶段服务监督轮次。统一服务表 `kernel/sched/services.c`（头文件
`kernel/include/services.h`）是 32/64 唯一的**系统服务生命周期事实源**
（注册 / 启动 / 停止 / 健康 / 退避）。下表逐个盘点其余带 "service" 字样的
集合，注明迁入、保留归属与阻塞原因。

## 1. 映射总表

| # | 文件 | 集合内容 | 32/64 差异 | 处置 | 迁移阻塞 / 边界说明 |
|---|---|---|---|---|---|
| 1 | `kernel/sched/services.c` | network / audio / wifi / bluetooth / gui 五项内置服务的注册、依赖、启动、停止、健康、退避 | 32/64 共用同一实现 | **唯一事实源**（本轮增强） | 五项 start/stop 仍为空壳，健康标 STUB，不代表真实 daemon 已拉起 |
| 2 | `kernel/arch/x86/microg_service.c`（头 `kernel/include/microg_service.h`） | 12 个 microG 能力槽位：location / push / maps / auth / cloud msg / device auth / ad id / health data / payment / nearby / cast / cloud storage；挂账号、推送令牌、位置状态 | 仅 32 位编译；`init_sequence.c` 阶段调用 `microg_service_init()` | **保留**，归属 microG 兼容子系统；头/源文件已加归属注释，不作为第二套服务管理入口 | 模型不同：槽位是 provider 描述 + 子系统数据（账号表、令牌、位置缓存），没有本地 daemon 的 start/stop/依赖/健康语义；统一表无这些字段。槽位 `MG_STATE_RUNNING` 同为原型空壳，注释已标明不代表真实后台进程 |
| 3 | `kernel/arch/x86/distributed_os.c`（头 `kernel/include/distributed_os.h`） | `dos.services[]`：分布式 mesh 远端设备能力注册表（device_id / type / capacity / available / 容量分配） | 仅 32 位编译；`init_sequence.c` 阶段调用 `distributed_os_init()` | **保留**，归属分布式互联子系统；头/源文件已加归属注释 | 登记对象是远程设备提供的能力与容量，不是本地常驻进程；容量申请/释放模型与统一表的生命周期模型不匹配 |
| 4 | `kernel/arch/x86_64/hsservices.c`（头 `kernel/arch/x86_64/hsservices.h`） | 三个桌面后端组件：`hs_audio_*` 音频后端桥接、`hs_process_monitor_*` 进程快照、`hs_file_monitor_*` 文件监听框架 | 仅 64 位编译；`kernel_main_64.c` 调用 `hs_services_init()` | **部分迁移**：音频后端 `hs_audio_init()` 绑定到统一表 `audio` 服务（64 位 `svc_audio_start` 调用）；`hs_services_init()` 不再重复初始化音频。进程快照、文件监听**保留**为桌面后端组件 | 进程快照由桌面事件循环周期 `update()` 刷新，文件监听目前只是 watch 表框架（`hs_file_monitor_poll` 为 TODO），均非常驻 daemon，没有独立生命周期，不迁入统一表 |
| 5 | `kernel/ipc/service.c` | `sys_service_register()`：用户态 IPC 的服务名 → pid 注册表（系统调用 ABI，按 pid 绑定名字） | 32 位 syscall 路径 | **保留**，归属 IPC/系统调用层，本轮不改 | 这是 IPC 寻址表（名字查 pid），不是 daemon 生命周期表；与统一表解决的问题不同。后续若要打通，应由 IPC 层查询统一表，而不是再建一套生命周期注册 |
| 6 | `kernel/sched/appframework.c` | `af_service_register()`：应用框架（Intent/Activity 模型）的应用内组件注册 | 32 位 | **保留**，归属应用框架层，本轮不改 | 登记对象是应用组件（Activity/Service 意图），受应用框架生命周期管理，不是系统守护进程 |

## 2. 32/64 差异结论

- 32 位平行集合：#2 microG 槽位、#3 分布式远端能力、#5 IPC 名字表、#6 应用框架组件。
- 64 位平行集合：#4 桌面后端组件（音频桥接 / 进程快照 / 文件监听）。
- 两端共用：#1 统一服务表。
- 本轮没有留下"两套都被当成真源"的系统服务注册点：64 位音频后端原先由
  `hs_services_init()` 独立初始化，现收敛为统一表 `audio` 服务一条初始化路径；
  其余保留项均为不同抽象层（兼容层 / 分布式 mesh / IPC 寻址 / 应用框架 / 桌面组件），
  并在头文件与源文件注释中标明归属。

## 3. 仍未迁移项与阻塞原因（交接）

- #2 microG 槽位、#3 分布式远端能力：需要先在统一表扩展"能力/资源提供者"
  模型（或明确两层映射关系），当前生命周期字段不足以承载，阻塞于数据模型
  扩展，不是简单挪表。
- #5 IPC 名字表、#6 应用框架组件：属于用户态/框架层抽象，迁移应随用户态
  服务化（IPC 化）一并设计，本轮不动。
- 64 位 `shell64.c` 内 `shell_cmd_service()` 的展示列仍为第一阶段格式（未显示
  health 列与 STUB 备注）；该文件命令表/展示归第三路，本轮未改，见 OPEN 登记。
