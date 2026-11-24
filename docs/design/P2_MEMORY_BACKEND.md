# P2 内存与并发地基：buddy 单一后端

## 1. 问题

P2 之前，物理页有两套账：

- `kernel/mm/pmm.c` 的 `memory_bitmap` 是真实占用位图，`mm_alloc_page()` /
  `mm_alloc_pages()` 直接从位图线性扫描。
- `kernel/mm/buddy.c` 另有一份 `buddy_free_bitmap[order]`。buddy 分配时先用
  `mm_page_used()` 过滤 pmm 侧保留区/页表页/已占用页，再逐页调用
  `mm_alloc_page_at()` 补记；释放时先改 buddy 位图并合并伙伴，再逐页清 pmm 位。

这就是“先分配再补记”的双账结构。只要有一次分配没有同时经过两侧，或者释放
顺序不同，就会出现：pmm 已把页分给驱动/页缓存，buddy 仍认为它空闲；或者
buddy 已合并成高阶块，pmm 里还有页没清，导致整块再也分配不出去。

## 2. 改造结果

### 2.1 唯一事实源

`kernel/mm/buddy.c` 不再维护任何空闲位图，只保留：

- `buddy_total_pages`：初始化时记录的总页数；
- `buddy_hint_page`：下一次扫描的页号提示；
- `buddy_initialized`：是否已接管 pmm 后端。

分配时按 `2^order` 对齐扫描块，先对块内每页调用 `mm_page_used()` 做整块预检，
确认全部空闲后再逐页 `mm_alloc_page_at()` 标记。任何一页标记失败都会回滚本块
已标记的页，不把半分配的块留在账上。

释放时只逐页清 pmm 位图，不做第二套位图合并。连续空闲页会在下一次高阶分配时
被重新识别，因此“释放后大块可复用”仍然成立。

### 2.2 统一入口

`buddy_init()` 现在调用 `mm_enable_buddy_backend()`。此后：

- `mm_alloc_page()` → `buddy_alloc_pages(1)`
- `mm_alloc_pages(n)` → `buddy_alloc_pages(n)`
- `mm_free_page()` / `mm_free_pages()` 仍然直接清 pmm 位图，释放路径只有一套。

初始化前的早期启动仍保留 pmm 线性位图路径，只用于 `mm_init()` 到
`buddy_init()` 之间的引导阶段。

### 2.3 二次分配断言

`mm_alloc_page_at()` 在 `HEPU_BOOT_DEBUG` 构建下增加断言：目标页的 pmm 位已置位时，
先打印物理地址和页号，再 `assert_fail()` 停机。正常 buddy 路径已经在整块预检中
用 `mm_page_used()` 排除了已占用页，所以断言只会在绕过预检、并发记账错误或
调用方重复分配时触发。

实测探针：在 `buddy_selftest()` 里临时对已分配的 `p1` 再调用一次
`mm_alloc_page_at(p1)`，串口输出：

```
[BUDDY] test1 alloc 1 page: PASS (addr=0x09148000)
[MM] FATAL: double alloc page phys=0x0x09148000 page=0x00009148
[ASSERT] FAILED: double physical page allocation at kernel/mm/pmm.c287
```

探针随后已撤回，最终代码不包含该临时调用。

### 2.4 释放块大小

`buddy_alloc_pages(n)` 会把请求向上取整到 `2^order`；`buddy_free_pages(phys, n)`
现在也按同一个 `2^order` 块大小释放，避免 DMA 这类非 2 的幂请求只释放尾部
一部分页造成泄漏。`mm_free_pages(phys, n)` 仍按调用方给的精确页数逐页释放，
不受此影响。

最大 order 从 10 提到 20（1M 页 / 4GB），覆盖 swap 的 2048 页和 GPU 帧缓冲
这类大于 4MB 的连续请求。

## 3. 新增测试

