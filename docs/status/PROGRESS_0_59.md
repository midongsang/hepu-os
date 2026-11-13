---
title: PROGRESS_0_59
status: active
sys_version: 0.59.0
updated: 2026-11-13
owner: status
source_of_truth: null
supersedes: null
---

# 0.59.0 桌面视觉与交互收口（2026-11-13）

本版不动内核能力，全部落在桌面这一层：任务管理器重做成两页表格、四条实时曲线补满宽、
全局滚动条补齐、窗口投影从"三层色带"换成真的卷积阴影、新增第二套内置光标并可切换。
期间挖出三个把前面几轮工作全部作废的工程性问题，单独记在 FIXES §1.121。

## 一、任务管理器

| 项 | 之前 | 现在 |
|---|---|---|
| 页面结构 | 摘要 + 进程表 + 设备状态挤在一页 | `Processes` / `Devices` 两页标签栏，标签固定顶部不随内容滚动 |
| 进程表 | `PID NAME CPU MEM STATE` 五列，无分隔 | 八列 `PID NAME CPU MEM PRIO CORE TIME STATE`，七条列竖线（12% 混白太淡，改 18%） |
| 设备表 | 混在进程表下方的一串文字 | 独立页 `DRIVER / STATE` 两列 + 竖线，9 个驱动 |
| 曲线 | 2px 柱 + 3px 步进，看着像"看不见" | 连续面积图：2px 步进无缝、半透明填充 + 顶部 1px 亮线 |
| 历史深度 | `M_HIST 72`（144px 铺不满 470px 图形区） | `M_HIST 288`，首次打开预填当前值 |
| 视图宽度 | 铺满整行 | 收窄到 `cw - MC(96) - MC(88)`，四条视图同宽同高左端对齐 |
| 滚动 | 全系统没有通用滚动条 | `hs_scrollbar_draw()`：thumb 按 `视口/(视口+可滚量)` 折算、最小 16px；进程表加裁剪层 |

顶部改成单行规格串 `Hepu OS v0.59.0 | 64-bit SMP | 2 cores | 979 MB | DSK 9%`，
内存占用也做成时间曲线视图，数字贴在视图旁 —— 之前"数字和曲线割裂"就是三行文字摘要与
图形区各说各话。

## 二、窗口投影（四轮 + 一次事故）

1. **三层色带**（3 层不透明圆角矩形、每层 4px、相邻差 9%）→ 硬边台阶；
2. **9 层 2px 细渐变**，基色取窗口下缘 12px 处的真实像素 → 仍然"假"；
3. **真 alpha 混合**：只遍历窗口外侧环，按到边界的欧氏距离平方衰减；
4. **高斯 mask**：把实心圆角矩形当遮挡体做 3 遍盒式模糊（中心极限定理逼近高斯），
   按窗口尺寸缓存 mask。

第 3、4 轮实际上**从未进入镜像**：`hpdesk.c` 里写 `extern int gfx_width, gfx_height`，
而这两个在 `gfx64.c` 里是宏（`#define gfx_width (gfx_fb_info.width)`），没有符号，
链接报 `undefined reference`；而验证命令 `make ... | grep -E ' error'` 匹配不到这个词，
每次都被判"编译通过"。详见 FIXES §1.121。

真正生效后还有三处要修：

- `hs_s()` 依赖 `g_ui_scale_pct`，而窗口在 `hpdesk_init` 早期创建、缩放还没读出来，
  半径算成 1px，又被尺寸缓存永久固化 → 参数全部保底 + 半径/圆角/pad 并入缓存指纹；
- `pad` 写死 60 而 3 遍模糊支撑 108，边带被切出硬边 → `pad = 3r + 6`；
- 阴影源下移 14px 模拟光源在上方，blit 原点和跳过判定却没对齐，
  窗口正下方 14px 既不入阴影又被当窗口内部跳过 → 一条亮缝（FIXES §1.122）。

现在参数按窗口短边自适应：`rc ≤ mn/8`、`r = mn/18`（10..40）、`pad = 3r + 6`、
`off = rc/2 + 4`（≤18）。

## 三、光标

- 新增内置第二套 **Modern**：17 张 128px 线稿裁包围盒缩到 48px，
  每像素只存 `alpha(8bit) + 1bit 前景/描边`，每图两组色常量 —— 17 个形态共 34.7KB；
- 原素材包 `*.xcur`（24px）保留为 Classic；
- 热点按形态定：箭头 `(0,0)`、文本/move/cross 取中心、上箭头取底边中点、hand 取指尖；
- 跟随 UI 缩放档最近邻放大（48 → 60/72/96），用最近邻是为了保住 1px 描边不糊；
- Display 页 `Cursor Theme: Classic/Modern [click to toggle]` 即时切换并持久化。

## 四、Display 页开关与配置

```
scale=150
resolution=1920x1080
shadow=1
cursor=1
```

`hs_display_load()` 原来解析到 `scale=` 就 `return 1`，后面根本不看；改为扫完整行，
`shadow=` / `cursor=` 一并读回。

## 五、工程性修复

- **版本号单点化**：`Hepu OS v0.58.0` 在 `hpdesk.c`/`hs_apps.c` 硬编码 4 处，
  改为 `hpdesk.h` 的 `HEPU_OS_VERSION` 宏，界面、终端、任务管理器统一引用；
- **编译判据**：不许再用 `grep ' error'` 判成败，只看 `make` 退出码 + `bin` 时间戳；
- Display 页 `Available:` 列表步长写成 `mi * SETK(2)`（缩放后 = 3），模式表每项只有 2 个
  uint32，结果"跳着读"，输出成 `480x800, 800x600`。

## 六、验证

单轮回归五判据 `desk=1 EXC=0 PASS=1 FAIL=0 SPINERR=0 timercal=0`，内核 872,656 B（上限 1MB），
启动到桌面 15s。每项改动都配 1920×1080 截图存 `docs/assets/`。

## 七、遗留

见 `OPEN_BUGS.md` 的 OBS-02 / OBS-03 / OBS-04。
