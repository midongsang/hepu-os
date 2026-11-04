#ifndef QUOTA_H
#define QUOTA_H

#include <stdint.h>

#define MAX_QUOTA_USERS 16
#define QUOTA_DEFAULT_BLOCK_LIMIT 10000  /* 默认块限制 */
#define QUOTA_DEFAULT_INODE_LIMIT 1000   /* 默认inode限制 */

typedef struct {
    uint32_t uid;
    uint32_t block_used;
    uint32_t block_limit;
    uint32_t inode_used;
    uint32_t inode_limit;
    int enabled;
} quota_entry_t;

void quota_init(void);
int quota_set_limit(uint32_t uid, uint32_t block_limit, uint32_t inode_limit);
int quota_check_block(uint32_t uid, uint32_t blocks_needed);
int quota_check_inode(uint32_t uid, uint32_t inodes_needed);
int quota_alloc_block(uint32_t uid, uint32_t blocks);
int quota_free_block(uint32_t uid, uint32_t blocks);
int quota_alloc_inode(uint32_t uid, uint32_t inodes);
int quota_free_inode(uint32_t uid, uint32_t inodes);
int quota_get_info(uint32_t uid, quota_entry_t *info);

#endif /* QUOTA_H */
