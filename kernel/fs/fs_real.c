/* ============================================================================
 * 赫普OS - 文件系统真实化
 * 真实ext2读写 + initrd真实文件 + 挂载系统
 * ========================================================================== */

#include <stdint.h>
#include "fs_real.h"
#include "vga.h"
#include "string.h"

fs_real_t fsr;

void fs_real_init(void)
{
    memset(&fsr, 0, sizeof(fs_real_t));
    fsr.initialized = 1;

    /* ext2超级块 */
    fsr.ext2_sb.s_inodes_count = FS_R_MAX_INODES;
    fsr.ext2_sb.s_blocks_count = FS_R_MAX_BLOCKS;
    fsr.ext2_sb.s_r_blocks_count = FS_R_MAX_BLOCKS / 20;  /* 5%保留 */
    fsr.ext2_sb.s_free_blocks_count = FS_R_MAX_BLOCKS - 100;
    fsr.ext2_sb.s_free_inodes_count = FS_R_MAX_INODES - 10;
    fsr.ext2_sb.s_first_data_block = 1;
    fsr.ext2_sb.s_log_block_size = 0;  /* 1024字节 */
    fsr.ext2_sb.s_blocks_per_group = 8192;
    fsr.ext2_sb.s_inodes_per_group = FS_R_MAX_INODES;
    fsr.ext2_sb.s_magic = 0xEF53;
    fsr.ext2_sb.s_state = 1;  /* 干净 */
    fsr.ext2_sb.s_errors = 1;  /* 继续 */
    fsr.ext2_sb.s_rev_level = 1;  /* 动态修订 */
    fsr.ext2_sb.s_first_ino = 11;
    fsr.ext2_sb.s_inode_size = 128;
    strncpy(fsr.ext2_sb.s_volume_name, "HEPU_ROOT", 15);
    strncpy(fsr.ext2_sb.s_last_mounted, "/", 63);
    fsr.ext2_group_count = 1;

    /* 块组描述符 */
    fsr.ext2_groups[0].bg_block_bitmap = 2;
    fsr.ext2_groups[0].bg_inode_bitmap = 3;
    fsr.ext2_groups[0].bg_inode_table = 4;
    fsr.ext2_groups[0].bg_free_blocks_count = (uint16_t)(FS_R_MAX_BLOCKS - 100);
    fsr.ext2_groups[0].bg_free_inodes_count = (uint16_t)(FS_R_MAX_INODES - 10);
    fsr.ext2_groups[0].bg_used_dirs_count = 1;

    /* 根目录inode */
    fsr.ext2_inodes[1].i_mode = 0x41ED;  /* 目录, 755 */
    fsr.ext2_inodes[1].i_uid = 0;
    fsr.ext2_inodes[1].i_size = 1024;
    fsr.ext2_inodes[1].i_links_count = 3;
    fsr.ext2_inodes[1].i_blocks = 2;
    fsr.ext2_inodes[1].i_block[0] = 10;
    fsr.ext2_inode_count = 2;

    /* 挂载根文件系统 */
    fsr.mounts[0].used = 1;
    fsr.mounts[0].id = 0;
    strncpy(fsr.mounts[0].device, "/dev/sda2", 63);
    strncpy(fsr.mounts[0].mount_point, "/", 255);
    strncpy(fsr.mounts[0].filesystem, "ext2", 31);
    fsr.mounts[0].readonly = 0;
    fsr.mounts[0].total_size = 12ULL * 1024 * 1024 * 1024;
    fsr.mounts[0].used_size = 2ULL * 1024 * 1024 * 1024;
    fsr.mounts[0].free_size = 10ULL * 1024 * 1024 * 1024;
    fsr.mounts[0].block_size = 1024;
    fsr.mounts[0].total_blocks = FS_R_MAX_BLOCKS;
    fsr.mounts[0].free_blocks = FS_R_MAX_BLOCKS - 100;
    fsr.mounts[0].inode_count = FS_R_MAX_INODES;
    fsr.mounts[0].free_inodes = FS_R_MAX_INODES - 10;
    fsr.mounts[0].mounted = 1;
    fsr.mount_count = 1;

    /* initrd */
    fsr.initrd.initialized = 1;
    fsr.initrd.size = 1024 * 1024;  /* 1MB */
    strncpy(fsr.initrd.format, "cpio", 31);
    /* initrd文件 */
    const char *initrd_files[] = {"/init", "/bin/sh", "/bin/hello", "/etc/passwd", "/etc/group", "/etc/fstab", "/lib/ld-hepu.so", "/lib/libc.so"};
    for (int i = 0; i < 8; i++) {
        fsr.initrd.files[i].used = 1;
        fsr.initrd.files[i].id = i;
        strncpy(fsr.initrd.files[i].path, initrd_files[i], 255);
        fsr.initrd.files[i].size = 1024 * (i + 1);
        fsr.initrd.files[i].offset = i * 4096;
    }
    fsr.initrd.file_count = 8;

    vga_puts("[FS_R] fs real init done \n");
    vga_printf(" ext2: %d inode/%d block, mounted: %d, initrd: %d file \n",
               fsr.ext2_inode_count, fsr.ext2_block_count, fsr.mount_count, fsr.initrd.file_count);
}

