/* ============================================================================
 * 赫普OS - Shell：测试/性能/资源命令
 * 从 shell.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：cmd_dmatest/cmd_cacheinfo/cmd_perf/heap_used_snapshot/cmd_locktest/
 *       cmd_hdpk/cmd_setappcpu/cmd_ipcstress/cmd_memtest/cmd_limits/cmd_rlimit/
 *       cmd_softirqtest/cmd_softstat
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "process.h"
#include "shell.h"
#include "serial.h"
#include "pit.h"
#include "hepu.h"
#include "i18n.h"

void cmd_dmatest(void)
{
    extern int ide_dma_is_available(void);
    extern int ide_batch_read(uint32_t, uint8_t *, uint32_t);
    extern int ide_batch_write(uint32_t, const uint8_t *, uint32_t);
    static uint8_t buf_pio[64 * 512] __attribute__((aligned(4)));
    static uint8_t buf_dma[64 * 512] __attribute__((aligned(4)));
    static uint8_t backup[512];
    uint32_t t0, t1, i;
    int r, mismatch = 0;

    vga_puts("== IDE DMA batch test ==\n");
    if (!ide_dma_is_available()) {
        vga_puts(" DMA unavailable, PIO fallback only\n");
        return;
    }
    /* 0) 读 2 扇区: 小传输 DMA 一致性(定位单次传输上限) */
    {
        static uint8_t b2p[1024], b2d[1024];
        int j, mm = 0;
        r = ide_read_sectors(100, b2p, 2);
        r = ide_batch_read(100, b2d, 2);
        for (j = 0; j < 1024; j++) if (b2p[j] != b2d[j]) mm++;
        serial_puts(0x3F8, "[DMATEST] 2sect ret=");
        serial_puthex(0x3F8, r);
        serial_puts(0x3F8, " mm=");
        serial_puthex(0x3F8, mm);
        serial_puts(0x3F8, "\n");
        vga_printf(" 2-sector DMA: %s (mm=%d)\n", mm ? "FAIL" : "OK", mm);
    }
    /* 1) 读 64 扇区: PIO vs DMA 一致性 */
    r = ide_read_sectors(100, buf_pio, 64);
    if (r != 64) { vga_printf(" PIO read fail r=%d\n", r); return; }
    __asm__ volatile("rdtsc" : "=a"(t0));
    r = ide_batch_read(100, buf_dma, 64);
    __asm__ volatile("rdtsc" : "=a"(t1));
    serial_puts(0x3F8, "[DMATEST] batch_read ret=");
    serial_puthex(0x3F8, r);
    serial_puts(0x3F8, " t0=");
    serial_puthex(0x3F8, t0);
    serial_puts(0x3F8, " t1=");
    serial_puthex(0x3F8, t1);
    serial_puts(0x3F8, "\n");
    if (r != 64) { vga_printf(" DMA batch read fail r=%d (fallback?)\n", r); return; }
    for (i = 0; i < 64 * 512; i++) {
        if (buf_pio[i] != buf_dma[i]) { mismatch++; if (mismatch > 4) break; }
    }
    serial_puts(0x3F8, "[DMATEST] first mismatch at=");
    serial_puthex(0x3F8, i);
    serial_puts(0x3F8, " pio0..7=");
    for (i = 0; i < 8; i++) serial_puthex(0x3F8, buf_pio[i]);
    serial_puts(0x3F8, " dma0..7=");
    for (i = 0; i < 8; i++) serial_puthex(0x3F8, buf_dma[i]);
    serial_puts(0x3F8, "\n");
    vga_printf(" batch read 64 sectors: %s (mismatch=%d)\n",
               mismatch ? "FAIL" : "OK", mismatch);
    vga_printf(" PIO cyc=0x%x DMA cyc=0x%x (delta=%d)\n",
               t0, t1, (int)(t1 - t0));
    /* 2) DMA 写 1 扇区(备份后写回恢复) */
    r = ide_read_sectors(65000, backup, 1);
    if (r != 1) { vga_printf(" backup read fail r=%d\n", r); return; }
    memset(buf_dma, 0x5A, 512);
    r = ide_batch_write(65000, buf_dma, 1);
    if (r != 1) { vga_printf(" DMA write fail r=%d\n", r); return; }
    r = ide_batch_read(65000, buf_pio, 1);
    if (r != 1 || buf_pio[0] != 0x5A || buf_pio[511] != 0x5A) {
        vga_puts(" DMA write verify FAIL\n");
    } else {
        vga_puts(" DMA write+readback: OK\n");
    }
    r = ide_write_sectors(65000, backup, 1);   /* 恢复原数据 */
    vga_printf(" backup restored: %s\n", (r == 1) ? "OK" : "FAIL");
}

