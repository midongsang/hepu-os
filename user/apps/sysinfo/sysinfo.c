/* ============================================================================
 * 赫普OS - 系统信息应用
 * 显示内核版本、CPU、内存、进程、系统调用等信息
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("      赫普OS 系统信息\n");
    printf("========================================\n\n");

    printf("[系统]\n");
    printf("  内核版本: %s\n", hepu_info());
    printf("  当前PID:  %d\n", hepu_getpid());
    printf("  系统调用: 63个 (SYS_MAX=63)\n\n");

    printf("[内存]\n");
    printf("  内核: 微内核架构\n");
    printf("  内存管理: 分页+虚拟内存\n");
    printf("  堆分配: kmalloc/kfree\n\n");

    printf("[进程]\n");
    printf("  调度: 抢占式时间片轮转\n");
    printf("  多核: SMP支持\n");
    printf("  进程状态: READY/RUNNING/BLOCKED/FROZEN/ZOMBIE\n\n");

    printf("[文件系统]\n");
    printf("  VFS: 虚拟文件系统\n");
    printf("  支持: initrd/devfs/ext2\n");
    printf("  应用格式: .hug (Hyper User Gear)\n\n");

    printf("[网络]\n");
    printf("  协议栈: TCP/IP\n");
    printf("  支持: WiFi/蓝牙/Web引擎\n\n");

    printf("[安全]\n");
    printf("  版权指纹: GPLv3+HEPU附加条款\n");
    printf("  沙盒: 应用隔离\n");
    printf("  权限: 精细化权限管理\n\n");

    printf("[特色功能]\n");
    printf("  - 内核级后台冻结\n");
    printf("  - 设备标识随机化\n");
    printf("  - 弹性自由桌面\n");
    printf("  - AI通知分级\n");
    printf("  - 创变自由模式\n");
    printf("  - 多用户隐私空间\n");
    printf("  - 多语言输入法(21种)\n");
    printf("  - 系统i18n(30种语言)\n\n");

    printf("========================================\n");
    printf("  HEPU Hyper Evolve OS\n");
    printf("  系统归人，数据归己，自由可控，极致纯净\n");
    printf("========================================\n");

    return 0;
}
