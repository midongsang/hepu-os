#ifndef HEPU_KLOG_H
#define HEPU_KLOG_H

#include <stdint.h>
#include <stdarg.h>

/* ============================================================================
 * 赫普OS - 内核诊断日志 (V2.30)
 * 把启动/运行期诊断记录到内存环形缓冲区, 可随时用 shell `dmesg` 查看.
 * 解决"诊断闪一下就黑屏看不到"——日志不依赖屏幕停留, 落内存即可回溯.
 * ========================================================================== */

#define KLOG_BUF_SIZE 8192

/* 日志级别 (V3.87-E14) */
#define KLOG_ERR   0   /* 致命/错误: 必须可见 */
#define KLOG_WARN  1   /* 警告: 资源紧张/降级 */
#define KLOG_INFO  2   /* 信息: 启动/驱动正常事件 */
#define KLOG_DEBUG 3   /* 调试: 详细跟踪(默认过滤) */

/* 记录一条日志(带时间戳前缀由内部处理), 格式同 snprintf */
void klog_printf(const char *fmt, ...);

/* V3.87-E14: 分级日志——记录时附加级别标记, 低于全局阈值直接丢弃 */
void klog_printf_lvl(int level, const char *fmt, ...);

/* V3.87-E14: 设置全局日志阈值(低于该级别的不记录; 默认 KLOG_INFO) */
void klog_set_level(int level);

/* V3.87-E14: 读取当前阈值 */
int klog_get_level(void);

/* V3.87-E14: 按级别过滤显示——只打印 level >= min_level 的日志 */
void klog_dump_level(int min_level);

/* 记录纯字符串 */
void klog_puts(const char *s);

/* 捕获引导器写入 VGA 文本显存(0xB8000)的引导诊断, 追加到日志.
 * 引导器在实模式直写显存, 内核此函数在清屏前调用, 把前 N 行读回. */
void klog_capture_boot(void);

/* 打印全部日志到屏幕 (shell `dmesg`) */
void klog_dump(void);

/* V3.78: 诊断日志落盘——环形缓冲写入 /hepu_boot.log (ext2 磁盘) */
void klog_save_to_disk(void);

/* 日志当前字节数 */
uint32_t klog_len(void);
uint32_t klog_snapshot(char *out, uint32_t max);

#endif