/* V4.00-E45: 缓存布局诊断——process_t 热簇/汇编依赖偏移.
 * 作用: 修改 process.h 后跑 cacheinfo, 立即核对汇编硬编码偏移
 * (kernel_stack@48/kernel_esp@52/page_dir@68) 是否仍成立. */
void cmd_cacheinfo(void)
{
    unsigned cl = 64;
    unsigned pdir = (unsigned)offsetof(process_t, page_dir);
    vga_printf("== cache layout: process_t ==\n");
    vga_printf(" sizeof=%u bytes (cacheline x%u: %u lines, aligned=%s)\n",
               (unsigned)sizeof(process_t), cl,
               ((unsigned)sizeof(process_t) + cl - 1) / cl,
               (sizeof(process_t) % cl == 0) ? "yes" : "no");
    vga_printf(" asm-deps: kernel_stack@%u kernel_esp@%u page_dir@%u\n",
               (unsigned)offsetof(process_t, kernel_stack),
               (unsigned)offsetof(process_t, kernel_esp), pdir);
    if (pdir != 68) {
        vga_printf(" !! page_dir 实际@%u, interrupt.S 硬编码 68 —— 该处为"
                   " memset 产生的 padding(=0), CR3 切换被跳过(侥幸稳定)."
                   " 修正需页目录审计(见 DESIGN V4.00-E45 待办).\n", pdir);
    }
    vga_printf(" hot-cluster(line 64-127): prio@%u slice@%u used@%u"
               " prem@%u sleep@%u ticks@%u blk@%u\n",
               (unsigned)offsetof(process_t, priority),
               (unsigned)offsetof(process_t, time_slice),
               (unsigned)offsetof(process_t, time_used),
               (unsigned)offsetof(process_t, preemptions),
               (unsigned)offsetof(process_t, sleep_until),
               (unsigned)offsetof(process_t, ticks_total),
               (unsigned)offsetof(process_t, blocked_ticks));
    vga_printf(" cold-cluster: compat@%u heap@%u mem@%u\n",
               (unsigned)offsetof(process_t, compat_abi),
               (unsigned)offsetof(process_t, heap_start),
               (unsigned)offsetof(process_t, mem_limit));
}

