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
| 32 位内核内桌面 | ⛔ **已下线**（`init_sequence.c` 不再进 `elastic_desktop_main_loop`） |
| 32 位用户态桌面 | 🚧 进行中：先要 `SYS_FB_DESC` + `SYS_MMAP` 拿得到 LFB |
| 64 位桌面接同一接口 | ⏳ 未做（现状仍是 `hpdesk.c` 直写） |
| 默认桌面 `desk_shell` | ⏳ 未做 |

## 六、与其它文档的关系

- `GRAPHICS_BOUNDARY.md`：三层模型（共享算法 / 32 位实现 / 64 位实现）。
  本文档在其之上加了一层"桌面"—— 桌面属于**共享层**，因为它只需要 surface。
- `KERNEL_SCOPE.md` §5-4：framebuffer 契约（`SYS_FB_DESC`）是本文档的第一块地基。
- `UNIFIED_SOURCE_TREE.md`：渐进共享。桌面先做成接口，再逐个架构接上。