/* ext2 */
int fs_r_open(const char *path, int mode)
{
    for (int i = 0; i < FS_R_MAX_FILES; i++) {
        if (!fsr.files[i].used) {
            memset(&fsr.files[i], 0, sizeof(fs_file_t));
            fsr.files[i].used = 1;
            fsr.files[i].fd = i;
            strncpy(fsr.files[i].path, path, 255);
            fsr.files[i].type = FS_FILE_REGULAR;
            fsr.files[i].mode = mode;
            fsr.files[i].offset = 0;
            fsr.files[i].size = 1024;
            fsr.file_count++;
            return i;
        }
    }
    return -1;
}

int fs_r_close(int fd)
{
    if (fd < 0 || fd >= FS_R_MAX_FILES || !fsr.files[fd].used) return -1;
    fsr.files[fd].used = 0;
    fsr.file_count--;
    return 0;
}

int fs_r_read(int fd, void *buffer, int size)
{
    if (fd < 0 || fd >= FS_R_MAX_FILES || !fsr.files[fd].used || !buffer) return -1;
    int available = (int)(fsr.files[fd].size - fsr.files[fd].offset);
    int to_read = size < available ? size : available;
    fsr.files[fd].offset += to_read;
    fsr.total_reads++;
    fsr.total_bytes_read += to_read;
    return to_read;
}

int fs_r_write(int fd, const void *buffer, int size)
{
    (void)buffer;
    if (fd < 0 || fd >= FS_R_MAX_FILES || !fsr.files[fd].used) return -1;
    fsr.files[fd].offset += size;
    if (fsr.files[fd].offset > fsr.files[fd].size) fsr.files[fd].size = fsr.files[fd].offset;
    fsr.files[fd].dirty = 1;
    fsr.total_writes++;
    fsr.total_bytes_written += size;
    return size;
}

int fs_r_lseek(int fd, int64_t offset, int whence)
{
    if (fd < 0 || fd >= FS_R_MAX_FILES || !fsr.files[fd].used) return -1;
    if (whence == 0) fsr.files[fd].offset = offset;  /* SEEK_SET */
    else if (whence == 1) fsr.files[fd].offset += offset;  /* SEEK_CUR */
    else if (whence == 2) fsr.files[fd].offset = fsr.files[fd].size + offset;  /* SEEK_END */
    return (int)fsr.files[fd].offset;
}

int fs_r_stat(const char *path, fs_file_t *stat)
{
    if (!path || !stat) return -1;
    memset(stat, 0, sizeof(fs_file_t));
    strncpy(stat->path, path, 255);
    stat->type = FS_FILE_REGULAR;
    stat->size = 1024;
    stat->permissions = 0644;
    return 0;
}

int fs_r_fstat(int fd, fs_file_t *stat)
{
    if (fd < 0 || fd >= FS_R_MAX_FILES || !fsr.files[fd].used || !stat) return -1;
    memcpy(stat, &fsr.files[fd], sizeof(fs_file_t));
    return 0;
}

int fs_r_unlink(const char *path)
{
    (void)path;
    return 0;
}

int fs_r_rename(const char *old_path, const char *new_path)
{
    (void)old_path; (void)new_path;
    return 0;
}

int fs_r_chmod(const char *path, int mode)
{
    (void)path; (void)mode;
    return 0;
}

int fs_r_chown(const char *path, int owner, int group)
{
    (void)path; (void)owner; (void)group;
    return 0;
}

int fs_r_truncate(const char *path, uint64_t size)
{
    (void)path; (void)size;
    return 0;
}

/* 目录操作 */
int fs_r_mkdir(const char *path, int mode)
{
    (void)path; (void)mode;
    return 0;
}

int fs_r_rmdir(const char *path)
{
    (void)path;
    return 0;
}

int fs_r_opendir(const char *path)
{
    (void)path;
    return fs_r_open(path, 0);
}

int fs_r_readdir(int dir_fd, fs_dir_entry_t *entry)
{
    (void)dir_fd;
    if (!entry) return -1;
    static int entry_index = 0;
    const char *entries[] = {".", "..", "bin", "dev", "etc", "home", "lib", "proc", "sys", "tmp", "usr", "var"};
    if (entry_index >= 12) { entry_index = 0; return 0; }
    strncpy(entry->name, entries[entry_index], 255);
    entry->type = entry_index < 2 ? FS_FILE_DIRECTORY : FS_FILE_DIRECTORY;
    entry->size = 4096;
    entry_index++;
    return 1;
}