void cmd_perf(void)
{
    extern uint32_t sched_get_switches(void);
    extern uint32_t sched_get_ticks(void);
    extern uint32_t irq_count[16];
    extern void kheap_perf(uint32_t *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
    extern process_t proc_table[];

    uint32_t calls, frees, bytes, peak, fails;
    uint32_t used, free, total;
    uint32_t slabs, slab_alloc, slab_mem;
    kheap_perf(&calls, &frees, &bytes, &peak, &fails);
    kheap_stats(&used, &free, &total);
    extern void slab_stats(uint32_t *, uint32_t *, uint32_t *);
    slab_stats(&slabs, &slab_alloc, &slab_mem);

    extern void irq_lat_stats(uint32_t *, uint32_t *, uint32_t *);
    uint32_t ilmax, ilavg, iln;
    irq_lat_stats(&ilmax, &ilavg, &iln);

    vga_puts("======== HEPU OS Perf (E1/E2/E33) ========\n");
    /* V4.51-E96: idle/busy 占比 */
    extern uint32_t sched_get_idle_ticks(void);
    {
        uint32_t tot = sched_get_ticks();
        uint32_t idl = sched_get_idle_ticks();
        int idle_pct = tot ? (idl * 100 / tot) : 0;
        vga_printf("sched : ctx_switches=%d  ticks=%d  idle=%d%% busy=%d%%\n",
                   sched_get_switches(), tot, idle_pct, 100 - idle_pct);
    }
    vga_printf("irq0  : lat_max=%u avg=%u cycles n=%u\n",
               ilmax, ilavg, iln);
    vga_printf("kheap : used=%d free=%d total=%d\n", used, free, total);
    vga_printf("        calls=%d frees=%d bytes=%d peak=%d fails=%d\n",
               calls, frees, bytes, peak, fails);
    vga_printf("slab  : caches=6 slabs=%d allocs=%d mem=%d\n",
               slabs, slab_alloc, slab_mem);
    {
        extern void spin_stats(uint32_t *, uint32_t *);
        uint32_t sw, st;
        spin_stats(&sw, &st);
        vga_printf("spin  : waits=%d timeouts=%d\n", sw, st);
    }
    {
        /* V3.92-E38: 新 softirq_stats 5 参数(raised/handled/dropped/defer/pending) */
        extern void softirq_stats(uint32_t *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
        uint32_t sr = 0, sh = 0, sd = 0, sm = 0, sp = 0;
        softirq_stats(&sr, &sh, &sd, &sm, &sp);
        vga_printf("softirq: raised=%d handled=%d dropped=%d max_defer=%d pend=%d\n",
                   sr, sh, sd, sm, sp);
        /* V4.16-E61: 延迟直方图 (tick 桶) */
        extern void softirq_stats_hist(uint32_t *, uint32_t *);
        uint32_t hist[6], ssum = 0;
        softirq_stats_hist(hist, &ssum);
        uint32_t s_avg = (sh > 0) ? (ssum / sh) : 0;
        vga_printf("  defer hist: 0-1:%d 2-5:%d 6-10:%d 11-20:%d 21-50:%d >50:%d avg=%d\n",
                   hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], s_avg);
    }
    /* V3.95-E41: 堆碎片 + 上下文切换 + 系统 tick */
    {
        extern void kheap_frag_stats(uint32_t *, uint32_t *, uint32_t *);
        extern uint32_t sched_get_ticks(void);
        extern uint32_t sched_switch_count(void);
        uint32_t fb = 0, lf = 0, fr = 0;
        kheap_frag_stats(&fb, &lf, &fr);
        vga_printf("kheap : frag_rate=%u%% free_blocks=%d largest_free=%d\n",
                   fr, fb, lf);
        vga_printf("sched : switches=%d tick=%d\n",
                   sched_switch_count(), sched_get_ticks());
    }
    {
        extern void ipc_mutex_wait_stats(uint32_t *, uint32_t *);
        extern void ipc_sem_wait_stats(uint32_t *, uint32_t *);
        uint32_t mw, mt, sw, st;
        ipc_mutex_wait_stats(&mw, &mt);
        ipc_sem_wait_stats(&sw, &st);
        vga_printf("ipc    : mutex waits=%d timeouts=%d | sem waits=%d timeouts=%d\n",
                   mw, mt, sw, st);
    }

    vga_puts("proc  : (PID  name        state  ticks  slice  prio)\n");
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) continue;
        const char *st = "?";
        if (proc_table[i].state == PROC_READY) st = "READY ";
        else if (proc_table[i].state == PROC_RUNNING) st = "RUN   ";
        else if (proc_table[i].state == PROC_SLEEPING) st = "SLEEP ";
        else if (proc_table[i].state == PROC_BLOCKED) st = "BLOCK ";
        else if (proc_table[i].state == PROC_FROZEN) st = "FROZEN";
        else if (proc_table[i].state == PROC_ZOMBIE) st = "ZOMBIE";
        const char *pr = (proc_table[i].priority == 0) ? "RT  "
                       : (proc_table[i].priority == 2) ? "BKG " : "NORM";
        vga_printf("        %d  %-12s %s %6d %5d %s\n",
                   proc_table[i].pid, proc_table[i].name, st,
                   proc_table[i].ticks_total, proc_table[i].time_slice, pr);
    }

    vga_puts("irq   : ");
    int any = 0;
    for (int i = 0; i < 16; i++) {
        if (irq_count[i]) {
            vga_printf("IRQ%d=%d ", i, irq_count[i]);
            any = 1;
        }
    }
    if (!any) vga_puts("(no interrupts yet)");
    vga_puts("\n");
}

/* V3.87-E3: 堆已用快照(供 locktest) */
uint32_t heap_used_snapshot(void)
{
    extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
    uint32_t used = 0, f, t;
    kheap_stats(&used, &f, &t);
    return used;
}

