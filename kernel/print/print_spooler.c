/* ============================================================================
 * 赫普OS - 打印子系统实现
 * V8.67: 初始实现
 * 打印队列管理、作业管理、页面渲染
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/print.h"
#include "../include/vga.h"
#include "../include/kheap.h"

/* 外部打印机驱动 */
extern int usb_printer_get_count(void);
extern void *usb_printer_get(int index);
extern int usb_printer_write(void *printer, const void *data, int length);
extern int usb_printer_print_test_page(void *printer);

/* 打印队列大小 */
#define PRINT_MAX_JOBS 32
#define PRINT_MAX_PRINTERS 8

/* 全局状态 */
static print_job_t print_jobs[PRINT_MAX_JOBS];
static int print_job_count = 0;
static uint32_t print_next_job_id = 1;
static int print_default_printer = 0;
static uint8_t print_initialized = 0;

/* 统计 */
static uint32_t stat_total_jobs = 0;
static uint32_t stat_completed = 0;
static uint32_t stat_failed = 0;

/* 初始化打印子系统 */
int print_init(void)
{
    if (print_initialized) return 0;

    memset(print_jobs, 0, sizeof(print_jobs));
    print_job_count = 0;
    print_next_job_id = 1;
    print_default_printer = 0;
    stat_total_jobs = 0;
    stat_completed = 0;
    stat_failed = 0;

    print_initialized = 1;
    vga_puts("[PRINT] Print spooler initialized\n");
    return 0;
}

