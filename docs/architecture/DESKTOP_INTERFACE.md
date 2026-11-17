# 桌面接口契约（DESKTOP_INTERFACE）

> 起因：32 位桌面原本是内核内一套 4522 行的 `desktop_render.c`，64 位是
> `hpdesk.c` 8206 行 + `hs_apps.c` 4498 行。两套各写各的，永远无法共用，
> 也和 32 位"微内核、应用在用户态"的定位相悖。
>
> 这份文档定一件事：**桌面是一份逻辑，后端是可替换的，用户能自己加桌面。**

## 一、分层

```
┌─────────────────────────────────────────────────────────┐
│ 桌面实现   desk_shell.c / desk_hpdesk.c / 用户自建        │
│ 只调 desk_surface_t, 不碰硬件                            │
└─────────────────────────────────────────────────────────┘
              ↓ desk_surface_t（绘制 + 尺寸 + 缩放）
┌─────────────────────────────────────────────────────────┐
│ 后端（各架构/各形态各自提供）                             │
│  · 32 位用户态: mmap 后的 LFB 基址                        │
│  · 32 位内核内: e.g. bochs dispi LFB                     │
│  · 64 位:      gfx64 的线性帧缓冲                        │
└─────────────────────────────────────────────────────────┘
```

**硬判据**：桌面实现里不允许出现 `outb` / `inb` / `0xA0000` / 物理地址 /
寄存器名。一旦出现，就说明它偷偷做了后端的事，违反
`GRAPHICS_BOUNDARY.md` 三条规则里的"硬件在两边、算法在中间"。

## 二、契约

全部定义在 `kernel/shared/desktop/desk_api.h`。

| 类型 | 作用 |
|---|---|
| `desk_surface_t` | 桌面看到的世界：宽高、bpp、pitch、缩放百分比，以及 7 个绘制入口 |
| `desk_input_t` | 一个输入事件：鼠标移动/按下/抬起、按键按下/抬起 |
| `desk_backend_t` | 一个桌面实现：`init` / `frame` / `event` / `fini` |

`desk_surface_t` 的绘制入口语义固定：

- 颜色一律是 `0x00RRGGBB`，不带 alpha；
- 坐标是**逻辑像素**，后端负责按 `scale_pct` 换算；
- `present()` 只在双缓冲后端需要，单缓冲可留空。

## 三、可切换：注册表

> **定位说明**：注册表是**本项目新加的设计**，不是对既有 `hpdesk.c` 的现有
> 结构的描述。`hpdesk.c` 原本是"内核里直接被调用的一份桌面"，没有注册、
> 没有选择、没有"换一个桌面"的概念。这一层是 2026-11 为了"旧桌面保留但
> 迁出内核 / 留可切换的图形化桌面接口"两个要求加出来的，写在这里是为了让
> 后来人知道它是新增契约，不是既有事实。

`desk_registry.c` 提供：

```c
int             desk_register(desk_backend_t *d);   /* 加一个桌面 */
int             desk_select(const char *name);      /* 切到某个 */
desk_backend_t *desk_current(void);
int             desk_count(void);
desk_backend_t *desk_at(int index);                 /* 遍历 */
int             desk_list(const char **out, int max);
```

规则：

1. **第一个注册的成为默认**，不需要额外指定。
2. 名字唯一，重名注册返回 `-2`。
3. 切换只改索引，不动任何硬件状态 —— 后端坐标系由 `desk_surface_t` 保证一致。
4. 上限 `DESK_MAX_BACKENDS` = 8。

## 四、用户怎么加自己的桌面

```c
static int my_init(desk_surface_t *s)  { return 0; }
static int my_frame(desk_surface_t *s) {
    s->fill_rect(s, 0, 0, s->width, s->height, 0x101018);
    s->draw_text(s, 40, 40, "我的桌面", 0xFFFFFF, 20);
    return 0;
}
static desk_backend_t my_desk = {
    .name = "mine", .display_name = "我的桌面",
    .init = my_init, .frame = my_frame,
};

desk_register(&my_desk);      /* 加进去 */
desk_select("mine");          /* 切过去 */
```

不需要改内核、不需要碰寄存器、不需要知道帧缓冲在哪。

## 五、当前状态

