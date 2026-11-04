#ifndef EXT2_FSCK_H
#define EXT2_FSCK_H

#include <stdint.h>

typedef struct {
    uint32_t total_inodes;
    uint32_t total_blocks;
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint32_t errors_found;
    uint32_t errors_fixed;
    uint32_t directories;
    uint32_t regular_files;
    uint32_t symlinks;
    uint32_t bad_inodes;
    uint32_t bad_blocks;
    uint32_t duplicate_blocks;
    uint32_t unreferenced_inodes;
} fsck_result_t;

int ext2_fsck_check(fsck_result_t *result);
int ext2_fsck_repair(fsck_result_t *result);

#endif /* EXT2_FSCK_H */
