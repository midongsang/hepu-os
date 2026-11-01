/* ============================================================================
 * 赫普OS - VFS 虚拟文件系统
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include "vfs.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static vfs_node_t *vfs_root = NULL;
static vfs_fd_t fd_table[VFS_MAX_FDS];
static uint32_t next_inode = 1;

/* 路径分割辅助: 获取路径的第一部分 */
static const char *path_first_component(const char *path, char *buf, int buflen)
{
    int i = 0;
    while (*path == '/') path++;  /* 跳过开头的斜杠 */
    while (*path && *path != '/' && i < buflen - 1) {
        buf[i++] = *path++;
    }
    buf[i] = '\0';
    return path;
}

void vfs_init(void)
{
    /* 创建根目录 */
    vfs_root = vfs_create_node("/", VFS_DIRECTORY);
    vfs_root->inode_num = 0;

    /* 初始化文件描述符表 */
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        fd_table[i].used = 0;    }

    vga_puts("[VFS] virtual fs init done \n");
}

/* V3.87: 兼容进程标准流绑定——把 fd 0/1/2 指向指定节点
 * (Linux ELF 程序假定 stdin/stdout/stderr 已打开; vfs fd_table
 * 默认全空, 直接 write(1,..) 会返回 -1 丢输出). */
int vfs_bind_stdio(vfs_node_t *node)
{
    if (!node) return -1;
    for (int i = 0; i < 3; i++) {
        fd_table[i].used = 1;
        fd_table[i].node = node;
        fd_table[i].flags = 0;
        fd_table[i].position = 0;
    }
    return 0;
}

vfs_node_t *vfs_create_node(const char *name, vfs_node_type_t type)
{
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    memset(node, 0, sizeof(vfs_node_t));
    strncpy(node->name, name, VFS_MAX_NAME - 1);
    node->type = type;
    node->inode_num = next_inode++;
    node->permissions = 0755;
    node->links = 1;

    return node;
}

void vfs_add_child(vfs_node_t *parent, vfs_node_t *child)
{
    if (!parent || !child) return;
    child->parent = parent;
    child->next = parent->children;
    parent->children = child;
}

vfs_node_t *vfs_get_root(void)
{
    return vfs_root;
}

vfs_node_t *vfs_find(const char *path)
{
    if (!path || !vfs_root) return NULL;
    if (strcmp(path, "/") == 0) return vfs_root;

    vfs_node_t *current = vfs_root;
    char component[VFS_MAX_NAME];
    int symlink_depth = 0;
    const int MAX_SYMLINK_DEPTH = 8;  /* 最大软链接递归深度 */

    while (*path) {
        path = path_first_component(path, component, VFS_MAX_NAME);
        if (component[0] == '\0') break;

        /* 在当前目录的子节点中查找 */
        vfs_node_t *child = current->children;
        int found = 0;
        while (child) {
            if (strcmp(child->name, component) == 0) {
                current = child;
                found = 1;
                break;
            }
            child = child->next;
        }
        if (!found) return NULL;

        /* V8.21: 软链接解析（跟随符号链接） */
        if (current->type == VFS_SYMLINK && current->private_data) {
            if (symlink_depth >= MAX_SYMLINK_DEPTH) {
                return NULL;  /* 软链接循环，返回NULL */
            }
            symlink_depth++;

            const char *target = (const char *)current->private_data;
            char resolved_path[VFS_MAX_PATH];

            /* 如果目标是绝对路径，从根开始；否则相对于当前目录 */
            if (target[0] == '/') {
                /* 绝对路径：重置到根，然后解析剩余路径 */
                strncpy(resolved_path, target, VFS_MAX_PATH - 1);
                resolved_path[VFS_MAX_PATH - 1] = '\0';
                /* 追加剩余路径 */
                if (*path) {
                    strncat(resolved_path, "/", VFS_MAX_PATH - strlen(resolved_path) - 1);
                    strncat(resolved_path, path, VFS_MAX_PATH - strlen(resolved_path) - 1);
                }
                current = vfs_root;
                path = resolved_path;
                /* 跳过开头的斜杠 */
                while (*path == '/') path++;
            } else {
                /* 相对路径：相对于当前目录的父目录 */
                vfs_node_t *parent = current->parent;
                if (!parent) parent = vfs_root;

                /* 构造完整路径：父目录路径 + 目标 + 剩余路径 */
                /* 简化：直接递归解析目标路径（相对于父目录） */
                strncpy(resolved_path, target, VFS_MAX_PATH - 1);
                resolved_path[VFS_MAX_PATH - 1] = '\0';
                if (*path) {
                    strncat(resolved_path, "/", VFS_MAX_PATH - strlen(resolved_path) - 1);
                    strncat(resolved_path, path, VFS_MAX_PATH - strlen(resolved_path) - 1);
                }

                /* 从父目录开始解析 */
                current = parent;
                path = resolved_path;
            }
        }
    }

    return current;
}

