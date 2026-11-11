---
title: Hepu 完整开发路线图
status: active
sys_version: 0.55.0
updated: 2026-11-03
owner: planning
source_of_truth: null
supersedes: null
---

# Hepu 完整开发路线图 (xiulihe 原话记录)

> 本文档是 xiulihe 的设计哲学原话记录，是架构最高原则。
> 「不往内核主干塞特例补丁，所有差异化行为走域标签、能力、契约配置。」
> 状态项按源码核实，原文未标状态的按实际情况补注。

## 铁律
不往内核主干塞特例补丁，所有差异化行为走域标签、能力、契约配置。

## 优先级排序
1. VA地址空间管理器 + 域粒度内存水位/OOM
2. 域QoS调度、中断隔离
3. 设备能力抽象、固件中心化
4. 带能力校验的IPC总线
5. VFS快照原语、域ACL
6. 兼容沙箱实例池、原子切换
7. Shell结构化输出、审计、trace调试框架

## 不做
- 不在内核主干增加Linux syscall/POSIX特例
- 不做内核核心代码原地热补丁
- 不把tty/信号/VFS为兼容层打特殊补丁
- 不维护海量原生硬件驱动

## 当前进度（按源码核实）
- [x] VA区间管理器 (v0.20.83)
- [x] 动态链接器: PT_LOAD/PT_DYNAMIC/DT_NEEDED/JUMP_SLOT/RELA (v0.20.78-82)
- [ ] 域粒度内存OOM
- [ ] 域QoS调度
- [ ] 设备能力抽象
- [ ] 能力IPC
