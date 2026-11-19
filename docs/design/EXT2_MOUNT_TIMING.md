# ext2 首次挂载失败、重试成功的时序问题

> 2026-11-19 登记。现象在 32 位磁盘验证时暴露，根因未定位，这里只登记现象、
> 代码链路和排查方案。**不要在没有证据前改 mount 逻辑。**

## 一、现象

同一份串口日志里同时出现这几行：

```
[EXT2] selftest: mount FAIL
[FS] ext2 filesystem found on IDE drive 0x00000000 partition LBA=0x800
[SELFTEST] /bsod_selftest found -> bsod
```

第一行说挂载失败，第三行说明文件明明读到了。也就是说 **`fs_mounted` 报失败，
但读路径实际可用**。

## 二、代码链路（为什么两者能同时成立）

```
ide_ext2_selftest()          kernel/fs/ide_ext2_selftest.c:16
  └─ ide_ext2_mount()        kernel/fs/ide_ext2.c:223
       └─ 失败 → 打印 "mount FAIL" → 直接 return（此时 fs_mounted 仍为 0）

ide_ext2_read_file()         kernel/fs/ide_ext2_fm.c:117
  └─ if (!fs_mounted) ide_ext2_mount()   ← 这里又调了一次
       └─ 这一次成功 → fs_mounted = 1 → 文件读到
```

关键点：**`mount` 不是幂等失败，`read_file` 会自己重试一次**。所以真实情况是
“第一次挂载失败，第二次成功”，不是“失败但能读”。

## 三、为什么第一次会失败

`ide_ext2_mount()` 有四个无声失败点（已由 `e0add8a` 补上打印）：

| 失败点 | 含义 | 先查什么 |
|---|---|---|
| `bad data_sectors` | 分区扇区数为 0 或超 128GB | MBR 分区项 `sector count` 是否为 0 |
| `resident read failed` | 读块 0–31 到 `sb_area` 失败 | `ide_read_range` 的 LBA 计算、IDE 驱动是否就绪 |
| `superblock magic mismatch` | `sb_area + 1024 + 56` 不是 `0xEF53` | 读出来的内容是不是全 0 / 错位 |
| `ext2_init failed` | `ext2_init` 内部的 magic 校验/大小检查 | `sb_area` 内容与 `size` |

候选根因（按可能性排序）：

1. **IDE 驱动状态未就绪**：`ide_set_current_drive(i)` / `ide_get_drive_info`
   在第一次调用时驱动还没完成初始化，第二次时已就绪。
2. **静态缓冲时序**：`sb_area`、`mbr`、`probe_buf` 都在 `.bss`，如果第一次
   读之前被清零、或读到的是旧内容（和 32 位 LFB 那次“`.bss` 缓存被清掉”
   是同一类坑）。
3. **PIO/DMA 模式**：日志有 `[IDE-DMA] QEMU TCG, DMA disabled, PIO only`，
   第一次读可能走了未初始化的 DMA 路径。
4. **页缓存初始化**：`g_block_cache = page_cache_create(...)` 在 mount 末尾，
   不影响 superblock 读，排除。

## 四、排查步骤（需要能跑 QEMU）

1. 用 `e0add8a` 之后的镜跑一次，拿到**第一次失败的具体打印**（四选一）
2. 同时打印 `data_lba` / `found_data_sectors` / `sb_area` 前 16 字节，
   判断是“没读到”还是“读到了但内容不对”
3. 对比第二次 mount 的同一组值——若第二次相同却成功，就是**状态**问题
   （驱动/缓冲），不是数据问题
4. 若第二次值不同（如 `data_lba` 变了），就是**分区解析**问题

## 五、修复方向（待证据，不要现在改）

- **首选**：让第一次就成功——把 `ide_ext2_selftest` 挪到 IDE 驱动初始化之后，
  或让 `mount` 等到驱动就绪
- **次选**：`mount` 失败时区分“可重试”（驱动未就绪）与“真不可用”（盘上没有
  ext2），避免自检误报 FAIL
- **不要做**：在 `selftest` 里简单重试一次就宣告成功——那只是把问题藏起来，
  日志仍然误导

## 六、影响面

- 32 位镜像验证不稳定（`mount FAIL` 随机出现），是之前两条未取证项
  （CMOS 跨重启横幅、编解码自检第 5 项）反复失败的背景原因之一
- 桌面侧的 `ide_ext2_read_file`（IME、联系人、短信、照片）都依赖这次重试，
  如果第一次失败发生在更早的路径上，这些功能会静默失败
