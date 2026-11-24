# S1 最小生产力：32 位图形终端写读闭环

日期：2026-11-24
状态：S1 端到端场景通过；真实键盘输入、动态上屏、重启后读回均已闭环并存证。

## 1. 结论

S1 要求的“在图形终端里新建文件 → 写入 → 保存 → 重启 → 内容还在”已经在
32 位 hpdesk 上跑通，证据是同一张 ext2 盘上的两次启动截图和串口日志。

同时补齐了终端基础文件工具：

- `edit <file>`：新建/追加编辑，`.save` 落盘，`.quit` 退出，`.list` 查看
- `cat <file>`：读回文件
- `ls [path]`：列目录
- `cp <src> <dst>`：复制并 `vfs_sync`
- `mv <src> <dst>`：复制 + 删源
- `rm <path>`：删文件/空目录
- `mkdir <path>`：建目录
- `run <program>`：调用现有 `cmd_run_auto` 执行用户态程序

这些命令和 shell 的 `fs` 命令共用同一套 32 位 VFS/ext2 后端，没有另开旁路。

## 2. 验收证据

### 2.1 第一次启动：编辑、保存、读回

盘：`/tmp/s1_gui_clean.img`（58 MB ext2 分区）
autostart：

```text
edit /s1_gui.txt
HEPU_S1_GUI
SECOND LINE
.save
.quit
cat /s1_gui.txt
```

截图：`docs/assets/s1/s1-first-boot.png`

终端内可见：

```text
edit: /s1_gui.txt
edit: +
edit: +
edit: saved
edit: quit
HEPU_S1_GUI
SECOND LINE
```

串口关键行：

```text
[S1T] save path=/s1_gui.txt len=0x00000018
```

`0x18` = 24 字节，正好是 `HEPU_S1_GUI\nSECOND LINE\n`。

### 2.2 同一张盘重启：内容仍在

保留同一张盘，只把 autostart 改成一行：

```text
cat /s1_gui.txt
```

重启 QEMU 后截图：`docs/assets/s1/s1-restart.png`

终端直接显示：

```text
HEPU_S1_GUI
SECOND LINE
```

盘上文件由 `debugfs` 交叉核对，内容一致：

```text
HEPU_S1_GUI
SECOND LINE
```

### 2.3 基础文件工具

autostart：

```text
mkdir /s2work
cp /s1_gui.txt /s2work/copy.txt
cat /s2work/copy.txt
mv /s2work/copy.txt /s2work/moved.txt
cat /s2work/moved.txt
rm /s2work/moved.txt
ls /s2work
```

截图：`docs/assets/s1/s1-file-tools.png`

终端内可见 `mkdir: done`、`cp: /s2work/copy.txt`、两段文件内容、
`mv: done`、`rm: done`，最后 `(empty)`。

### 2.4 运行一个静态程序

在图形终端里通过真实键盘输入 `run linuxhello`，串口输出：

```text
Hello from Linux ELF on HEPU!
[RUN] linuxhello -> rc=0x0000000B
```

日志：`docs/assets/s1/s1-run-linuxhello.log`

`linuxhello` 是 initrd 里的 Linux ELF 用户态程序，走现有
`cmd_run_auto` → `hepu_exec` 路径；程序确实打印了自己的欢迎串，不是
shell 内置命令的假输出。

### 2.5 真实键盘输入与动态上屏（闭环）

在 F12 打开的终端里用真实键盘依次输入 `help`、`edit /s1dyn.txt`、
`HEPU_DYNAMIC_OK`、`.save`、`.quit`、`cat /s1dyn.txt`，屏幕逐行刷新，
不需要 autostart，也不是预渲染。

截图：`docs/assets/s1/s1-terminal-dynamic.png`

重启同一张盘后，F12 打开终端输入 `cat /s1dyn.txt`，内容仍在：

截图：`docs/assets/s1/s1-terminal-reboot.png`

这一步同时验证了三件事：键盘事件进入终端状态机、每次命令后屏幕按需
重绘、`.save` 落盘的内容跨启动保留。

## 3. VFS 自测

```text
[VFS-TEST] term.edit 18/18 PASS              终端编辑两行并保存落盘
[VFS-TEST] disk.s1_edit 9/9 PASS             终端编辑内容跨启动保留
[VFS-TEST] RESULT: PASS
写阶段 18/18 PASS；读阶段 9/9 PASS
```

测试盘 `debugfs` 显示 `Free blocks: 52901`，没有出现 S1 排查阶段的
“空闲块被吃光”。

## 4. 本轮同时修掉的四个真实缺陷

### 4.1 `ext2_alloc_block()` 元数据保护记账顺序

原实现先置位块位图、`bg_free_blocks_count--`、`s_free_blocks_count--`，
然后才判断 `ext2_is_metadata_block()`；命中元数据块时直接 `continue`，
位图和计数都没有回滚。每扫到 inode 表/GDT 等元数据候选块，就永久
吃掉一个空闲块位并扣一次 free_blocks，几十次分配后 `Free blocks: 0`。

