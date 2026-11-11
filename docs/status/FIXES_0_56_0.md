---
title: fsck 接入 inode 检查与欢迎引导开关（0.56.0）
status: active
sys_version: 0.56.0
updated: 2026-11-11
owner: status
source_of_truth: kernel/arch/x86_64/
supersedes: null
---

# fsck 接入 inode 检查与欢迎引导开关（0.56.0）

## 一、`check_inode` 接入 fsck 主流程

### 问题

`ext2_fsck64.c` 里的 `check_inode()` 早已实现（含孤立 inode 判据 `i_links_count == 0`、
`fsck_stats.inodes_checked++`），但被标为 `__attribute__((unused))` ——
`ext2_fsck_run()` 只做三件事：检查超级块、算块组数、检查块组描述符。
**inode 表从未被遍历**，这个检查函数是彻底的死代码。

这解释了为什么 `disk fsck` 的汇总里 `Inodes checked` 恒为 0。

### 修复

新增 `check_inode_tables()`，按 `s_inodes_per_group × num_groups`（并以
`s_inodes_count` 封顶）遍历全部 inode，逐个调用 `check_inode()`：

- inode 读取复用 `ext2_read_inode()`（`ext2_64.c` 已实现，自行处理 `s_inode_size`
  与组号越界），不另造读路径；
- **从 inode 2 起遍历**：inode 1 是 ext2 规范里的坏块保留 inode，其链接计数可能为 0，
  逐 inode 检查会稳定误报；
- `s_inodes_per_group == 0` 时直接返回 0，避免除零与死循环；
- 在 `ext2_fsck_run()` 的第 3 步（块组描述符）之后、输出统计之前调用，
  计入 `total_errors`。

### 顺带发现的隐患：两套 ext2 结构定义

`ext2_fsck64.c` 自带一份 `ext2_superblock_t / ext2_group_desc_t / ext2_inode_t`
定义（非 packed），与 `kernel/include/ext2.h` 中的 packed 版本**同名但不同类型**。
`ext2_read_inode()` 定义在另一个编译单元、用的是 `ext2.h` 的类型，
所以这次遍历是"跨两套结构定义"传数据 —— 一旦任一侧字段增删，就会静默读错字段。

核对结果：fsck 那份定义字段顺序与标准一致，且自然对齐后恰好也是
inode 128 B / 块组描述符 32 B，与磁盘布局吻合，当前不会读错。为防漂移，
加了编译期断言：

```c
_Static_assert(sizeof(ext2_inode_t) == 128, "fsck 的 inode 布局必须与磁盘 (128B) 一致");
_Static_assert(sizeof(ext2_group_desc_t) == 32, "fsck 的块组描述符布局必须为 32B");
```

## 二、欢迎引导（OOBE）可关闭

### 行为

启动时的图形化首设向导由 `desktop64.c` 中一句决定：

```c
if (!auth64_is_configured()) hs_oobe_begin();
```

即"账户未配置就弹向导"，没有开关。

### 修复

在 `config.hsqml` 的 `UI` 段新增 `skip_oobe`：

```
UI {
    skip_oobe: 1      // 关闭启动欢迎引导(OOBE)，直接进桌面
    ...
}
```

链路：`hpdesk_load_config()` 解析 `UI.skip_oobe` → 全局 `g_skip_oobe`
→ 导出 `hpdesk_skip_oobe()` → `desktop64.c` 判定改为
`if (!auth64_is_configured() && !hpdesk_skip_oobe())`。

- 初始化顺序已确认安全：`hpdesk_init()`（含配置加载）在 OOBE 判定之前执行，
  判定时开关值已就绪（串口 `[Hepu Desktop] config: ...` 先于向导出现）；
- `hs_oobe_active()` 在未 `begin()` 时返回 0，桌面各输入/渲染路径的拦截条件自然失效，
  无需改动 `hpdesk.c` 里的 4 处判断；
- 内置 `default_config` 中该值为 0（没有配置文件时保持原行为），只在镜像内的
  `rootfs/etc/hepushell/config.hsqml` 置 1。

副作用：镜像内不再写 `/account.conf`、`/system.conf`、`/lang.conf`，
`auth64_is_configured()` 持续为假。当前系统没有依赖该状态强制登录的路径，
桌面可正常使用（已实测）。

## 三、顺带清理

- **pit64 遗留调试栈转储**：`g_kernel_tick64` 落在 5001~5009 时无条件打印 9 行
  24 字栈快照，每次启动污染串口日志（曾被误当作异常堆栈排查）。已删除。
- **7 处过期对外版本文案**：启动横幅与 `version` 命令仍打印 **0.54.12**，
  终端 `Hepu Terminal v0.54.12`、Notes 欢迎页、`hpdesk` System Info 面板同理。
  统一为 0.56.0（历史修复注释里的 `V0.54.12` 作为变更记录保留不改）。

## 四、验证

| 项目 | 结果 |
|---|---|
| 全量重编 | 0 error，89 warning（全为 unused-parameter，未新增） |
| fsck 主机回归（新增） | 5 组用例通过：空闲 inode 不报错、`links==0` 报 1 错、正常 inode 不报错、遍历计数正确、`inodes_per_group==0` 安全退出 |
| 主机回归总计 | **8 / 8**（原 7 项 + fsck） |
| QEMU `-smp 1` | 正常进桌面，**无欢迎引导**（截图直接显示桌面与设置面板） |
| 串口 | `[Hepu Desktop] config: 3 sections, background=0xf0ebe0`；无异常堆栈 |
| 文档 | `check_docs.py` 0 warn 0 error |

## 五、遗留

- `disk fsck` 的运行时耗时未在实机量化：遍历 1 万余 inode、每个走一次
  `ext2_read_inode`（一次块读），QEMU 内可能需数秒。若后续在慢速 IDE 设备上
  执行，可改为按 inode 位图跳过空闲 inode 或按块批读。
- fsck 目前只报错不修复（`errors_fixed` 恒为 0），未实现 `-y` 类自动修复。
- `-Wunused-parameter` 89 条按约定保留（接口签名噪音）。
- `-smp 4` 卡在 AP trampoline（既有缺陷，与本次无关）。
