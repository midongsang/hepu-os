/* ============================================================================
 * ext2 fsck inode 检查主机端回归（V0.55.2）
 *
 * 背景：check_inode() 早先实现了却从未被 fsck 主流程调用（属功能缺口）。
 * V0.55.2 把它接入 check_inode_tables() 遍历 inode 表。本测试直接 include
 * 该源文件以拿到 static 实现，用桩 ext2_read_inode 供给 inode 内容，
 * 验证：孤立 inode(links==0) 能报错、空闲 inode 不报错、遍历计数正确。
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vga_puts(const char *s) { (void)s; }
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }

/* 注意：不 include kernel/include/ext2.h —— 它与 ext2_fsck64.c 自带的结构定义
 * 同名但非 packed，直接同编会冲突。这里测的就是 fsck 自己那套。 */
#include "../../kernel/arch/x86_64/ext2_fsck64.c"

/* 桩：check_inode_tables 通过它读取 inode */
static ext2_inode_t g_fake[16];
static int g_fake_n = 0;

int ext2_read_inode(uint32_t ino, ext2_inode_t *inode)
{
    if (ino < 2 || (int)ino >= 2 + g_fake_n) return -1;
    *inode = g_fake[ino - 2];
    return 0;
}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void reset_stats(void)
{
    fsck_stats.errors_found = 0;
    fsck_stats.errors_fixed = 0;
    fsck_stats.warnings = 0;
    fsck_stats.inodes_checked = 0;
}

int main(void)
{
    ext2_inode_t in;

    /* 1. 空闲 inode (mode==0)：不报错，且不计入已检查数 */
    memset(&in, 0, sizeof in);
    reset_stats();
    CHECK(check_inode(&in, 5) == 0);
    CHECK(fsck_stats.inodes_checked == 0);

    /* 2. 已分配但链接计数为 0：孤立 inode，应报 1 个错误 */
    memset(&in, 0, sizeof in);
    in.i_mode = 0x81A4;          /* S_IFREG | 0644 */
    in.i_links_count = 0;
    reset_stats();
    CHECK(check_inode(&in, 7) == 1);
    CHECK(fsck_stats.inodes_checked == 1);

    /* 3. 正常 inode：链接计数 >= 1，不报错 */
    in.i_links_count = 1;
    reset_stats();
    CHECK(check_inode(&in, 8) == 0);

    /* 4. 遍历：3 个 inode，其中第 2 个是孤立的 -> 整体应有 1 个错误，
     *    且读取失败(越界)的 inode 不计入检查数 */
    memset(g_fake, 0, sizeof g_fake);
    g_fake_n = 3;
    g_fake[0].i_mode = 0x81A4; g_fake[0].i_links_count = 1;
    g_fake[1].i_mode = 0x81A4; g_fake[1].i_links_count = 0;   /* 孤立 */
    g_fake[2].i_mode = 0;                                     /* 空闲 */

    ext2_superblock_t sb;
    memset(&sb, 0, sizeof sb);
    sb.s_inodes_per_group = 3;
    sb.s_inodes_count = 3;

    reset_stats();
    int errs = check_inode_tables(&sb, 1);
    CHECK(errs == 1);
    CHECK(fsck_stats.inodes_checked == 2);   /* 两个已分配 inode 被检查 */

    /* 5. inodes_per_group==0 时必须安全退出，不能除零/死循环 */
    sb.s_inodes_per_group = 0;
    CHECK(check_inode_tables(&sb, 4) == 0);

    printf(fails ? "fsck inode checks: %d FAILED\n" : "fsck inode checks: ok (5 groups)\n", fails);
    return fails ? 1 : 0;
}