修复：把 `ext2_is_metadata_block()` 检查移到置位/减计数之前，命中
元数据直接 `continue`，不再动位图和计数。

### 4.2 `cpall` 把伪文件系统当普通目录复制

`cpall /cpall_dst` 递归复制根目录时会把 `/proc`、`/dev`、`/sys`、`/tmp`
逐项当真实文件复制，产生约 5 万次块分配，58 MB 盘被写满，后续
`vfs_write` 全部 ENOSPC。修复：`shell_cmd_fs.c` 的 `cpall` 跳过这四个
非磁盘目录。

### 4.3 32 位图形终端动态内容被绘制全局裁掉

32 位 hpdesk 启动后，`gfx_width/gfx_height/gfx_bpp/gfx_pitch` 仍是 VGA
文本模式的 `320x200@8bpp`，而桌面实际跑在 `1920x1080@32bpp`。
`gfx64_compat` 的 `fill_rect` / `put_pixel` 都按这组旧全局做边界检查，
于是终端窗口的整块黑底和后续新行全部落在 320x200 之外，被静默裁掉；
窗口对象、z-order、render 回调、裁剪矩形本身都正常，所以看起来像
“渲染了但屏幕没变化”。

修复：`hpdesk_render()` 每帧把绘制全局校准到 `g_shell.screen_width/
screen_height`，并置 `gfx_bpp=32`、`gfx_pitch=width*4`。校准后同一份
`fill_rect` 与文字绘制才开始真正落在 1920x1080 的后台缓冲里。

### 4.4 32 位窗口 surface 是空目标，整窗被贴成透明

32 位 `gfx64_target_begin/end` 是空实现，也没有真正的离屏绘制目标。
窗口内容实际画到主 framebuffer，随后 `hs_blend_image` 又把全零的窗口
surface 贴回去，结果是整窗透明：只剩标题栏、边框和桌面背景。Settings
因此“打开但里面什么都没有”，时间胶囊展开层也只剩文字/边框。

修复：32 位一律不再为窗口分配表面（`hs_window_surface_ensure` 直接返回
0），所有窗口和浮层都直接画主缓冲，由桌面帧统一提交。Settings 与时间
胶囊随后完整显示：

- `docs/assets/s1/s1-settings-visible.png`
- `docs/assets/s1/s1-time-capsule.png`（展开态按行顶排版并保留 12px 底边距，
  状态条不再越过圆角面板下沿）

这四项修复后，S1 编辑器才从“创建成功、写入 0 字节”变成真正落盘，
图形终端也才从“只有 autostart 首帧”变成真实键盘逐行上屏，桌面窗口
和浮层也恢复为不透明可读。

## 5. 代码位置

- `kernel/arch/x86/s1_terminal32.c`：32 位 hpdesk 终端、行编辑器、文件工具
- `kernel/arch/x86/hpdesk32_deps.c`：`/etc/hepushell/autostart.cmd` 转发到终端
- `kernel/arch/x86/keyboard.c`：键盘优先交给图形终端；F12 打开终端
- `kernel/arch/x86_64/hpdesk.c`：32 位渲染循环内驱动 `s1_terminal_poll`；
  每帧校准 `gfx_width/height/bpp/pitch`；32 位窗口禁用空 surface 直绘主缓冲
- `kernel/fs/ext2_alloc.c`：元数据块保护顺序修复
- `kernel/sched/shell_cmd_fs.c`：`cpall` 跳过伪文件系统
- `kernel/fs/fs_vfs_selftest.c`：`term.edit`、`disk.s1_edit` 与 cpall 空闲块断言

## 6. 已知限制

- 动态键盘行的屏幕刷新已闭环，根因与修复见 4.3；`s1-terminal-dynamic.png`
  与 `s1-terminal-reboot.png` 是本次修复后的真实键盘取证。
- 32 位终端 `run` 的用户程序输出走串口/VGA，不回显到终端窗口；这是
  现有用户态程序的输出通道决定的。
- `/data/clock/alarms.txt` inode 32 的 deleted/unused 记账问题仍未定位，
  不在 S1 范围。

## 7. 复现

```bash
cd hepu-os
export T=/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/root2
export DD=/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/datadir
export PATH=$T/sbin:$T/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

make -j2
qemu-system-i386 -L "$DD" -kernel build_v250/kernel.elf -m 256 \
  -display none -vnc 127.0.0.1:9 \
  -drive file=/tmp/s1_gui_clean.img,format=raw,if=ide \
  -serial file:/tmp/s1.log
```

截图：`vncdo -s 127.0.0.1::5909 capture /tmp/s1.png`

VFS 回归：

```bash
bash tools/verify_p1p3.sh
```