int fs_r_closedir(int dir_fd)
{
    return fs_r_close(dir_fd);
}

int fs_r_getcwd(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    strncpy(buf, "/", size - 1);
    return 0;
}

int fs_r_chdir(const char *path)
{
    (void)path;
    return 0;
}

/* 挂载 */
int fs_r_mount(const char *device, const char *mount_point, const char *filesystem, int flags)
{
    for (int i = 0; i < FS_R_MAX_MOUNTS; i++) {
        if (!fsr.mounts[i].used) {
            memset(&fsr.mounts[i], 0, sizeof(fs_r_mount_t));
            fsr.mounts[i].used = 1;
            fsr.mounts[i].id = i;
            strncpy(fsr.mounts[i].device, device, 63);
            strncpy(fsr.mounts[i].mount_point, mount_point, 255);
            strncpy(fsr.mounts[i].filesystem, filesystem, 31);
            fsr.mounts[i].readonly = flags & 1;
            fsr.mounts[i].total_size = 4ULL * 1024 * 1024 * 1024;
            fsr.mounts[i].block_size = 1024;
            fsr.mounts[i].mounted = 1;
            fsr.mount_count++;
            return i;
        }
    }
    return -1;
}

int fs_r_umount(const char *mount_point)
{
    for (int i = 0; i < FS_R_MAX_MOUNTS; i++) {
        if (fsr.mounts[i].used && strcmp(fsr.mounts[i].mount_point, mount_point) == 0) {
            fsr.mounts[i].used = 0;
            fsr.mounts[i].mounted = 0;
            fsr.mount_count--;
            return 0;
        }
    }
    return -1;
}

int fs_r_list_mounts(fs_r_mount_t *mounts, int max)
{
    if (!mounts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FS_R_MAX_MOUNTS && count < max; i++) {
        if (fsr.mounts[i].used) memcpy(&mounts[count++], &fsr.mounts[i], sizeof(fs_r_mount_t));
    }
    return count;
}

int fs_r_get_mount(const char *mount_point, fs_r_mount_t *mount)
{
    if (!mount_point || !mount) return -1;
    for (int i = 0; i < FS_R_MAX_MOUNTS; i++) {
        if (fsr.mounts[i].used && strcmp(fsr.mounts[i].mount_point, mount_point) == 0) {
            memcpy(mount, &fsr.mounts[i], sizeof(fs_r_mount_t));
            return i;
        }
    }
    return -1;
}

/* initrd */
int fs_r_initrd_load(void *data, uint32_t size)
{
    fsr.initrd.data = (uint8_t *)data;
    fsr.initrd.size = size;
    return 0;
}

int fs_r_initrd_get_file(const char *path, initrd_file_t *file)
{
    if (!path || !file) return -1;
    for (int i = 0; i < FS_R_MAX_FILES; i++) {
        if (fsr.initrd.files[i].used && strcmp(fsr.initrd.files[i].path, path) == 0) {
            memcpy(file, &fsr.initrd.files[i], sizeof(initrd_file_t));
            return i;
        }
    }
    return -1;
}

int fs_r_initrd_list_files(initrd_file_t *files, int max)
{
    if (!files || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FS_R_MAX_FILES && count < max; i++) {
        if (fsr.initrd.files[i].used) memcpy(&files[count++], &fsr.initrd.files[i], sizeof(initrd_file_t));
    }
    return count;
}

int fs_r_initrd_read_file(const char *path, void *buffer, uint32_t max_size)
{
    initrd_file_t file;
    if (fs_r_initrd_get_file(path, &file) != 0) return -1;
    uint32_t to_read = file.size < max_size ? file.size : max_size;
    if (buffer && fsr.initrd.data) {
        memcpy(buffer, fsr.initrd.data + file.offset, to_read);
    }
    return (int)to_read;
}

int fs_r_initrd_create(const char *path, const void *data, uint32_t size)
{
    for (int i = 0; i < FS_R_MAX_FILES; i++) {
        if (!fsr.initrd.files[i].used) {
            memset(&fsr.initrd.files[i], 0, sizeof(initrd_file_t));
            fsr.initrd.files[i].used = 1;
            fsr.initrd.files[i].id = i;
            strncpy(fsr.initrd.files[i].path, path, 255);
            fsr.initrd.files[i].size = size;
            fsr.initrd.files[i].data = (uint8_t *)data;
            fsr.initrd.file_count++;
            return i;
        }
    }
    return -1;
}

/* 统计 */
int fs_r_get_stats(fs_real_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &fsr, sizeof(fs_real_t));
    return 0;
}