/* V3.87-E3: 锁压力测试——中断并发下 2000 次 kmalloc/kfree, 验证堆锁无死锁 */
void cmd_locktest(void)
{
    extern void spin_stats(uint32_t *, uint32_t *);
    uint32_t w0, t0, w1, t1;
    spin_stats(&w0, &t0);
    vga_puts("[LOCKTEST] 1000x kmalloc(64)+kfree + kmalloc(128)+kfree\n");
    for (int i = 0; i < 1000; i++) {
        void *p = kmalloc(64);
        if (p) kfree(p);
        void *q = kmalloc(128);
        if (q) kfree(q);
    }
    spin_stats(&w1, &t1);
    vga_printf("[LOCKTEST] done: waits=%d timeouts=%d (heap used=%d)\n",
               w1 - w0, t1 - t0, heap_used_snapshot());
}

/* V3.87-E15: H-DPK 社区模板/机型查看(小缓冲摘要 API——不占用大栈) */
void cmd_hdpk(void)
{
    extern int hdpk_community_stats(int *, int *, uint32_t *);
    extern int hdpk_template_summary(int, char *, int, char *, int, int *, int *);
    extern int hdpk_device_summary(int, char *, int, char *, int, int *, uint32_t *);
    int nt = 0, nd = 0;
    uint32_t contribs = 0;
    hdpk_community_stats(&nt, &nd, &contribs);
    vga_printf(" H-DPK community: templates=%d devices=%d contributions=%u\n",
               nt, nd, contribs);
    for (int i = 0; i < 32 && i < nt; i++) {
        char name[48], chip[32];
        int comps = 0, ver = 0;
        if (hdpk_template_summary(i, name, sizeof(name), chip, sizeof(chip),
                                  &comps, &ver) == 0 && i < 6) {
            vga_printf("  [T%d] %s chip=%s comps=%d ver=%d\n",
                       i, name, chip, comps, ver);
        }
    }
    for (int i = 0; i < 128 && i < nd && i < 4; i++) {
        char brand[32], model[32];
        int lvl = 0;
        uint32_t ram = 0;
        if (hdpk_device_summary(i, brand, sizeof(brand), model, sizeof(model),
                                &lvl, &ram) == 0) {
            vga_printf("  [D%d] %s %s (lvl=%d ram=%dMB)\n",
                       i, brand, model, lvl, ram);
        }
    }
}

/* V3.87-E15: 首次启动新手引导——数据盘无 firstboot.flag 时显示引导页 */
void firstboot_welcome(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_writefile(const char *, const char *);
    char buf[8];
    if (ide_ext2_read_file("/", "firstboot.flag", buf, sizeof(buf)) >= 0)
        return;   /* 已引导过 */
    if (ide_ext2_writefile("firstboot.flag", "1") <= 0)
        return;   /* 无数据盘/写入失败: 静默跳过 */
    vga_puts("\n");
    vga_puts(" =============================================================\n");
    vga_puts("   HEPU OS (Hyper Evolve) v1.02 — ");
    vga_puts(i18n_get(I18N_BOOT_COMPLETE));
    vga_putc('\n');
    vga_puts(" -------------------------------------------------------------\n");
    vga_puts("   Commands: help  apps  hug  fs mount  dmesg  perf  ps\n");
    vga_puts("             reboot  shutdown  version\n");
    vga_puts("   App formats: .hug native | EXE / Linux ELF / macOS compat\n");
    vga_puts("   Install: base image + H-DPK device pack = full system\n");
    vga_puts("   Open source: GPLv3 + HEPU terms (kernel source marking)\n");
    vga_puts(" =============================================================\n");
    vga_puts("  Type help for the full command list.\n\n");
}

/* V3.87-E10: 设置下一 ELF 程序的 CPU 限额(tick@100Hz, 0=不限) */
void cmd_setappcpu(char *arg)
{
    extern uint32_t binfmt_next_cpu_limit;
    if (!arg || !*arg) {
        vga_printf("usage: setappcpu <ticks>  (0=unlimited, 当前=%d)\n",
                   binfmt_next_cpu_limit);
        return;
    }
    uint32_t v = 0;
    for (int i = 0; arg[i]; i++) v = v * 10 + (arg[i] - '0');
    binfmt_next_cpu_limit = v;
    vga_printf("[SETAPPCU] next ELF cpu_limit=%d ticks\n", v);
}

