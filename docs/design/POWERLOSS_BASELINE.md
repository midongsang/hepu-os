# 真掉电基线（D3 第一步）

> 2026-11-23。目的：先测清楚“现在到底会坏成什么样”，再决定 journal 要不要做。
> 结论：**元数据会不一致，但 e2fsck 能全部修回；暂不做 journal，先做有序写 + FLUSH + 脏标志。**

## 方法

脚本 `tools/powerloss_test.sh`：

1. `make_single_img64.sh` 造干净 ext2 盘（224MB @ LBA13000，块大小 4096）；
2. 注入 `/powerloss_test` 标记，32 位启动后跑阶段 `'p'`：以 1KB 为单位持续写 `/pl.dat`，每 8 块 `vfs_sync` 一次，每 64 块打一行串口；
3. QEMU 用 **`-drive cache=unsafe`**（忽略 flush，模拟盘上没有电容/写缓存）；
4. 串口出现第一个 `chunk=` 后再等 2 秒，`kill -9` QEMU；
5. 宿主机抽出分区，`e2fsck -f -n` 看损伤，再对副本 `e2fsck -f -y` 看能否修回。

关键：默认 `cache=writeback` 下 `kill -9` 只是杀进程，宿主页缓存仍会落盘，**测不到掉电**。必须 `cache=unsafe`。

## 实测样本（2026-11-23）

```
串口最后一行: [POWERLOSS] chunk=2304     # 约 2.25MB 已写
镜像中 pl.dat: 2416640 字节
e2fsck -n: exit=4（发现不一致）
e2fsck -y: exit=1（全部修复）
Fix? yes 共 15 处
```

损伤清单：

- `Inode 76/80/81/83/85/87/89/91/93, i_blocks is 0, should be 8` —— inode 已落盘（有大小/块指针），但 `i_blocks` 计数没落盘；
- `Inode 2 ref count is 10, should be 12`、`Inode 80 ref count is 2, should be 9` —— 目录引用计数滞后；
- `Block bitmap differences: -(4472--4473)`、`Free blocks count wrong` —— 块位图与超级块空闲计数不一致。

`e2fsck -y` 之后 `pl.dat` 仍在（2416640 字节），目录可读。

## 这说明什么

1. 现在**没有有序写**：inode 表、块位图、超级块计数各自按需刷，崩溃时天然不同步；
2. 损伤是“可修的元数据不一致”，不是“目录树断裂/数据块被当空闲再分配”；
3. 因此 journal 的收益暂时不成立 —— 先补：
   - 写顺序：数据块 → inode → 位图 → 超级块计数；
   - `vfs_sync` / 卸载时发 ATA `FLUSH CACHE (0xE7)`，而不是只写 QEMU 页缓存；
   - 挂载时脏标志（上次未正常卸载）触发 `fsck`，把“修”自动化。

## 边界与不确定

- `kill -9` 模拟的是“宿主进程瞬间消失”，不是硬件掉电；SSD/HDD 内部缓存行为未覆盖；
- 只测了 32 位 IDE PIO + 4096 字节块；1024 字节块、64 位路径未测；
- 单样本，未做“同一负载重复 10 次”的分布统计；
- 未测“写到一半时目录项正在创建/删除”的窗口（当前负载是单文件追加写）。

## 下一步（D3 收口条件）

- 连续 10 次样本中，**不出现 e2fsck 修不回的项**（exit 4/8）→ journal 继续推迟；
- 一旦出现目录项丢失、块被重复分配、fsck 修不回 → 立即升级为 journal 方案。
