/* ============================================================================
 * 赫普OS - 磁盘配额管理
 * ========================================================================== */

#include <stdint.h>
#include "quota.h"
#include "vga.h"
#include "string.h"

static quota_entry_t quotas[MAX_QUOTA_USERS];
static int quota_initialized = 0;

void quota_init(void)
{
    memset(quotas, 0, sizeof(quotas));
    /* root用户无限制 */
    quotas[0].uid = 0;
    quotas[0].block_limit = 0xFFFFFFFF;
    quotas[0].inode_limit = 0xFFFFFFFF;
    quotas[0].enabled = 1;
    quota_initialized = 1;
    vga_puts("[QUOTA] disk quota mgr init \n");
}

static quota_entry_t *find_quota(uint32_t uid)
{
    for (int i = 0; i < MAX_QUOTA_USERS; i++) {
        if (quotas[i].enabled && quotas[i].uid == uid) {
            return &quotas[i];
        }
    }
    return NULL;
}

int quota_set_limit(uint32_t uid, uint32_t block_limit, uint32_t inode_limit)
{
    if (!quota_initialized) return -1;

    quota_entry_t *q = find_quota(uid);
    if (!q) {
        /* 创建新配额项 */
        for (int i = 0; i < MAX_QUOTA_USERS; i++) {
            if (!quotas[i].enabled) {
                q = &quotas[i];
                q->uid = uid;
                q->block_used = 0;
                q->inode_used = 0;
                q->enabled = 1;
                break;
            }
        }
    }
    if (!q) return -1;

    q->block_limit = block_limit;
    q->inode_limit = inode_limit;
    vga_printf("[QUOTA] set user %d quota: block =%d, inode=%d\n",
               uid, block_limit, inode_limit);
    return 0;
}

int quota_check_block(uint32_t uid, uint32_t blocks_needed)
{
    quota_entry_t *q = find_quota(uid);
    if (!q) return 0;  /* 无配额限制 */
    if (q->block_used + blocks_needed > q->block_limit) return -1;
    return 0;
}

int quota_check_inode(uint32_t uid, uint32_t inodes_needed)
{
    quota_entry_t *q = find_quota(uid);
    if (!q) return 0;
    if (q->inode_used + inodes_needed > q->inode_limit) return -1;
    return 0;
}

int quota_alloc_block(uint32_t uid, uint32_t blocks)
{
    quota_entry_t *q = find_quota(uid);
    if (!q) return 0;
    if (q->block_used + blocks > q->block_limit) return -1;
    q->block_used += blocks;
    return 0;
}

int quota_free_block(uint32_t uid, uint32_t blocks)
{
    quota_entry_t *q = find_quota(uid);
    if (!q) return 0;
    if (q->block_used >= blocks) {
        q->block_used -= blocks;
    } else {
        q->block_used = 0;
    }
    return 0;
}

int quota_alloc_inode(uint32_t uid, uint32_t inodes)
{
    quota_entry_t *q = find_quota(uid);
    if (!q) return 0;
    if (q->inode_used + inodes > q->inode_limit) return -1;
    q->inode_used += inodes;
    return 0;
}

int quota_free_inode(uint32_t uid, uint32_t inodes)
{
    quota_entry_t *q = find_quota(uid);
    if (!q) return 0;
    if (q->inode_used >= inodes) {
        q->inode_used -= inodes;
    } else {
        q->inode_used = 0;
    }
    return 0;
}

/* V4.17-E62: 全量配额表显示 (shell `quota show` 用) */
void quota_show_all(void)
{
    vga_puts("uid   blocks_used blocks_limit inodes_used inodes_limit\n");
    for (int i = 0; i < MAX_QUOTA_USERS; i++) {
        if (quotas[i].enabled) {
            vga_printf("%-5d %-11d %-12d %-11d %d\n",
                       quotas[i].uid, quotas[i].block_used,
                       quotas[i].block_limit, quotas[i].inode_used,
                       quotas[i].inode_limit);
        }
    }
}

int quota_get_info(uint32_t uid, quota_entry_t *info)
{
    if (!info) return -1;
    quota_entry_t *q = find_quota(uid);
    if (!q) return -1;
    *info = *q;
    return 0;
}