`kernel/kunit/kunit_stress.c` 新增 3 个用例，stress 套件从 6 个测试变成 9 个：

- `buddy_single_backend`：验证 buddy 已接管；连续分配 1/2/4/8/16 页并检查块对齐，
  写每页首尾字节，释放后重复 5 轮；结束时检查 pmm 空闲页没有明显泄漏。
- `slab_all_sizes_reuse`：对 32/64/128/256/512/1024 六档各分配 8 个对象并写首尾，
  释放后重复 6 轮；检查累计分配计数增加、slab 页数没有随轮次持续增长。
- `page_cache_pressure`：创建页缓存，逐页分配 4096 个物理页并作为
  `PC_FLAG_UPTODATE` 条目插入；释放引用后第 4097 页触发 LRU 回收 1024 页；
  检查总数不超过 `PAGE_CACHE_MAX_PAGES`、确实发生回收、无脏页，销毁后物理页
  回到 pmm（允许 slab 元数据页残留 128 页余量）。

## 4. 实测

`tools/gate.sh` 连续通过：

```text
[1/5] 构建 32 位
  ✓ 32 位构建 0 error
[2/5] 构建 64 位
  ✓ 64 位构建 0 error
[3/5] VFS 跨启动（verify_p1p3.sh）
  ✓ 写阶段 + 读阶段 PASS
[4/5] kunit 连续 3 次启动
  ✓ 第 1 次：ALL PASS
  ✓ 第 2 次：ALL PASS
  ✓ 第 3 次：ALL PASS
[5/5] 结论
GATE: PASS
```

`stress` 套件在每次启动中均为 9 tests / 9 passed / 0 skipped。
buddy 启动日志：

```text
[BUDDY] single-backend total_pages=0x0000FF7F free_pages=0x00006E37
[BUDDY] selftest: done (pass=0x00000005, fail=0x00000000)
```

### 4.1 30 分钟长稳

`tools/stress_long.sh 1800` 在 32 位 debug 构建（`HEPU_BOOT_DEBUG`）上跑满
1800 秒，普通 kunit 全部通过后进入长稳循环：

```text
[KTEST] RESULT: ALL PASS
[P2STRESS] PASS seconds=0x00000708 rounds=0x000EE160 skips=0x00000000
           free_start=0x0000627D free_warm=0x0000627D free_end=0x0000627D free_min=0x0000627D
[P2STRESS] RESULT: PASS
```

- `seconds=0x00000708` 即 1800 秒；共 975,200 轮，0 skip。
- 空闲页从预热后的 `0x627D` 到结束仍是 `0x627D`，`free_min` 全程没有下降，
  未发现 buddy/slab/kmalloc/页缓存泄漏。
- 全程 0 个 `[KUNIT-ASSERT]` / `[ASSERT] FAILED` / `[PANIC]` / `[EXCEPTION]`，
  也没有 watchdog 复位标记。

脚本在长稳循环自行打印 `PASS` 后由 QEMU timeout 收尾（本机 32 位 ACPI
关机路径只 `hlt` 不退出），因此总耗时 2157 秒；`rc=124` 是预期收尾，
判定以串口日志为准。

## 5. 已知边界

- buddy 仍是调用方串行化，没有内部锁；当前 32 位运行路径没有 SMP 并发分配，
  后续引入 SMP 时需要把预检 + 标记做成原子区间，或加分配器锁。
- `buddy_alloc_pages()` 扫描是 O(总块数 × 块大小)，目前靠 `buddy_hint_page`
  把常规单页分配压到近似 O(1)；内存碎片极端时最坏情况仍可能变慢。
- 长稳压测只在 32 位 debug 构建、QEMU 256MB 内存下跑过；64 位和真机长时间
  压力仍未覆盖。长稳循环本身是单核串行，不能替代 SMP 并发分配测试。
- 32 位窗口直绘替代离屏合成、inode 32 记账等 S1/P1 尾巴不在 P2 范围。
