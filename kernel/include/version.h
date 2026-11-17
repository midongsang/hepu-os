/* 赫普OS 系统版本号 —— 32/64 位共用的唯一宏。
 *
 * 版本号规则见 docs/status/VERSIONING.md：重要版本.默认版本.bug版本。
 * 这个文件是"代码侧的单一来源"：任何界面/终端/任务管理器/引导菜单要显示
 * 版本号，都必须引用 HEPU_OS_VERSION，不要再各写一份字面量 —— 历史上就是
 * 因为多处硬编码，README 停在 0.59.0、引导菜单停在 0.23，而代码里已经用到
 * 0.60.6，三处对不上。
 *
 * 发布时只需改这一行 + docs/status/VERSIONING.md 的 frontmatter。 */
#ifndef HEPU_VERSION_H
#define HEPU_VERSION_H

#define HEPU_OS_VERSION      "0.60.7"
#define HEPU_OS_VERSION_MAJOR 0
#define HEPU_OS_VERSION_MINOR 60
#define HEPU_OS_VERSION_BUG   7

#endif /* HEPU_VERSION_H */