/* V3.87-E9: IPC 睡眠锁自测——占用→超时路径 + 释放→获取路径 */
void cmd_ipcstress(void)
{
    extern int ipc_mutex_create(uint32_t);
    extern int ipc_mutex_lock_timed(int, uint32_t, uint32_t);
    extern int ipc_mutex_unlock(int, uint32_t);
    extern int ipc_mutex_destroy(int);
    int m = ipc_mutex_create(0x5EED);
    if (m < 0) { vga_puts("[IPC] create fail\n"); return; }
    /* 模拟 pid=9 持有 */
    if (ipc_mutex_lock_timed(m, 9, 1000) != 0) { vga_puts("[IPC] lock fail\n"); return; }
    /* 路径1: 主线(pid=1)等被占锁 → 应阻塞 100ms 后超时返回 -3 */
    int r1 = ipc_mutex_lock_timed(m, 1, 100);
    /* 路径2: 释放后, 主线应获取成功 */
    ipc_mutex_unlock(m, 9);
    int r2 = ipc_mutex_lock_timed(m, 1, 1000);
    ipc_mutex_unlock(m, 1);
    ipc_mutex_destroy(m);
    vga_printf("[IPC] timeout-path=%d(expect -3) acquire-path=%d(expect 0)\n", r1, r2);
}

/* V3.87-E7: 记账逻辑自测——验证限额拒绝与退款 */
void cmd_memtest(void)
{
    extern process_t proc_table[];
    extern void process_set_limits(process_t *, uint32_t, uint32_t);
    extern int process_mem_charge(process_t *, uint32_t);
    extern void process_mem_refund(process_t *, uint32_t);
    process_t *p = &proc_table[0];  /* init */
    uint32_t saved = p->mem_limit;
    process_set_limits(p, 96U * 1024U / PAGE_SIZE, 0);   /* 96KB 限额 = 24页 */
    uint32_t before = p->mem_used;
    int r1 = process_mem_charge(p, 10);   /* +10页: 应成功 */
    int r2 = process_mem_charge(p, 15);   /* +15页: 应超限拒绝 */
    process_mem_refund(p, 10);
    process_mem_refund(p, 15);
    vga_printf("[MEMTEST] charge1=%d charge2=%d(refused) used=%d->%d viol=%d\n",
               r1, r2, before, p->mem_used, p->res_violations);
    process_set_limits(p, saved, 0);
}

/* V3.87-E7: 资源限额命令 (limits pid mem_kb) */
void cmd_limits(char *arg)
{
    extern process_t proc_table[];
    if (!arg || !*arg) {
        vga_puts("usage: limits <pid> <mem_kb>  (0=unlimited)\n");
        return;
    }
    char *sp = 0;
    for (char *c = arg; *c; c++) if (*c == ' ') { *c = 0; sp = c + 1; break; }
    if (!sp) return;
    int pid = 0, kb = 0;
    { int i = 0; while (arg[i]) { pid = pid * 10 + (arg[i] - '0'); i++; } }
    { int i = 0; while (sp[i]) { kb = kb * 10 + (sp[i] - '0'); i++; } }
    extern void process_set_limits(process_t *, uint32_t, uint32_t);
    uint32_t t = 0, r = 0, q = 0;
    process_stats(&t, &r, &q);
    process_t *p = 0;
    for (uint32_t i = 0; i < t; i++) {
        if (proc_table[i].pid == (uint32_t)pid) { p = &proc_table[i]; break; }
    }
    if (!p) { vga_printf("pid %d not found\n", pid); return; }
    process_set_limits(p, (uint32_t)kb / (PAGE_SIZE / 1024), p->cpu_limit_ticks);
    vga_printf("[LIMITS] pid=%d mem_limit=%dKB cpu_limit=%d\n",
               pid, kb, p->cpu_limit_ticks);
}

