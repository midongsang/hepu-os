#ifndef HEPU_TYPES_H
#define HEPU_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * 赫普OS 统一类型定义
 * 所有模块应优先使用此处定义的类型，避免重复定义和类型冲突
 * ========================================================================== */

/* 基础整数类型 */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* 布尔类型 */
typedef uint8_t bool;
#define true  1
#define false 0

/* 状态码 */
typedef int32_t status_t;
#define HEPU_OK          0
#define HEPU_ERROR      -1
#define HEPU_ENOMEM     -2
#define HEPU_EINVAL     -3
#define HEPU_ENOENT     -4
#define HEPU_EEXIST     -5
#define HEPU_EBUSY      -6
#define HEPU_EPERM      -7
#define HEPU_EIO        -8
#define HEPU_ENOSPC     -9
#define HEPU_ETIMEDOUT  -10
#define HEPU_EAGAIN     -11

/* 常用大小常量 */
#define KiB 1024
#define MiB (1024 * 1024)
#define GiB (1024 * 1024 * 1024)

/* 页大小 */
#define PAGE_SIZE       4096
#define PAGE_MASK       (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(x)   (((x) + PAGE_SIZE - 1) & PAGE_MASK)

/* 扇区大小 */
#define SECTOR_SIZE     512

/* 最大路径长度 */
#define MAX_PATH_LEN    256

/* 最大文件名长度 */
#define MAX_NAME_LEN    64

/* 进程ID */
typedef int32_t pid_t;

/* 文件描述符 */
typedef int32_t fd_t;

/* 时间戳（毫秒） */
typedef uint64_t timestamp_ms_t;

/* 物理地址 */
typedef uint32_t phys_addr_t;

/* 虚拟地址 */
typedef uint32_t virt_addr_t;

/* 中断号 */
typedef uint32_t irq_t;

/* 设备ID */
typedef uint32_t device_id_t;

/* 颜色（RGBA） */
typedef uint32_t color_t;
#define COLOR_RGBA(r,g,b,a) (((r)<<24)|((g)<<16)|((b)<<8)|(a))
#define COLOR_RGB(r,g,b)    (((r)<<16)|((g)<<8)|(b))

/* 矩形 */
typedef struct {
    int32_t x, y;
    int32_t width, height;
} rect_t;

/* 点 */
typedef struct {
    int32_t x, y;
} point_t;

/* 二维向量 */
typedef struct {
    float x, y;
} vec2_t;

/* 三维向量 */
typedef struct {
    float x, y, z;
} vec3_t;

/* 缓冲区描述符 */
typedef struct {
    void    *data;
    uint32_t size;
    uint32_t capacity;
} buffer_t;

/* 链表节点 */
typedef struct list_node {
    struct list_node *next;
    struct list_node *prev;
    void             *data;
} list_node_t;

/* 链表头 */
typedef struct {
    list_node_t *head;
    list_node_t *tail;
    uint32_t     count;
} list_t;

#endif /* HEPU_TYPES_H */
