---
title: Hepu Shell 设计原则
status: active
sys_version: 0.56.2
updated: 2026-10-29
owner: architecture
source_of_truth: null
supersedes: null
---

# Hepu Shell 设计原则

> 作者：xiulihe
> 日期：2026-10-29
> 地位：Hepu Shell 开发的最高设计准则，与 PHILOSOPHY.md 同级

---

## 核心纲领

它是 Hepu 原生契约的交互窗口，**不是 POSIX/bash 的复刻**。

定位：一等原生用户态程序，绑定 Hepu 原生ABI；不负责兼容 POSIX 脚本，脚本交给 Linux 兼容沙箱内的 bash。

---

## 1. 模型对齐原则（最高优先级）

Shell 的一切内置语义，和内核的资源、隔离域、设备契约保持一致。

- 进程不再只看 PID，优先展示隔离域 ID、资源配额组、访问权限标签；
- 管道传递优先支持结构化数据（对象、表格），文本只是兼容视图；
- 内置命令 `hepu *` 是内核能力的映射：查看配额、沙箱、代理后端、快照、IOMMU 状态；
- Shell 本身运行在一个普通隔离域，它没有特殊内核后门，权限遵循统一的内核访问契约。

**一句话：Shell 看到的世界，就是内核真实的资源模型，不是模拟出来的 UNIX 进程视图。**

---

## 2. 边界隔离原则

- Hepu Shell 默认命名空间里，看不到兼容子系统内的 Linux 进程、Linux 程序；
- 进入 Linux 脚本环境必须显式：`linux-sh`，或者执行 `.sh` 时自动拉起隔离沙箱；
- 原生程序、兼容程序泾渭分明，禁止混用、隐式跨域调用，避免两套语义纠缠；
- 不内置 POSIX 兼容模式，不慢慢为了脚本语法打补丁。

---

## 3. 简单、可预测原则

拒绝 bash 那种几十年累积的隐晦语法、奇怪隐式规则。

- 语法显式，歧义语法一律删掉；
- 错误信息完整：明确说明是权限契约限制、配额耗尽、设备后端是原生/代理、目标在另一个隔离域；
- 行为确定性优先：同一个命令，同样输入，输出和 side-effect 稳定，不依赖全局隐式状态。

---

## 4. 复用优先，自研克制原则（控制工作量）

v2 基于 Nushell 改造，而不是从零手写解析器、行编辑、补全：

- 复用它成熟的解析、管道、结构化数据、行编辑；
- 只替换底层 syscall 绑定层，对接 Hepu libc / 原生ABI；
- 只增加 Hepu 专属内置，删掉不适合 Hepu 哲学的特性；
- 完全体阶段，如果有足够理由，再考虑裁剪出一个极简自研 Shell。

---

## 5. 可扩展，但不随便扩展

扩展方式优先：外部原生命令，而不是无限往 Shell 内核塞内置。

- `hepu` 是一组相关管理命令的命名空间（`hepu ps`、`hepu sandbox`、`hepu dev`）；
- 普通业务工具独立成外部二进制；
- 不支持无限制的 shell 内置宏魔法。

---

## 6. 调试友好原则

它同时是系统调试入口：

- 方便开发者探查内核抽象层，看清资源仲裁、隔离边界；
- 可以低成本创建、销毁测试沙箱，测试驱动代理、IOMMU、文件快照；
- 输出适合机器读取，方便脚本（原生结构化脚本，不是 sh）、图形管理面板复用。

---

## 7. 文档诚实原则

文档里清晰写明取舍：

Hepu Shell 为 Hepu 内核模型设计，语法、模型和 sh/bash 不兼容。POSIX shell 脚本请使用 Linux 兼容子系统。

不欺骗用户，不承诺未来会补齐 POSIX 兼容。

---

## 当前实现状态（0.54.6）

当前 shell 是内核内置的简易版（shell64.c），是过渡产品。

**已实现：**
- 70+ 基本命令（help/ls/cat/pwd/echo/ps/cd/mkdir/rm/clear/date/uname/ifconfig/free/top 等）
- fork+execve 跑外部 ELF
- pipe 管道
- `hepu` 命名空间命令（ps/sandbox/dev/info/mem/compat/pkg/install 等 40+ 子命令）
- 隔离域视图（hepu ps 展示进程隔离域树）

**与设计原则的差距：**
- [x] `hepu` 命名空间命令（hepu ps / hepu sandbox / hepu dev / hepu info / hepu mem / hepu compat）
- [x] 隔离域视图（hepu ps 展示进程隔离域树）
- [ ] 资源配额展示
- [ ] 结构化数据管道（当前是纯文本）
- [ ] `linux-sh` 显式进入兼容沙箱
- [ ] 基于 Nushell 的 v2 版本
- [ ] 机器可读输出格式
