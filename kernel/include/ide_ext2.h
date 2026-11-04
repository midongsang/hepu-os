#ifndef IDE_EXT2_H
#define IDE_EXT2_H
#include <stdint.h>
int ide_ext2_mount(void);
int ide_ext2_sync(void);
int ide_ext2_fsck(void);
void ide_ext2_list_dir(void);
int ide_ext2_cat(const char *name);
void ide_ext2_selftest(void);
int ide_ext2_writefile(const char *name, const char *data);
int ide_ext2_read_file(const char *dirpath, const char *name, char *buf, int max);
int ide_ext2_get_file_size(const char *dirpath, const char *name);  /* V7.47: 获取文件大小 */
int ide_ext2_mkdir(const char *path);
int ide_ext2_delete(const char *dirpath, const char *name);
int ide_ext2_write_raw(const char *dirpath, const char *name, const char *data, int len);

/* V2.58: 手机文件管理器列表条目 */
#define FM_MAX_ENTRIES 32
typedef struct {
    char name[128];
    uint32_t size;
    uint8_t is_dir;
} fm_entry_t;

/* 列出目录条目（dirpath: "/" 根 或 "/docs" 子目录），返回条目数 */
int ide_ext2_list_entries(const char *dirpath, fm_entry_t *entries, int max);
#endif
/* V3.87-D2: 数据盘剩余可写字节 (挂载失败返回 -1) */
int ide_ext2_free_bytes(void);

/* V3.87-D6: 按需块访问 (superblock+GDT 常驻; 位图/inode表/间接表/数据块走 4x4KB 缓存) */
int ext2_read_block(uint32_t blk, uint8_t *out);
int ext2_write_block(uint32_t blk, const uint8_t *in);
uint8_t *ext2_block_access(uint32_t blk);
void ext2_block_dirty(uint32_t blk);
int ext2_block_flush(void);
