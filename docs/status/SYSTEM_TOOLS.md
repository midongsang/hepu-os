# 系统工具台账（CLI）

对应「基础系统信息 / 进程资源 / 存储 / 用户权限 / 网络 / 硬件 / 日志调试 / 包管理 / 服务 / 图形面板 / 高级」十一组清单的落地现状。
判定口径：**shell 里有真实实现并通过内核接口取数**才算「有」，只有名字没有数据的算「缺」。

## 一、基础系统信息

| 工具 | 状态 | 说明 |
|---|---|---|
| uname | 有 | 内核版本、架构、主机名 |
| hostname | 有 | 读写主机名 |
| uptime | 有 | 开机时长；负载未统计（无 loadavg 采样） |
| date | 有 | RTC 直读 |
| timedatectl | 有（v0.59.3 新增） | 时间 + 时区 + NTP 状态（NTP 未实现，如实回报） |
| id | 有（v0.59.3 新增） | 角色映射 uid：developer=0 / normal=1000 |
| who / w | 有（v0.59.3 新增） | 单会话 tty1 + 开机时长 |

## 二、进程与资源

| 工具 | 状态 | 说明 |
|---|---|---|
| ps | 有 | 开发者视图含 PID/PPID/PRIO/STATE/DOMAIN/CPU/MEM/RUNTIME |
| top / htop | 有 | `top` 为交互式；`htop` 是别名 |
| vmstat | 有（v0.59.3 新增） | 进程数 / 运行数 / 内存三项；swap 与块设备计数器未接入 |
| pstree | 有（v0.59.3 新增） | 按 parent_pid 递归，最多 8 层 |
| free | 有 | 物理页统计 |
| kill / killall | 有（v0.59.3 新增） | `process64_kill_pid` / `process64_kill_by_name`：标 SIGKILL + 置 ZOMBIE，资源交给 waitpid 回收；idle 与当前进程拒绝 |
| nice / renice | 有（v0.59.3 新增） | `process64_set_priority` 现在同步重算 default_slice（旧实现只改 prio 不改片长，renice 假生效） |
| taskset | 有（v0.59.3 新增，受限） | `process64_set_affinity` 改 cpu_id；**AP 调度未开放（SB-25），绑到 AP 只记账不迁移**，命令里会明说 |

## 三、存储与文件系统

| 工具 | 状态 | 说明 |
|---|---|---|
| lsblk | 有（v0.59.3 新增） | 走 partition64，仅 MBR；GPT 盘如实回报「无 MBR 签名」 |
| blkid | 有（v0.59.3 新增） | 分区类型 + ext2 判定；**UUID 未持久化**，不伪造 |
| df | 有 | ext2 超级块统计 |
| du | 有 | 递归统计 |
| sync | 有（v0.59.3 新增） | `blk_cache_flush` + `ext2_cache_flush_all` |
| mount / umount | 缺 | 当前是固定挂载表（启动时建立），无运行时挂载点管理 |
| fdisk / parted | 缺 | 无分区编辑器；只读扫描 |
| mkfs | 缺 | — |
| fsck | 缺 | — |
| swapon / swapoff | 缺 | 无 swap 子系统 |

## 四、用户与权限

| 工具 | 状态 | 说明 |
|---|---|---|
| id | 有 | 见上 |
| useradd / userdel / usermod | 缺 | 账号体系是单账号（account.conf），无多用户管理 |
| groupadd 等 | 缺 | 无组模型 |
| passwd | 部分 | 有改密入口（account 重写），无独立命令 |
| chmod / chown / chgrp | 缺 | 权限位在 inode 里有，缺 shell 层命令 |
| su / sudo | 缺 | 角色模型是 developer/normal，无提权切换 |

## 五、网络