int vfs_mount(const char *path, vfs_node_t *root)
{
    vfs_node_t *mount_point = vfs_find(path);
    if (!mount_point) {
        vga_printf("[VFS] mount point missing: %s\n", path);
        return -1;
    }

    /* 将挂载点的子节点替换为新文件系统的根 */
    mount_point->children = root->children;
    mount_point->ops = root->ops;
    mount_point->private_data = root->private_data;
    mount_point->size = root->size;

    /* 更新子节点的parent */
    vfs_node_t *child = mount_point->children;
    while (child) {
        child->parent = mount_point;
        child = child->next;
    }

    vga_printf("[VFS] mount fs to %s\n", path);
    return 0;
}

/* ----------------------------------------------------------------------------
 * 文件描述符操作
 * -------------------------------------------------------------------------- */

int vfs_open(const char *path, int flags)
{
    vfs_node_t *node = vfs_find(path);
    if (!node) return -1;

    /* 找空闲文件描述符 */
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            fd_table[i].node = node;
            fd_table[i].flags = flags;
            fd_table[i].position = (flags & VFS_O_APPEND) ? node->size : 0;

            if (node->ops && node->ops->open) {
                node->ops->open(node, flags);
            }
            return i;
        }
    }
    return -1;
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;

    vfs_node_t *node = fd_table[fd].node;
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }

    fd_table[fd].used = 0;
    fd_table[fd].node = NULL;
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t size)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;

    vfs_fd_t *f = &fd_table[fd];
    vfs_node_t *node = f->node;

    if (!node || !node->ops || !node->ops->read) return -1;

    int ret = node->ops->read(node, f->position, buf, size);
    if (ret > 0) f->position += ret;
    return ret;
}

int vfs_write(int fd, const void *buf, uint32_t size)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;

    vfs_fd_t *f = &fd_table[fd];
    vfs_node_t *node = f->node;

    if (!node || !node->ops || !node->ops->write) return -1;

    int ret = node->ops->write(node, f->position, buf, size);
    if (ret > 0) {
        f->position += ret;
        if (f->position > node->size) node->size = f->position;
    }
    return ret;
}

int vfs_lseek(int fd, int32_t offset, int whence)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;

    vfs_fd_t *f = &fd_table[fd];
    int32_t new_pos;

    switch (whence) {
        case VFS_SEEK_SET:
            new_pos = offset;
            break;
        case VFS_SEEK_CUR:
            new_pos = f->position + offset;
            break;
        case VFS_SEEK_END:
            new_pos = f->node->size + offset;
            break;
        default:
            return -1;
    }

    if (new_pos < 0) return -1;
    f->position = new_pos;
    return new_pos;
}

int vfs_fsize(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;
    return fd_table[fd].node->size;
}

/* ----------------------------------------------------------------------------
 * 目录操作
 * -------------------------------------------------------------------------- */

int vfs_readdir(const char *path, vfs_dirent_t *buf, int max_entries)
{
    vfs_node_t *dir = vfs_find(path);
    if (!dir || dir->type != VFS_DIRECTORY) return -1;

    int count = 0;
    vfs_node_t *child = dir->children;
    while (child && count < max_entries) {
        strncpy(buf[count].name, child->name, VFS_MAX_NAME - 1);
        buf[count].type = child->type;
        buf[count].size = child->size;
        count++;
        child = child->next;
    }
    return count;
}

/* === V0.40 新增：完整文件操作 === */

static void split_path(const char *path, char *parent, char *name)
{
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (!last_slash) {
        strcpy(parent, "/");
        strcpy(name, path);
        return;
    }
    int plen = last_slash - path;
    if (plen == 0) strcpy(parent, "/");
    else { strncpy(parent, path, plen); parent[plen] = '\0'; }
    strcpy(name, last_slash + 1);
}

static int remove_child(vfs_node_t *parent, vfs_node_t *child)
{
    if (!parent || !child) return -1;
    if (parent->children == child) { parent->children = child->next; return 0; }
    vfs_node_t *c = parent->children;
    while (c && c->next) {
        if (c->next == child) { c->next = child->next; return 0; }
        c = c->next;
    }
    return -1;
}