/* 提交打印作业 */
int print_submit_job(const char *title, const char *user,
                     const void *data, uint32_t data_size,
                     print_page_size_t page_size,
                     print_orientation_t orientation,
                     int copies)
{
    if (!print_initialized || !data || data_size == 0) return -1;
    if (print_job_count >= PRINT_MAX_JOBS) return -2;

    /* 查找空闲槽位 */
    int slot = -1;
    for (int i = 0; i < PRINT_MAX_JOBS; i++) {
        if (print_jobs[i].state == PRINT_JOB_COMPLETED ||
            print_jobs[i].state == PRINT_JOB_FAILED ||
            print_jobs[i].state == PRINT_JOB_CANCELLED ||
            (print_jobs[i].job_id == 0 && print_jobs[i].state == 0)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -2;

    print_job_t *job = &print_jobs[slot];
    memset(job, 0, sizeof(print_job_t));

    job->job_id = print_next_job_id++;
    if (title) {
        strncpy(job->title, title, sizeof(job->title) - 1);
        job->title[sizeof(job->title) - 1] = 0;
    }
    if (user) {
        strncpy(job->user, user, sizeof(job->user) - 1);
        job->user[sizeof(job->user) - 1] = 0;
    }
    job->page_size = page_size;
    job->orientation = orientation;
    job->copies = (copies > 0) ? copies : 1;
    job->total_pages = 1;  /* 简化：默认1页 */
    job->printed_pages = 0;
    job->state = PRINT_JOB_QUEUED;
    job->submit_time = 0;  /* 简化：应获取系统时间 */
    job->data_size = data_size;

    /* 复制打印数据 */
    job->data = kmalloc(data_size);
    if (!job->data) return -3;
    memcpy(job->data, data, data_size);

    print_job_count++;
    stat_total_jobs++;

    vga_printf("[PRINT] Job %d queued: %s (%d bytes)\n",
               job->job_id, job->title, data_size);
    return job->job_id;
}

/* 取消打印作业 */
int print_cancel_job(uint32_t job_id)
{
    if (!print_initialized) return -1;

    for (int i = 0; i < PRINT_MAX_JOBS; i++) {
        if (print_jobs[i].job_id == job_id) {
            if (print_jobs[i].state == PRINT_JOB_QUEUED) {
                print_jobs[i].state = PRINT_JOB_CANCELLED;
                if (print_jobs[i].data) {
                    kfree(print_jobs[i].data);
                    print_jobs[i].data = NULL;
                }
                print_job_count--;
                vga_printf("[PRINT] Job %d cancelled\n", job_id);
                return 0;
            }
            return -2;  /* 正在打印，无法取消 */
        }
    }
    return -3;  /* 未找到 */
}

/* 获取作业状态 */
int print_get_job_state(uint32_t job_id, print_job_state_t *state)
{
    if (!print_initialized || !state) return -1;

    for (int i = 0; i < PRINT_MAX_JOBS; i++) {
        if (print_jobs[i].job_id == job_id) {
            *state = print_jobs[i].state;
            return 0;
        }
    }
    return -2;
}

/* 获取作业列表 */
int print_get_jobs(print_job_t *jobs, int max_count)
{
    if (!print_initialized || !jobs || max_count <= 0) return 0;

    int count = 0;
    for (int i = 0; i < PRINT_MAX_JOBS && count < max_count; i++) {
        if (print_jobs[i].job_id != 0 &&
            print_jobs[i].state != PRINT_JOB_CANCELLED) {
            jobs[count++] = print_jobs[i];
        }
    }
    return count;
}

/* 获取打印机列表 */
int print_get_printers(print_printer_info_t *printers, int max_count)
{
    if (!print_initialized || !printers || max_count <= 0) return 0;

    int count = usb_printer_get_count();
    if (count > max_count) count = max_count;

    for (int i = 0; i < count; i++) {
        void *printer = usb_printer_get(i);
        if (!printer) continue;

        /* 简化：从打印机结构提取信息 */
        printers[i].index = i;
        snprintf(printers[i].name, sizeof(printers[i].name),
                 "USB Printer %d", i);
        printers[i].online = 1;
        printers[i].paper_empty = 0;
        printers[i].pages_printed = 0;
    }
    return count;
}

/* 获取默认打印机 */
int print_get_default_printer(void)
{
    return print_default_printer;
}

/* 设置默认打印机 */
int print_set_default_printer(int index)
{
    if (index < 0 || index >= PRINT_MAX_PRINTERS) return -1;
    print_default_printer = index;
    return 0;
}

/* 打印队列统计 */
void print_stats(uint32_t *total_jobs, uint32_t *queued,
                 uint32_t *printing, uint32_t *completed)
{
    uint32_t q = 0, p = 0, c = 0;
    for (int i = 0; i < PRINT_MAX_JOBS; i++) {
        if (print_jobs[i].job_id == 0) continue;
        if (print_jobs[i].state == PRINT_JOB_QUEUED) q++;
        else if (print_jobs[i].state == PRINT_JOB_PRINTING) p++;
        else if (print_jobs[i].state == PRINT_JOB_COMPLETED) c++;
    }
    if (total_jobs) *total_jobs = stat_total_jobs;
    if (queued) *queued = q;
    if (printing) *printing = p;
    if (completed) *completed = stat_completed;
}

/* 清空已完成的作业 */
int print_clear_completed(void)
{
    int cleared = 0;
    for (int i = 0; i < PRINT_MAX_JOBS; i++) {
        if (print_jobs[i].state == PRINT_JOB_COMPLETED ||
            print_jobs[i].state == PRINT_JOB_FAILED ||
            print_jobs[i].state == PRINT_JOB_CANCELLED) {
            if (print_jobs[i].data) {
                kfree(print_jobs[i].data);
                print_jobs[i].data = NULL;
            }
            memset(&print_jobs[i], 0, sizeof(print_job_t));
            cleared++;
        }
    }
    return cleared;
}

/* 处理一个打印作业 */
static int process_job(print_job_t *job)
{
    if (!job || !job->data) return -1;

    /* 获取默认打印机 */
    void *printer = usb_printer_get(print_default_printer);
    if (!printer) {
        vga_printf("[PRINT] No printer available for job %d\n", job->job_id);
        return -1;
    }

    job->state = PRINT_JOB_PRINTING;

    /* 发送打印数据 */
    int ret = usb_printer_write(printer, job->data, job->data_size);
    if (ret < 0) {
        job->state = PRINT_JOB_FAILED;
        stat_failed++;
        vga_printf("[PRINT] Job %d failed: write error\n", job->job_id);
        return -1;
    }

    /* 发送换页符 */
    const char form_feed[] = "\x0C";
    usb_printer_write(printer, form_feed, 1);

    job->printed_pages = job->total_pages;
    job->state = PRINT_JOB_COMPLETED;
    stat_completed++;
    print_job_count--;

    vga_printf("[PRINT] Job %d completed: %s\n", job->job_id, job->title);

    /* 释放数据 */
    if (job->data) {
        kfree(job->data);
        job->data = NULL;
    }

    return 0;
}

/* 打印队列轮询（系统主循环调用） */
void print_spooler_poll(void)
{
    if (!print_initialized) return;

    /* 查找第一个排队的作业 */
    for (int i = 0; i < PRINT_MAX_JOBS; i++) {
        if (print_jobs[i].state == PRINT_JOB_QUEUED) {
            process_job(&print_jobs[i]);
            break;  /* 每次只处理一个作业 */
        }
    }
}

/* 渲染文本页面 */
int print_render_text_page(const char *text, int text_len,
                           void *out_buf, int out_buf_size,
                           const char *command_set)
{
    if (!text || !out_buf || out_buf_size <= 0) return -1;

    uint8_t *out = (uint8_t *)out_buf;
    int out_len = 0;

    /* 简化：根据命令集添加初始化命令 */
    if (command_set && strstr(command_set, "ESC/P")) {
        /* ESC/POS初始化 */
        if (out_len + 3 > out_buf_size) return -2;
        out[out_len++] = 0x1B;  /* ESC */
        out[out_len++] = '@';   /* @ 初始化 */
    } else if (command_set && strstr(command_set, "PCL")) {
        /* PCL初始化 */
        const char pcl_init[] = "\x1B" "E";  /* PCL Reset */
        int init_len = sizeof(pcl_init) - 1;
        if (out_len + init_len > out_buf_size) return -2;
        memcpy(out + out_len, pcl_init, init_len);
        out_len += init_len;
    }

    /* 复制文本内容 */
    int copy_len = text_len;
    if (out_len + copy_len > out_buf_size) {
        copy_len = out_buf_size - out_len;
    }
    memcpy(out + out_len, text, copy_len);
    out_len += copy_len;

    return out_len;
}