/* V3.91-E37: 统一资源限制——rlimit(显示全部) / rlimit <pid> <mem_kb> <cpu_ticks> */
void cmd_rlimit(char *arg)
{
    extern process_t proc_table[];
    extern void process_set_limits(process_t *, uint32_t, uint32_t);
    if (!arg || !*arg) {
        proc_lock_acquire();
        vga_puts(" pid   mem_KB   cpu_tk  mem_usedKB  mem_over\n");
        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            process_t *p = &proc_table[i];
            if (p->state == PROC_UNUSED || !p->name[0]) continue;
            vga_printf(" %-3d %8d %8d %11d %d\n",
                       p->pid,
                       p->mem_limit ? p->mem_limit * (PAGE_SIZE / 1024) : 0,
                       p->cpu_limit_ticks,
                       p->mem_used * (PAGE_SIZE / 1024),
                       p->mem_over_ticks);
        }
        proc_lock_release();
        return;
    }
    char *sp1 = 0, *sp2 = 0;
    for (char *c = arg; *c; c++) if (*c == ' ') { *c = 0; sp1 = c + 1; break; }
    if (!sp1) return;
    for (char *c = sp1; *c; c++) if (*c == ' ') { *c = 0; sp2 = c + 1; break; }
    if (!sp2) return;
    int pid = 0, kb = 0, cpu = 0;
    { int i = 0; while (arg[i]) { pid = pid * 10 + (arg[i] - '0'); i++; } }
    { int i = 0; while (sp1[i]) { kb = kb * 10 + (sp1[i] - '0'); i++; } }
    { int i = 0; while (sp2[i]) { cpu = cpu * 10 + (sp2[i] - '0'); i++; } }
    proc_lock_acquire();
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_UNUSED || p->pid != (uint32_t)pid) continue;
        process_set_limits(p, (uint32_t)kb / (PAGE_SIZE / 1024), (uint32_t)cpu);
        p->mem_over_ticks = 0;
        vga_printf("[RLIMIT] pid=%d mem=%dKB cpu=%d mem_used=%dKB\n",
                   pid, kb, cpu, p->mem_used * (PAGE_SIZE / 1024));
        proc_lock_release();
        return;
    }
    proc_lock_release();
    vga_printf(" pid %d not found\n", pid);
}

/* V3.92-E38: softirq 底半部演示与统计 */
void cmd_softirqtest(char *arg)
{
    extern void softirq_demo_ctl(int);
    extern uint32_t softirq_demo_count(void);
    if (arg && arg[0] == 'o' && (arg[1] == 'n' || arg[1] == 'N')) {
        softirq_demo_ctl(1);
        vga_puts("[SOFTIRQ] demo ON: raise every 25 ticks (run softstat to check)\n");
    } else {
        softirq_demo_ctl(0);
        vga_printf("[SOFTIRQ] demo OFF, handled=%d\n", softirq_demo_count());
    }
}

void cmd_softstat(void)
{
    extern void softirq_stats(uint32_t *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
    extern uint32_t softirq_demo_count(void);
    extern uint32_t softirq_demo_seen_count(void);
    extern uint32_t softirq_demo_enabled_get(void);
    uint32_t r = 0, h = 0, d = 0, m = 0, p = 0;
    softirq_stats(&r, &h, &d, &m, &p);
    vga_printf("[SOFTIRQ] raised=%d handled=%d dropped=%d pending=%d max_defer=%d ticks\n",
               r, h, d, p, m);
    vga_printf("[SOFTIRQ] demo handled=%d tick_seen=%d enabled=%d\n",
               softirq_demo_count(), softirq_demo_seen_count(), softirq_demo_enabled_get());
    /* V4.16-E61: 延迟直方图 (tick 桶) */
    extern void softirq_stats_hist(uint32_t *, uint32_t *);
    uint32_t hist[6], ssum = 0;
    softirq_stats_hist(hist, &ssum);
    uint32_t s_avg = (h > 0) ? (ssum / h) : 0;
    vga_printf("[SOFTIRQ] defer hist: 0-1:%d 2-5:%d 6-10:%d 11-20:%d 21-50:%d >50:%d avg=%d ticks\n",
               hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], s_avg);
}

/* V4.17-E62: 磁盘配额命令——quota set <uid> <blocks> / quota show */
/* V4.18-E63: fwrite <path> <text>——ext2 写测试(走 VFS + MAC 挂钩) */