| 项 | 状态 |
|---|---|
| `desk_api.h` + `desk_registry.c` | ✅ 已建（纯逻辑，32/64 都能编） |
| 32 位内核内旧桌面（`elastic_desktop_main_loop`） | ⛔ **已下线**，不再被调用 |
| 32 位经注册表起桌面 | ✅ **已接通**：`init_sequence.c` 在 32 位上调 `desk_start(100)` → 打 `[EDESKTOP] desk_start on 32-bit`，再 `desk_tick()` 量首帧周期 |
| 64 位经注册表起桌面 | ✅ **已接通**：`desktop64_init` 走同一条 `desk_start` 路径，最终进 `hpdesk_init` |
| 32 位与 64 位共用同一份桌面源码 | ✅ `hpdesk.c` 同一份文件同时编进两个架构，视觉以 64 位为准 |
| 32 位用户态桌面（搬出 ring0） | 🚧 未做：依赖 `SYS_FB_DESC` + `SYS_MMAP` 真映射，另见 `KERNEL_SCOPE.md` §5-4 |
| 默认桌面 `desk_shell` | ⏳ 未做（当前注册表里是 `hpdesk`） |

## 六、与其它文档的关系

- `GRAPHICS_BOUNDARY.md`：三层模型（共享算法 / 32 位实现 / 64 位实现）。
  本文档在其之上加了一层"桌面"—— 桌面属于**共享层**，因为它只需要 surface。
- `KERNEL_SCOPE.md` §5-4：framebuffer 契约（`SYS_FB_DESC`）是本文档的第一块地基。
- `UNIFIED_SOURCE_TREE.md`：渐进共享。桌面先做成接口，再逐个架构接上。

## 七、注册表与运行时实测契约（新增，非既有设计，2026-11-17）

> **标注**：本节为 2026-11-17 按当前代码实况补写，**不是历史设计稿**，也不是对
> 既有 `hpdesk.c` 结构的回溯描述。下文「实测」= 本次在源码中直接读到的事实；
> 「推断」= 由调用位置推出、代码未显式写明的判断。源码位置以本次检出为准。
> 本节只覆盖注册表 / 运行时一侧；`desk_surface_t` / `desk_input_t` /
> `desk_backend_t` 类型本体的契约见 §二。

### 7.1 实现位置（实测）

| 层 | 文件 | 内容 |
|---|---|---|
| 注册表 | `kernel/shared/desktop/desk_registry.c` | 注册 / 选择 / 遍历；声明在 `desk_api.h` |
| 运行时 | `kernel/shared/desktop/desk_runtime.c` | 启动、取帧、事件分发、节流、前台所有权 |
| 内置后端包装 | `desk_hpdesk.c`、`desk_demo.c` | hpdesk 正式后端；demo 仅验证"可切换" |

- 运行时入口（`desk_start` / `desk_tick` / `desk_tick_if_due` / `desk_dispatch` /
  `desk_active_surface` / `desk_is_running` / `desk_fb_owner_*`）**没有写进
  `desk_api.h`**，调用方各自在本地 `extern` 声明。
- 两侧都编入：32 位根 Makefile 用 `find kernel -name "*.c"` 全量收编 `shared/`；
  64 位 `kernel/arch/x86_64/Makefile` 显式列出五份 desktop 源文件。

### 7.2 注册表状态与结构体字段（实测）

表内静态状态（`desk_registry.c`）：

- `g_desks[DESK_MAX_BACKENDS]`：指针表，上限 `DESK_MAX_BACKENDS = 8`；
- `g_desk_count`：已注册项数；
- `g_desk_active`：当前索引，`-1` 表示空表。

`desk_backend_t` 字段（`desk_api.h`）：

| 字段 | 语义 |
|---|---|
| `name` | 注册名，`desk_select()` 按它逐字节比较 |
| `display_name` | 展示名，注册表不解析，仅给上层列举用 |
| `init(s)` | 启动期调一次；返回非 0 视为 init 失败 |
| `frame(s)` | 每帧取帧；返回值透传给 `desk_tick()` 调用方 |
| `event(s, ev)` | 输入事件；返回 0 = 已消费，非 0 = 本实现不处理 |
| `fini(s)` | 拆解回调；**当前没有任何调用路径**（见 7.6） |

表内只存后端指针，不拷贝结构体。两个内置后端都是文件级静态对象；新后端实例
必须具备与内核一致的生命周期，注册表不负责其内存管理。

### 7.3 注册 / 反注册的前置条件（实测）

`desk_register(d)` 返回码：

