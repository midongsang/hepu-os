/* ============================================================================
 * 赫普OS - 包管理器命令行工具
 * V8.53: 初始实现
 * 用法: hpkg <command> [args]
 *   hpkg install <package.hpkg>  - 安装包
 *   hpkg remove <name>           - 卸载包
 *   hpkg update <package.hpkg>   - 更新包
 *   hpkg list                     - 列出已安装包
 *   hpkg info <name>              - 查看包信息
 *   hpkg search <keyword>         - 搜索包
 *   hpkg help                     - 显示帮助
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 系统调用 */
extern int hepu_write(int fd, const void *buf, int count);
extern int hepu_read(int fd, void *buf, int count);
extern void hepu_exit(int status);

/* 标准输出 */
static void puts(const char *s) {
    int len = 0;
    while (s[len]) len++;
    hepu_write(1, s, len);
}

static void putchar(char c) {
    hepu_write(1, &c, 1);
}

/* 字符串工具 */
static int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 数字转字符串 */
static void itoa(int n, char *buf) {
    int i = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    if (n < 0) { buf[i++] = '-'; n = -n; }
    char tmp[16];
    int j = 0;
    while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
    while (j > 0) buf[i++] = tmp[--j];
    buf[i] = 0;
}

/* 帮助信息 */
static void print_help(void) {
    puts("Hepu Package Manager (hpkg) v1.0\n");
    puts("Usage: hpkg <command> [args]\n\n");
    puts("Commands:\n");
    puts("  install <package.hpkg>  Install a package\n");
    puts("  remove <name>            Remove a package\n");
    puts("  update <package.hpkg>    Update a package\n");
    puts("  list                     List installed packages\n");
    puts("  info <name>              Show package information\n");
    puts("  search <keyword>         Search packages\n");
    puts("  help                     Show this help\n");
}

/* 简化实现：通过内核系统调用接口
 * 实际实现需要系统调用号，这里用模拟输出演示 */

static int cmd_install(int argc, char **argv) {
    if (argc < 2) {
        puts("Error: package file required\n");
        puts("Usage: hpkg install <package.hpkg>\n");
        return 1;
    }
    puts("Installing: ");
    puts(argv[1]);
    puts("\n");
    /* TODO: 调用内核hpkg_install */
    puts("[HPKG] Package installed successfully\n");
    return 0;
}

static int cmd_remove(int argc, char **argv) {
    if (argc < 2) {
        puts("Error: package name required\n");
        puts("Usage: hpkg remove <name>\n");
        return 1;
    }
    puts("Removing: ");
    puts(argv[1]);
    puts("\n");
    /* TODO: 调用内核hpkg_remove */
    puts("[HPKG] Package removed successfully\n");
    return 0;
}

static int cmd_update(int argc, char **argv) {
    if (argc < 2) {
        puts("Error: package file required\n");
        puts("Usage: hpkg update <package.hpkg>\n");
        return 1;
    }
    puts("Updating: ");
    puts(argv[1]);
    puts("\n");
    /* TODO: 调用内核hpkg_update */
    puts("[HPKG] Package updated successfully\n");
    return 0;
}

static int cmd_list(int argc, char **argv) {
    puts("Installed packages:\n");
    puts("------------------------\n");
    /* TODO: 调用内核hpkg_list */
    puts("(none - database empty)\n");
    return 0;
}

static int cmd_info(int argc, char **argv) {
    if (argc < 2) {
        puts("Error: package name required\n");
        puts("Usage: hpkg info <name>\n");
        return 1;
    }
    puts("Package: ");
    puts(argv[1]);
    puts("\n");
    /* TODO: 调用内核hpkg_query */
    puts("Status: not installed\n");
    return 0;
}

static int cmd_search(int argc, char **argv) {
    if (argc < 2) {
        puts("Error: keyword required\n");
        puts("Usage: hpkg search <keyword>\n");
        return 1;
    }
    puts("Searching for: ");
    puts(argv[1]);
    puts("\n");
    /* TODO: 搜索可用包 */
    puts("(no results)\n");
    return 0;
}

/* 主函数 */
int main(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0) {
        return cmd_install(argc - 1, argv + 1);
    } else if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "uninstall") == 0) {
        return cmd_remove(argc - 1, argv + 1);
    } else if (strcmp(cmd, "update") == 0 || strcmp(cmd, "upgrade") == 0) {
        return cmd_update(argc - 1, argv + 1);
    } else if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) {
        return cmd_list(argc - 1, argv + 1);
    } else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "show") == 0) {
        return cmd_info(argc - 1, argv + 1);
    } else if (strcmp(cmd, "search") == 0 || strcmp(cmd, "find") == 0) {
        return cmd_search(argc - 1, argv + 1);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_help();
        return 0;
    } else if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        puts("hpkg v1.0\n");
        return 0;
    } else {
        puts("Unknown command: ");
        puts(cmd);
        puts("\n");
        puts("Run 'hpkg help' for usage.\n");
        return 1;
    }
}
