---
title: 内核优化与兼容预埋路线（KERNEL_OPT_PLAN）
status: active
sys_version: 0.54.6
updated: 2026-11-01
owner: planning
source_of_truth: null
supersedes: null
---

# Hepu OS 内核优化与兼容预埋路线

> 来源：用户指导 + 自研 OS 经验
> 更新：2026-10-29, v0.20.44
> 核心原则：内核保持 Hepu 干净模型，ABI 边界做翻译；不为兼容改内核设计。

## 优先级排序

### 第1档（立刻做，为兼容层铺路）
- [ ] syscall 入口统一兼容分支（已部分做：linux_map dispatch）
- [ ] errno 统一映射表（hepu_errno <-> linux_errno，集中转换，不散落）
- [ ] vDSO：clock_gettime/gettimeofday 映射到用户页，免陷内核
- [ ] VFS 操作加 compat_ctx 参数（原生传空，兼容层传上下文）
- [ ] 进程元数据加 env_type 标记（native / linux_compat / win_compat）

### 第2档（紧接，内存与调度骨架）
- [ ] VMA 抽象：分层标记（原生 flag + 兼容扩展 flag），红黑树查找
- [ ] task 结构预留兼容扩展字段（linux tid/tgid/siggroup/ns 指针）
- [ ] ASID（地址空间 ID）减少 TLB 全刷
- [ ] tickless 骨架
- [ ] 内部结构与对外 ABI 结构严格分离（不直接用 Linux struct stat）

### 第3档（然后，性能与稳定性）
- [ ] 调度器唤醒路径细粒度锁（不用全局大锁）
- [ ] 中断线程化 + 亲和性绑定
- [ ] page cache 预读/writeback 调优
- [ ] 缺页主路径无堆分配

### 第4档（同步，用户态底座）
- [ ] libc POSIX 语义收敛
- [ ] ld.so 延迟绑定 + gnu hash
- [ ] Vulkan 后端底座
- [ ] coreutils / shell 稳定性

### 第5档（延后）
- [ ] io_uring 骨架
- [ ] evdev 输入事件对齐
- [ ] tracepoint / 内核探针埋点
- [ ] ioctl 分派表

### 第6档（最后）
- [ ] Linux ELF 加载器原型（我们已开始）

## 避坑
1. 不要为兼容把内核改成 Linux 结构
2. errno 转换集中在一处，不散落
3. 内部结构和 ABI 输出分开
4. 高频 syscall 走快路径/vDSO
5. 接受 5~30% 兼容损耗，不追求零损耗
