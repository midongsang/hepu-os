/* V3.88-E18: 用户态异常隔离测试——除零触发 #DE, 验证"杀进程不宕内核" */
void _start(void)
{
    volatile int z = 0;
    int y = 42 / z;   /* #DE 除零异常 → 用户态异常隔离应杀本进程 */
    (void)y;
    for (;;) { /* 正常不会到达 */ }
}