int vfs_mkdir(const char *path)
{
    if (!path || !*path || vfs_find(path)) return -1;
    char pp[VFS_MAX_PATH], name[VFS_MAX_NAME];
    split_path(path, pp, name);
    vfs_node_t *parent = vfs_find(pp);
    if (!parent || parent->type != VFS_DIRECTORY) return -1;
    vfs_node_t *dir = vfs_create_node(name, VFS_DIRECTORY);
    if (!dir) return -1;
    vfs_add_child(parent, dir);
    return 0;
}

int vfs_rmdir(const char *path)
{
    vfs_node_t *node = vfs_find(path);
    if (!node || node->type != VFS_DIRECTORY || node->children || node == vfs_root) return -1;
    remove_child(node->parent, node);
    kfree(node);
    return 0;
}

int vfs_unlink(const char *path)
{
    vfs_node_t *node = vfs_find(path);
    if (!node || node->type == VFS_DIRECTORY) return -1;
    remove_child(node->parent, node);
    kfree(node);
    return 0;
}

int vfs_rename(const char *old_path, const char *new_path)
{
    vfs_node_t *node = vfs_find(old_path);
    if (!node || vfs_find(new_path)) return -1;
    char np[VFS_MAX_PATH], name[VFS_MAX_NAME];
    split_path(new_path, np, name);
    vfs_node_t *parent = vfs_find(np);
    if (!parent || parent->type != VFS_DIRECTORY) return -1;
    remove_child(node->parent, node);
    strncpy(node->name, name, VFS_MAX_NAME - 1);
    vfs_add_child(parent, node);
    /* ext2持久化 */
    extern int ext2_path_rename(const char *old_path, const char *new_path);
    ext2_path_rename(old_path, new_path);
    return 0;
}

int vfs_stat(const char *path, vfs_stat_t *buf)
{
    vfs_node_t *node = vfs_find(path);
    if (!node || !buf) return -1;
    buf->inode = node->inode_num; buf->type = node->type;
    buf->size = node->size; buf->permissions = node->permissions;
    buf->uid = node->uid; buf->gid = node->gid; buf->links = node->links;
    return 0;
}

int vfs_fstat(int fd, vfs_stat_t *buf)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used || !buf) return -1;
    vfs_node_t *node = fd_table[fd].node;
    buf->inode = node->inode_num; buf->type = node->type;
    buf->size = node->size; buf->permissions = node->permissions;
    buf->uid = node->uid; buf->gid = node->gid; buf->links = node->links;
    return 0;
}

int vfs_access(const char *path) { return vfs_find(path) ? 0 : -1; }

int vfs_dup(int oldfd)
{
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS || !fd_table[oldfd].used) return -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!fd_table[i].used) { fd_table[i] = fd_table[oldfd]; return i; }
    }
    return -1;
}

int vfs_dup2(int oldfd, int newfd)
{
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS || !fd_table[oldfd].used) return -1;
    if (newfd < 0 || newfd >= VFS_MAX_FDS) return -1;
    if (fd_table[newfd].used) fd_table[newfd].used = 0;
    fd_table[newfd] = fd_table[oldfd];
    return newfd;
}

int vfs_readdir_fd(int fd, vfs_dirent_t *buf, int max_entries)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;
    vfs_node_t *dir = fd_table[fd].node;
    if (!dir || dir->type != VFS_DIRECTORY) return -1;
    int count = 0;
    vfs_node_t *child = dir->children;
    while (child && count < max_entries) {
        strncpy(buf[count].name, child->name, VFS_MAX_NAME - 1);
        buf[count].type = child->type;
        buf[count].size = child->size;
        count++;
        child = child->next;
    }
    return count;
}

/* === V8.21 新增：软链接、硬链接、文件权限 === */