| 条件 | 返回 |
|---|---|
| `d == NULL` 或 `d->name == NULL` | -1 |
| 表满（`count ≥ 8`） | -1 |
| `name` 与已有项重名（逐字节比较，无大小写折叠、无长度截断处理） | -2 |
| 成功 | 0 |

- 第一个注册的项自动成为默认 active（`g_desk_active = 0`），不需要额外指定。
- **全仓不存在反注册接口**：grep 不到 `desk_unregister` / `desk_remove`。后端一旦
  注册，生命周期与内核一致。
- `desk_register_hpdesk()`（`desk_hpdesk.c`）幂等：表里已有同一指针时直接返回 0，
  不会重复塞两份。

### 7.4 `desk_start(scale_pct)`：返回值与失败语义（实测）

源码 `desk_runtime.c`，返回码与源码注释一致：

| 返回 | 语义 |
|---|---|
| 0 | 成功；若已在运行态（`g_running` 非 0），直接返回 0，不重复 init |
| -1 | 表面不可用：`desk_surface_get()` 拿不到帧缓冲 / 分辨率（fb 为 0 或宽高 bpp 未知） |
| -2 | 注册表里没有桌面（自动补注册内置 hpdesk 之后仍为空） |
| -3 | 桌面自己的 `init` 返回非 0；运行态先回滚（`g_running = 0`） |

启动顺序（实测）：

1. 空表先补注册内置 hpdesk；编译宏 `HEPU_DESK_DEMO` 下再注册 demo 并 `select("demo")`；
2. `desk_surface_get()` 填表面（fb / 宽高 / bpp / pitch / `scale_pct` + 全部图元指针）；
3. 归零上一个桌面留下的裁剪区与离屏目标（`target_end` + `clip_disable`）；
4. **先置运行态再调 `init`**：后端在 init 里取表面画第一帧时，
   `desk_active_surface()` 已非空；init 失败再回滚运行态。

`scale_pct` 写入 `surface.scale_pct`。对当前唯一后端 hpdesk 它只是提示：hpdesk 另有
自己的 `g_ui_scale_pct`，64 位经 `desk_scale_hint()` 读回传给 `desk_start`；32 位
调用点传字面量 `100`。

### 7.5 `desk_tick()`：表面所有权与调用时机（实测）

- 表面是运行时层的单个静态 `g_surf`，全系统一份；`desk_tick()` 把它交给当前后端的
  `frame`。`g_running` / `g_surf` **没有锁保护**。
- 返回值：未启动 → -1；当前后端无 `frame` → -1；否则透传 `frame` 返回值
  （hpdesk 包装恒返回 0）。

实际调用点（实测）：

| 侧 | 调用点 | 时机 / 节流 |
|---|---|---|
| 64 位 | `desktop64_run()` 主循环 | 约 16ms 节流；取帧前先 `hpdesk_deskstate_drain()` 消费跨核标脏请求 |
| 32 位 | `init_sequence.c` 启动期 | 只调一次 `desk_tick()` 量首帧周期，不持续驱动 |
| 32 位 | `kernel/sched/shell.c` 主循环 | 经 `desk_tick_if_due()` 驱动，主路径 |
| 32 位 | `keyboard.c` 等待循环 + 读线循环 | 经 `desk_tick_if_due()` 驱动，补充路径 |

- `desk_tick_if_due()` 是计数节流（每 8 次调用画一帧），shell 与键盘两个 32 位入口
  共用同一个静态计数器，不会画出双倍帧；源码注释记录了 32 位 PIT tick 不投递、故
  弃用 PIT 间隔改计数节流的实测原因。
- **谁可以调（实测 + 推断）**：现有调用全部在内核主上下文（shell / 键盘 / 64 位主
  循环）。代码中没有并发占用表面的防护；**推断**表面所有权属于"当前主循环上下文"，
  中断处理程序与并行核不应直接调 `desk_tick()`，跨核只能经标脏请求由主循环在下一帧
  消费。
- `desk_dispatch(ev)` 与 `desk_is_running()` 已实现但当前**无外部调用方**（实测 grep
  为 0）；`desk_active_surface()` 仅 hpdesk 包装层懒加载取表面时使用。

### 7.6 桌面切换时注册表如何表现（实测）

- `desk_select(name)` 只改 `g_desk_active` 索引：找不到返回 -1，找到返回 0。它**不动
  硬件状态、不调旧后端 `fini`、不调新后端 `init`**，也不会重填表面。