| 工具 | 状态 | 说明 |
|---|---|---|
| ifconfig | 有 | 地址/掩码/网关 |
| ip | 缺 | 未做 `ip addr/route` 子命令解析 |
| netstat / ss | netstat 有，ss 为别名 | 套接字列表 |
| ping | 缺 | 无 ICMP 发送路径 |
| route | 缺 | 路由表由 DHCP 写入，无查询命令（可并入 `ip route`） |
| dhclient | 部分 | DHCP 客户端在启动流程里，非独立命令 |
| tcpdump | 缺 | 无抓包钩子 |

## 六、硬件

| 工具 | 状态 | 说明 |
|---|---|---|
| lspci / pci | 有（lspci 为别名） | PCI 枚举 |
| lsusb / usb | 有（lsusb 为别名） | USB 枚举 |
| lscpu / cpuinfo | 有（lscpu 为别名） | CPU 型号/核数 |
| dmesg | 有（v0.59.3 新增） | `klog64_snapshot` 环形缓冲 |
| sensors | 缺 | 无温度/风扇采集（ACPI thermal zone 未接） |

## 七、日志与调试

| 工具 | 状态 | 说明 |
|---|---|---|
| dmesg | 有 | 见上 |
| journald / syslog | 缺 | 日志只在内存环形缓冲，未落盘 |
| strace | 缺 | 需内核侧系统调用桩（大工程） |
| gdb | 缺 | 无调试器 |
| objdump / readelf | 缺 | elf64 解析器在内核里，未暴露成命令 |

## 八~十一

- 包管理：只有 App Store（GUI），无 CLI 包管理器；`sha256sum` 可用 `auth64_sha256` 实现（未做）
- 服务管理：无 systemd 类框架，桌面/网络服务由内核启动流程拉起
- 图形面板：任务管理器、磁盘管理器、设备管理器、日志查看器、设置（显示/外观）均已有，后端就是上面这些 CLI 的同一批数据源

## 下一步建议（按依赖排序）

1. ~~kill / killall / renice / taskset~~ 已完成（v0.59.3）。剩 `nice <prio> <cmd>` 的启动前设优先级，等有 spawn 命令后再说。
2. **ip / ping**：`ip addr`、`ip route` 走现有网卡与路由表；ping 需要 ICMP 发送，可与 e1000 驱动共用。
3. **sha256sum**：零成本，`auth64_sha256` 已导出。
4. **chmod / chown**：inode 权限位已在，缺 shell 解析。
5. **objdump / readelf**：把 elf64 的节表解析暴露成命令。

## 取证通道（v0.59.3）

这台开发机跑 QEMU 用 `-display none`，鼠标/键盘事件进不到系统里，CLI 改动没法手动验证。
解决办法是一条「启动即执行」通道：

1. 写命令到 `rootfs/etc/hepushell/autostart.cmd`（一行一条），重新构建镜像；
2. 桌面初始化末尾读这个文件，逐行交给 `hs_terminal_run_line()` —— 它会开终端窗口并执行；
3. 命令输出经 `shell_set_output()` 钩到终端行缓冲（按 `\n` 切行、`TERM_MAX_LEN` 截断），
   于是 `vga_puts` 写的文本模式输出直接显示在终端窗口里；
4. `qemu` 起来 7 秒后 `screendump`，截图就是证据。

配套修的两个真实缺陷：
- **rootfs/ 从来没被打包进镜像**：`build_uefi_dual.py` 只写了字体/壁纸/光标，
  所以 `/etc/hepushell/config.hsqml` 一直是「期望存在、实际缺失」，配置读取永远落回内核内嵌模板。
  现在 `_pack_rootfs()` 递归写入，镜像配置才真正生效。
- **桌面终端和 shell 命令表是两套**：`shell_execute` 注释写着「导出给图形终端调用」但从未接线，
  于是在终端里敲 `dmesg` 得到 `Unknown command`。现在未识别命令转给 `shell_has_command()`
  判定后走 `shell_execute()`，shell64 里的全部命令（ps/df/dmesg/ss/pstree…）在桌面终端里可用。

文件默认留空 —— 空文件不执行任何命令，只在需要取证时写进去。