/* 创建软链接 */
int vfs_symlink(const char *target, const char *linkpath)
{
    if (!target || !linkpath) return -1;

    /* 拆分路径 */
    char parent_path[VFS_MAX_PATH], name[VFS_MAX_NAME];
    split_path(linkpath, parent_path, name);

    /* 查找父目录 */
    vfs_node_t *parent = vfs_find(parent_path);
    if (!parent || parent->type != VFS_DIRECTORY) return -1;

    /* 检查是否已存在 */
    if (vfs_find(linkpath)) return -1;

    /* 创建软链接节点 */
    vfs_node_t *node = vfs_create_node(name, VFS_SYMLINK);
    if (!node) return -1;

    /* 存储目标路径（使用private_data） */
    uint32_t target_len = strlen(target) + 1;
    node->private_data = kmalloc(target_len);
    if (!node->private_data) {
        kfree(node);
        return -1;
    }
    memcpy(node->private_data, target, target_len);
    node->size = target_len - 1;  /* 不包括终止符 */
    node->permissions = 0777;     /* 软链接权限通常为0777 */

    /* 添加到父目录 */
    vfs_add_child(parent, node);

    return 0;
}

/* 读取软链接目标 */
int vfs_readlink(const char *path, char *buf, uint32_t bufsiz)
{
    if (!path || !buf || bufsiz == 0) return -1;

    vfs_node_t *node = vfs_find(path);
    if (!node) return -1;

    /* 不跟随软链接，直接读取 */
    if (node->type != VFS_SYMLINK) return -1;

    if (!node->private_data) return -1;

    const char *target = (const char *)node->private_data;
    uint32_t len = strlen(target);
    if (len >= bufsiz) len = bufsiz - 1;
    memcpy(buf, target, len);
    buf[len] = '\0';

    return len;
}

/* lstat（不跟随软链接） */
int vfs_lstat(const char *path, vfs_stat_t *buf)
{
    if (!path || !buf) return -1;

    vfs_node_t *node = vfs_find(path);
    if (!node) return -1;

    /* 不跟随软链接，直接返回节点状态 */
    buf->inode = node->inode_num;
    buf->type = node->type;
    buf->size = node->size;
    buf->permissions = node->permissions;
    buf->uid = node->uid;
    buf->gid = node->gid;
    buf->links = node->links;

    return 0;
}

/* 创建硬链接 */
int vfs_link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath) return -1;

    /* 查找原文件 */
    vfs_node_t *node = vfs_find(oldpath);
    if (!node) return -1;

    /* 目录不能创建硬链接 */
    if (node->type == VFS_DIRECTORY) return -1;

    /* 拆分新路径 */
    char parent_path[VFS_MAX_PATH], name[VFS_MAX_NAME];
    split_path(newpath, parent_path, name);

    /* 查找父目录 */
    vfs_node_t *parent = vfs_find(parent_path);
    if (!parent || parent->type != VFS_DIRECTORY) return -1;

    /* 检查是否已存在 */
    if (vfs_find(newpath)) return -1;

    /* 增加链接计数 */
    node->links++;

    /* 创建新目录项（指向同一个inode） */
    vfs_node_t *new_node = vfs_create_node(name, node->type);
    if (!new_node) {
        node->links--;
        return -1;
    }

    /* 复制节点属性（共享同一个文件数据） */
    new_node->size = node->size;
    new_node->inode_num = node->inode_num;
    new_node->permissions = node->permissions;
    new_node->uid = node->uid;
    new_node->gid = node->gid;
    new_node->links = node->links;
    new_node->ops = node->ops;
    new_node->private_data = node->private_data;  /* 共享文件数据 */

    /* 添加到父目录 */
    vfs_add_child(parent, new_node);

    return 0;
}

/* 修改文件权限 */
int vfs_chmod(const char *path, uint32_t mode)
{
    if (!path) return -1;

    vfs_node_t *node = vfs_find(path);
    if (!node) return -1;

    node->permissions = mode & 07777;  /* 只保留权限位 */

    return 0;
}

/* 修改文件描述符的权限 */
int vfs_fchmod(int fd, uint32_t mode)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;

    vfs_node_t *node = fd_table[fd].node;
    if (!node) return -1;

    node->permissions = mode & 07777;

    return 0;
}

/* 修改文件所有者 */
int vfs_chown(const char *path, uint32_t uid, uint32_t gid)
{
    if (!path) return -1;

    vfs_node_t *node = vfs_find(path);
    if (!node) return -1;

    if (uid != (uint32_t)-1) node->uid = uid;
    if (gid != (uint32_t)-1) node->gid = gid;

    return 0;
}

/* 修改文件描述符的所有者 */
int vfs_fchown(int fd, uint32_t uid, uint32_t gid)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;

    vfs_node_t *node = fd_table[fd].node;
    if (!node) return -1;

    if (uid != (uint32_t)-1) node->uid = uid;
    if (gid != (uint32_t)-1) node->gid = gid;

    return 0;
}