- 运行中切换的完整路径当前并不存在：`desk_start` 在 `g_running` 之后直接返回 0，
  没有 stop / re-init 序列；`fini` 回调虽写在 `desk_backend_t` 里，但全仓没有调用点。
  即"可切换"目前成立在**启动前选表**这一层；运行时热切换是预留设计，未实现。
- 7.4 第 3 步启动期归零 clip / 离屏目标，就是为切换准备的防护：上一家停在
  `target_begin` 里时，下一家的矩形会被夹进旧缓冲坐标系（源码注释记录了实测的半屏
  黑屏现象）。

### 7.7 前台所有权：两套不同的 owner，不要混（实测）

仓库里有两个名字相近、维度不同的 owner：

**(a) 帧缓冲前台 owner（pid 维度，runtime 层）** — `desk_runtime.c`

- `g_fb_owner_pid`，`-1` 表示前台在内核手里（常态）。
- `desk_fb_owner_acquire(pid)`：`pid < 0` → -1；已有 owner → -1（冲突**不排队**）；
  否则登记返回 0。
- `desk_fb_owner_release(pid)`：`pid < 0` 或 pid 等于当前 owner 才回收；他人 release
  不生效。
- 32 位经 syscall `sys_fb_acquire` / `sys_fb_release`（`sys_fb.c`）接入：先
  `MAP_PHYS` 映射显存，再交提交权；进程退出由 `sys_process.c` 强制回收，不依赖用户态
  主动 release。
- hpdesk 的两条提交路径（`hs_present` / `hs_present_region`）见到 owner ≥ 0 直接
  返回，内核不再提交像素，避免两个写者。

**(b) hpdesk 桌面状态 owner（slot 维度，64 位 hpdesk 内部）** — `hpdesk.c`

- `hpdesk_claim_owner()` 在 `desktop64_run()` 进主循环前调用，把当前进程槽位钉为
  owner；`hpdesk_is_owner()` / `hpdesk_owner_guard()` 据此放行或拒绝 z-order /
  焦点 / 脏标记类改动；启动期认领之前放行。
- 这套是 hpdesk 在 64 位多核下的**准入守卫**，与 (a) 的前台提交权无关，也不在 shared
  注册表契约内。

### 7.8 当前只是 hpdesk 单实现的假设，不能当通用契约（实测标注）

以下条目来自当前唯一内置后端 hpdesk 的实况，换成别的后端即可能不成立：

1. **输入自取**：hpdesk 在 `frame` 内自己轮询鼠标 / 键盘；包装层 `event` 恒返回 1
   （不消费事件）。通用契约要求 `event` 真正处理事件并返回 0 表示已消费——这条目前
   没有第二个样例验证。
2. **无拆解路径**：hpdesk `fini` 为空，桌面在系统生命周期内常驻；注册表也不提供
   反注册。
3. **gfx64_* 同形同签名假设**：表面层 `desk_surface.c` 直接接 `gfx64_*` /
   `font64_*`，32 位靠 `gfx64_compat*.c` 凑同名实现。换后端或换图元集时，这层假设
   需要重新核对。
4. **`desk_scale_hint()` 由 hpdesk 导出**（`hpdesk.c`，读私有全局
   `g_ui_scale_pct`），不是 runtime 提供的接口；runtime 头文件里没有该声明。
5. **默认后端是 hpdesk**：空表时 `desk_start` 自动补注册 hpdesk；demo 后端只在
   `HEPU_DESK_DEMO` 编译宏下注册并被选中，默认构建不启用。
6. **事件 / 节流时机绑定现有主循环**：32 位计数节流、64 位 16ms 节流都是各自调用方
   的时间源，runtime 层不做节流；新接入点要自己决定何时调 `desk_tick()`。

### 7.9 与既有文档的差异标注（实测核对）

- §三写"重名注册返回 -2"，与代码一致；但代码里 NULL 指针 / 表满也是 -1，旧文未列。
- §五写"64 位经注册表起桌面：`desktop64_init` 走同一条 `desk_start` 路径"。实测
  `desk_start` 在 `desktop64_run()` 内（`desktop64.c`），`desktop64_init()` 并不调
  它；实际链路是 shell64 → `desktop64_init()` → `desktop64_run()` → `desk_start`。
- §三规则 3"切换只改索引，不动任何硬件状态"与代码一致；需补充：运行中切换目前没有
  fini / init 序列（见 7.6）。
