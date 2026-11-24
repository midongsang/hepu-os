#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include "vfs.h"

/* P4: 结构定义与 64 位共用同一份，禁止在 32 位头里再维护一套。 */
#include "../shared/fs/ext2_shared.h"

/* 初始化ext2文件系统 */
extern uint32_t ext2_inode_size;
int ext2_init(void *disk_image, uint32_t size);

/* 挂载ext2到VFS */
int ext2_mount(const char *mountpoint);

/* 读取inode */
int ext2_read_inode(uint32_t inode_num, ext2_inode_t *inode);

/* 读取文件数据 */
int ext2_read_data(ext2_inode_t *inode, uint32_t offset, void *buf, uint32_t size);

/* 查找目录项 */
int ext2_lookup(ext2_inode_t *dir, const char *name, uint32_t *inode_num);

/* 写入文件数据 */
int ext2_write_data(ext2_inode_t *inode, uint32_t offset, const void *buf, uint32_t size);
int ext2_path_create(const char *path, uint16_t mode);
int ext2_path_mkdir(const char *path, uint16_t mode);
int ext2_path_unlink(const char *path);
int ext2_path_rename(const char *old_path, const char *new_path);
int ext2_path_symlink(const char *target, const char *linkpath);
int ext2_path_readlink(const char *path, char *buf, int bufsize);
int ext2_path_access(const char *path, int mode);
int ext2_sync(void);
uint32_t ext2_get_time(void);
int ext2_path_link(const char *old_path, const char *new_path);
int ext2_dir_compact(const char *path);
ext2_superblock_t *ext2_get_sb(void);
int ext2_path_create_v2(const char *path, uint16_t mode);
int ext2_path_mkdir_v2(const char *path, uint16_t mode);
int ext2_path_unlink_v2(const char *path);
int ext2_path_rename(const char *old_path, const char *new_path);
int ext2_path_symlink(const char *target, const char *linkpath);
int ext2_path_readlink(const char *path, char *buf, int bufsize);
int ext2_path_access(const char *path, int mode);
int ext2_sync(void);
uint32_t ext2_get_time(void);
int ext2_path_link(const char *old_path, const char *new_path);
int ext2_dir_compact(const char *path);
ext2_superblock_t *ext2_get_sb(void);

/* 创建文件 */
int ext2_create(ext2_inode_t *dir, const char *name, uint16_t mode, uint32_t *inode_num);

/* 删除文件 */
int ext2_unlink(ext2_inode_t *dir, const char *name);

/* 创建目录 */
int ext2_mkdir(ext2_inode_t *dir, const char *name, uint16_t mode);

/* 分配/释放块和inode */
uint32_t ext2_alloc_block(void);
void ext2_free_block(uint32_t block);
int ext2_read_block(uint32_t blk, uint8_t *out);
int ext2_write_block(uint32_t blk, const uint8_t *in);
void ext2_free_file_blocks(ext2_inode_t *inode);
uint32_t ext2_alloc_inode(void);
void ext2_free_inode(uint32_t inode_num);
int ext2_write_inode(uint32_t inode_num, ext2_inode_t *inode);

/* ext2 VFS写操作 */
int ext2_vfs_write(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size);

#endif /* EXT2_H */

/* 全局变量（供拆分模块使用） */
extern uint8_t *ext2_disk;
extern uint32_t ext2_disk_size;
extern ext2_superblock_t *ext2_sb;
extern uint32_t ext2_block_size;
extern ext2_group_desc_t *ext2_gdt;
extern int ext2_mounted;

/* 全局函数（供拆分模块使用） */
uint32_t ext2_get_block(ext2_inode_t *inode, uint32_t block_idx);
int ext2_add_dir_entry(ext2_inode_t *dir, uint32_t inode_num, const char *name, uint8_t type);
int ext2_resolve_path(const char *path, uint32_t *dir_inode_num, char *name, int name_max);
void ext2_update_atime(ext2_inode_t *inode);
void ext2_update_mtime(ext2_inode_t *inode);
uint32_t ext2_get_time(void);
