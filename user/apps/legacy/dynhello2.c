/* ============================================================================
 * dynhello2 —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/dynhello2.elf：
 *   DT_NEEDED = libtest2.so，.interp = /lib64/ld-linux-x86-64.so.2
 *   反汇编逐条还原 _start：
 *       a = call_func(0);
 *       b = call_func(1);
 *       c = get_value();
 *       d = double_value();
 *       _exit(a + b + c + d);
 *   没有输出 —— 它只把四个调用的结果加总后作为退出码。
 *
 * 与 dynhello 的分工：dynhello 测「单个导出函数 + 打印」，
 * dynhello2 测「函数指针表间接调用 + 多返回值汇总」。
 *
 * 注意：HepuOS 当前不支持 PT_INTERP 动态链接器加载，
 *       本程序在 HepuOS 上会被 exec 拒绝 —— 保留源码是为了让
 *       「历史测试资产」可从源码重建，而不是为了在内核上跑通。
 * ========================================================================== */
extern int call_func(int i);
extern int get_value(void);
extern int double_value(void);

void _exit(int code);

void _start(void)
{
    int a = call_func(0);
    int b = call_func(1);
    int c = get_value();
    int d = double_value();

    _exit(a + b + c + d);
}
