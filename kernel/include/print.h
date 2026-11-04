/* ============================================================================
 * 赫普OS - 打印子系统
 * V8.67: 初始实现
 * 打印队列管理、作业管理、页面渲染
 * ========================================================================== */

#ifndef _PRINT_H
#define _PRINT_H

#include <stdint.h>

/* 打印作业状态 */
typedef enum {
    PRINT_JOB_QUEUED = 0,
    PRINT_JOB_PRINTING,
    PRINT_JOB_COMPLETED,
    PRINT_JOB_FAILED,
    PRINT_JOB_CANCELLED
} print_job_state_t;

/* 页面格式 */
typedef enum {
    PRINT_PAGE_A4 = 0,
    PRINT_PAGE_LETTER,
    PRINT_PAGE_LEGAL
} print_page_size_t;

/* 打印方向 */
typedef enum {
    PRINT_ORIENT_PORTRAIT = 0,
    PRINT_ORIENT_LANDSCAPE
} print_orientation_t;

/* 打印作业 */
typedef struct {
    uint32_t job_id;
    char     title[64];
    char     user[32];
    print_page_size_t page_size;
    print_orientation_t orientation;
    int      copies;
    int      total_pages;
    int      printed_pages;
    print_job_state_t state;
    uint32_t submit_time;
    uint32_t data_size;
    void    *data;  /* 打印数据（简化版：直接存储） */
} print_job_t;

/* 打印机信息 */
typedef struct {
    int      index;
    char     name[64];
    char     manufacturer[64];
    char     model[64];
    char     command_set[32];
    int      online;
    int      paper_empty;
    uint32_t pages_printed;
} print_printer_info_t;

/* 初始化打印子系统 */
int print_init(void);

/* 提交打印作业 */
int print_submit_job(const char *title, const char *user,
                     const void *data, uint32_t data_size,
                     print_page_size_t page_size,
                     print_orientation_t orientation,
                     int copies);

/* 取消打印作业 */
int print_cancel_job(uint32_t job_id);

/* 获取作业状态 */
int print_get_job_state(uint32_t job_id, print_job_state_t *state);

/* 获取作业列表 */
int print_get_jobs(print_job_t *jobs, int max_count);

/* 获取打印机列表 */
int print_get_printers(print_printer_info_t *printers, int max_count);

/* 获取默认打印机 */
int print_get_default_printer(void);

/* 设置默认打印机 */
int print_set_default_printer(int index);

/* 打印队列统计 */
void print_stats(uint32_t *total_jobs, uint32_t *queued,
                 uint32_t *printing, uint32_t *completed);

/* 清空已完成的作业 */
int print_clear_completed(void);

/* 打印队列轮询（系统主循环调用） */
void print_spooler_poll(void);

/* 渲染文本页面为ESC/POS或PCL格式 */
int print_render_text_page(const char *text, int text_len,
                           void *out_buf, int out_buf_size,
                           const char *command_set);

#endif /* _PRINT_H */
