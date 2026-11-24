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
#include "ext2.h"   /* 懒加载要用 ext2_inode_t / ext2_lookup */

static vfs_node_t *vfs_root = NULL;
/* 2026-11-21: 根节点不再从堆上分配。
 * 抓到过 vfs_root == NULL（整个 VFS 不可用，写阶段 13 项全红）和
 * vfs_root->type 偶发变 0（应是 VFS_DIRECTORY=1）两种现象，都与堆有关。
 * 根节点是 VFS 的地基，不该依赖任何分配器 —— 放静态区。 */
static vfs_node_t vfs_root_storage;

/* OBS-08: vnode 自证魔数。'VNOD' 这四位是随便挑的常量，只要稳定即可。 */
#define VFS_NODE_MAGIC 0x564E4F44u
static vfs_fd_t fd_table[VFS_MAX_FDS];
static uint32_t next_inode = 1;

/* P3: VFS 访问能力位。
 * 与 shared/capability 的“资源句柄”不同，文件路径不是固定资源 ID，无法为每个
 * 路径 mint 一个能力对象；这里用 uid + 可写根集合作为 VFS 层的能力剖面：
 *   root(uid 0)      : 全部权限
 *   普通用户(uid>0) : 读放行；写/建/删仅限自己的可写根（/data /tmp /app /home）
 * 再叠加 inode 的 POSIX 权限位。两层都过才允许。
 */
#define VFS_REQ_READ   0x1
#define VFS_REQ_WRITE  0x2
#define VFS_REQ_EXEC   0x4
#define VFS_REQ_CREATE (VFS_REQ_WRITE | VFS_REQ_EXEC)
#define VFS_REQ_DELETE (VFS_REQ_WRITE | VFS_REQ_EXEC)

static void split_path(const char *path, char *parent, char *name);  /* 定义在本文件后部 */

static int vfs_current_uid(void)
{
    extern int hepu_current_uid(void);
    int uid = hepu_current_uid();
    return uid < 0 ? 0 : uid;
}

static int vfs_user_writable_path(const char *path)
{
    if (!path) return 0;
    if (strcmp(path, "/data") == 0 || strncmp(path, "/data/", 6) == 0) return 1;
    if (strcmp(path, "/tmp") == 0 || strncmp(path, "/tmp/", 5) == 0) return 1;
    if (strcmp(path, "/app") == 0 || strncmp(path, "/app/", 5) == 0) return 1;
    if (strcmp(path, "/home") == 0 || strncmp(path, "/home/", 6) == 0) return 1;
    return 0;
}

static int vfs_cap_check(const char *path, int req)
{
    if (vfs_current_uid() == 0) return 1;  /* root */
    if (req == VFS_REQ_READ) return 1;
    return vfs_user_writable_path(path);
}

static int vfs_mode_check(vfs_node_t *node, int req)
{
    if (!node) return 0;
    int uid = vfs_current_uid();
    if (uid == 0) return 1;

    uint32_t bits = node->permissions & 0777;
    if (node->uid == (uint32_t)uid) bits >>= 6;
    else if (node->gid == (uint32_t)uid) bits >>= 3;  /* 简化：gid=uid */
    if ((req & VFS_REQ_READ) && !(bits & 4)) return 0;
    if ((req & VFS_REQ_WRITE) && !(bits & 2)) return 0;
    if ((req & VFS_REQ_EXEC) && !(bits & 1)) return 0;
    return 1;
}

/* 对“最终对象”检查：能力剖面 + inode 权限位 */
static int vfs_permission_check(const char *path, vfs_node_t *node, int req)
{
    if (!vfs_cap_check(path, req)) return VFS_EACCES;
    if (!node) return VFS_ENOENT;
    if (!vfs_mode_check(node, req)) return VFS_EACCES;
    return 0;
}

/* 对“父目录”检查：建/删/改名需要父目录的写+执行位 */
static int vfs_parent_check(const char *path)
{
    char pp[VFS_MAX_PATH], nm[VFS_MAX_NAME];
    if (!path || !path[0]) return VFS_EINVAL;
    /* 能力剖面先判：普通用户对 /system 的写请求即使父目录不存在，
     * 也应返回 EACCES（S3 的验收语义），不能先暴露 ENOENT。 */
    if (!vfs_cap_check(path, VFS_REQ_CREATE)) return VFS_EACCES;
    split_path(path, pp, nm);
    if (!nm[0]) return VFS_EINVAL;
    vfs_node_t *parent = vfs_find(pp);
    if (!parent) return VFS_ENOENT;
    if (parent->type != VFS_DIRECTORY) return VFS_ENOTDIR;
    return vfs_permission_check(pp, parent, VFS_REQ_CREATE);
}

/* ext2 后端错误码 → VFS 对外错误码。ext2 的 -1 无法细分，按 EIO；
 * 分配失败已改为明确返回 -28(ENOSPC)。 */
static int vfs_backend_error(int rc)
{
    if (rc == VFS_ENOSPC || rc == -28) return VFS_ENOSPC;
    if (rc == -3) return VFS_EACCES;
    if (rc == VFS_ENOENT || rc == -2) return VFS_ENOENT;
    if (rc == VFS_EEXIST || rc == -17) return VFS_EEXIST;
    return VFS_EIO;
}

static int vfs_enospc_inject = 0;
void vfs_test_inject_enospc(int on) { vfs_enospc_inject = on ? 1 : 0; }

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
    if (vfs_root) return;   /* 幂等：可能被 vfs_find 的兜底重入 */

    /* 根目录用静态结构，不走 kmalloc —— 早期堆不可用、堆被踩都不影响地基 */
    memset(&vfs_root_storage, 0, sizeof(vfs_root_storage));
    vfs_root_storage.magic = VFS_NODE_MAGIC;
    strncpy(vfs_root_storage.name, "/", VFS_MAX_NAME - 1);
    vfs_root_storage.type = VFS_DIRECTORY;
    vfs_root_storage.inode_num = 0;
    vfs_root_storage.permissions = 0755;
    vfs_root_storage.links = 1;
    vfs_root = &vfs_root_storage;

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
        fd_table[i].path[0] = '\0';
    }
    return 0;
}

vfs_node_t *vfs_create_node(const char *name, vfs_node_type_t type)
{
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    memset(node, 0, sizeof(vfs_node_t));
    node->magic = VFS_NODE_MAGIC;
    strncpy(node->name, name, VFS_MAX_NAME - 1);
    node->type = type;
    node->inode_num = next_inode++;
    node->permissions = 0755;
    node->uid = (uint32_t)vfs_current_uid();
    node->gid = node->uid;
    node->links = 1;

    return node;
}

/* OBS-08 结构自证: 遍历前先判"这个指针像不像一个 vnode"。
 * 2026-11-19 取证结论: 崩溃不是 VFS 自己写坏内存, 而是某处把一块未初始化的
 * 内存当成了结构指针(poison 抓到 0x82630000/0xBBBBB000 这类值)。
 * 这里不追写入者, 只保证"坏指针进来也不崩": 地址不在内核堆区、没对齐、
 * name 在 VFS_MAX_NAME 内没有 NUL 收尾 —— 任一不满足就跳过并留一条日志。 */
static int vfs_node_sane(const vfs_node_t *n)
{
    uintptr_t a = (uintptr_t)n;
    int i;

    /* 2026-11-21: 根节点已改为静态结构，位于 BSS(32 位内核约 0x00100000)，
     * 低于下面的堆地址下限。它是地基，恒有效，直接放行 —— 否则
     * vfs_add_child 里对 parent 的这一关会把 root 挡掉，子节点一个都挂不上。 */
    if ((vfs_node_t *)n == vfs_root) return 1;
    if (a < 0x04000000u || a > 0x0FFFFFFFu) return 0;   /* 内核直接用到的地址区间 */
    if (a & 3u) return 0;                              /* 结构指针至少 4 字节对齐 */
    for (i = 0; i < VFS_MAX_NAME; i++) {
        if (n->name[i] == '\0') return 1;             /* name 必须收得住 */
    }
    return 0;
}

static void vfs_insane_warn(void);

/* 2026-11-19: 比"像不像 vnode"更硬一层 —— 魔数对不对。
 * 地址检查只能抓野指针; 魔数检查能抓"这块内存曾经是个合法节点、后来被
 * 别的东西覆盖了", 并且能把节点地址和残留的名字打出来。 */
static int vfs_node_verify(const vfs_node_t *n)
{
    if (!vfs_node_sane(n)) { vfs_insane_warn(); return 0; }
    if (n->magic != VFS_NODE_MAGIC) {
        extern void serial_puts(uint16_t, const char *);
        static int magic_warned;
        if (!magic_warned) {
            magic_warned = 1;
            serial_puts(0x3F8, "[VFS] node magic mismatch (OBS-08, node overwritten)\n");
        }
        return 0;
    }
    return 1;
}

static void vfs_insane_warn(void)
{
    static int warned;
    extern void serial_puts(uint16_t, const char *);
    if (warned) return;
    warned = 1;
    serial_puts(0x3F8, "[VFS] insane node pointer skipped (OBS-08 guard)\n");
}

void vfs_add_child(vfs_node_t *parent, vfs_node_t *child)
{
    if (!parent || !child) return;
    if (!vfs_node_sane(parent) || !vfs_node_sane(child)) {
        vfs_insane_warn();
        return;
    }
    /* 进树即盖章: 不管节点是哪个子系统造的, 挂进来就带上魔数 */
    child->magic = VFS_NODE_MAGIC;
    child->parent = parent;
    child->next = parent->children;
    parent->children = child;
}

vfs_node_t *vfs_get_root(void)
{
    return vfs_root;
}

/* 懒加载的最近一次失败原因，排查用（0=成功，负数=各分支） */
int vfs_lazy_rc = -99;

/* 2026-11-21: 按需加载 —— 内存树里找不到某个名字时，从 ext2 盘上查一次。
 *
 * 为什么必须有这一步：ext2_mount 只扫根目录一层，子目录的 children 是空的。
 * 没有懒加载的话，"重启后按路径访问盘上已有的文件"根本不成立
 * （vfs_find("/docs/readme.txt") 会在第二级就返回 NULL）。
 *
 * 只在 ext2 已挂载、父节点带真实 inode 号、且父节点确实是目录时才动手；
 * 其它情况（initrd 内存 FS、parent 没有 inode 号）一律返回 NULL，保持原语义。
 */
static vfs_node_t *vfs_lazy_lookup(vfs_node_t *parent, const char *name)
{
    extern int ext2_mounted;
    extern int ext2_read_inode(uint32_t, ext2_inode_t *);
    extern int ext2_lookup(ext2_inode_t *, const char *, uint32_t *);
    extern vfs_file_ops_t ext2_file_ops;

    vfs_lazy_rc = 1;
    if (!ext2_mounted || !parent || !name || !*name) return NULL;
    /* 注意：vfs_root 是第一个建的节点，next_inode 从 0 起，所以它的 inode_num
     * 恰好是 0 —— 不能拿==0 当"没有 inode 号"的判据，根节点是豁免的
     * （下面会把它强制对到 EXT2_ROOT_INODE）。 */
    if (parent->type != VFS_DIRECTORY) return NULL;
    if (parent->inode_num == 0 && parent != vfs_root) return NULL;

    /* 根挂载点必须对到盘上的 inode 2。vfs_root 不是 ext2_mount 建的
     * （ext2_mount 在整个内核里没有调用点），它的 inode_num 是 vfs_create_node
     * 给的自增假号，直接拿去读盘只会读到无关 inode。 */
    uint32_t pino = parent->inode_num;
    if (parent == vfs_root) pino = EXT2_ROOT_INODE;
    if (pino == 0) return NULL;

    vfs_lazy_rc = 2;
    ext2_inode_t din;
    if (ext2_read_inode(pino, &din) != 0) return NULL;
    vfs_lazy_rc = 3;
    if ((din.i_mode & 0xF000) != 0x4000) return NULL;   /* 不是目录 */

    vfs_lazy_rc = 4;
    uint32_t ino = 0;
    if (ext2_lookup(&din, name, &ino) != 0 || ino == 0) return NULL;

    vfs_lazy_rc = 5;
    ext2_inode_t ci;
    if (ext2_read_inode(ino, &ci) != 0) return NULL;

    uint32_t m = ci.i_mode & 0xF000;
    vfs_node_type_t t = VFS_REGULAR;
    if (m == 0x4000) t = VFS_DIRECTORY;
    else if (m == 0xA000) t = VFS_SYMLINK;

    vfs_lazy_rc = 6;
    vfs_node_t *node = vfs_create_node(name, t);
    if (!node) return NULL;
    node->inode_num = ino;
    node->size = ci.i_size;
    node->permissions = ci.i_mode & 0x0FFF;
    node->private_data = (void *)(uintptr_t)ino;
    if (t != VFS_DIRECTORY) node->ops = &ext2_file_ops;
    vfs_add_child(parent, node);
    vfs_lazy_rc = 0;
    return node;
}

vfs_node_t *vfs_find(const char *path)
{
    if (!path) return NULL;
    if (!vfs_root) {
        /* vfs_init 没跑到 / 根节点没建起来 —— 这里补一次并留证据 */
        extern void serial_puts(uint16_t, const char *);
        static int late_warned;
        if (!late_warned) {
            late_warned = 1;
            serial_puts(0x3F8, "[VFS] ROOT NULL -> late init\n");
        }
        vfs_init();
        if (!vfs_root) return NULL;
    }
    /* OBS-08: 根节点以前没人查 —— 子节点遍历有 verify，根节点却是裸的。
     * 2026-11-21 抓到的现象：偶发 vfs_root->type 变成 0（应是 VFS_DIRECTORY=1），
     * 而 inode_num 仍是 0、ext2 正常挂着。这里补一道自证。 */
    if (vfs_root->magic != VFS_NODE_MAGIC || vfs_root->type != VFS_DIRECTORY) {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        static int root_warned;
        if (!root_warned) {
            root_warned = 1;
            serial_puts(0x3F8, "[VFS] ROOT CORRUPT magic=");
            serial_puthex(0x3F8, vfs_root->magic);
            serial_puts(0x3F8, " type=");
            serial_puthex(0x3F8, (uint32_t)vfs_root->type);
            serial_puts(0x3F8, " ino=");
            serial_puthex(0x3F8, vfs_root->inode_num);
            serial_puts(0x3F8, " name0=");
            serial_puthex(0x3F8, (uint32_t)vfs_root->name[0]);
            serial_puts(0x3F8, " size=");
            serial_puthex(0x3F8, (uint32_t)vfs_root->size);
            serial_puts(0x3F8, "\n");
        }
        return NULL;
    }
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
            if (!vfs_node_verify(child)) { break; }
            if (strcmp(child->name, component) == 0) {
                found = 1;
                break;
            }
            child = child->next;
        }
        if (!found) {
            /* 内存树没有 → 问一次盘（懒加载），仍没有才认输 */
            child = vfs_lazy_lookup(current, component);
            if (!child) return NULL;
        }
        current = child;

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

/* split_path 定义在本文件后半段，vfs_open 要用它，先前置声明 */
static void split_path(const char *path, char *parent, char *name);

int vfs_open(const char *path, int flags)
{
    if (!path || !path[0]) return VFS_EINVAL;

    vfs_node_t *node = vfs_find(path);
    if (!node) {
        if (!(flags & VFS_O_CREAT)) return VFS_ENOENT;

        /* O_CREAT：先检查父目录的写+执行权限（能力剖面 + POSIX 位），
         * 再落 ext2。这样普通用户即使路径不存在也会先拿到 EACCES，
         * 而不是先让 ext2 返回一个含义模糊的 -1。 */
        int prc = vfs_parent_check(path);
        if (prc != 0) return prc;

        {
            extern int ext2_mounted;
            extern int ext2_path_create(const char *, uint16_t);
            if (!ext2_mounted) return VFS_EIO;
            int ino = ext2_path_create(path, 0x81A4);
            if (ino < 0) return vfs_backend_error(ino);
            char pp[VFS_MAX_PATH], nm[VFS_MAX_NAME];
            split_path(path, pp, nm);
            vfs_node_t *parent = vfs_find(pp);
            if (!parent || parent->type != VFS_DIRECTORY) return VFS_ENOENT;
            node = vfs_create_node(nm, VFS_REGULAR);
            if (!node) return VFS_ENOMEM;
            node->inode_num = (uint32_t)ino;
            node->permissions = 0644;
            node->uid = (uint32_t)vfs_current_uid();
            node->gid = node->uid;
            {
                extern vfs_file_ops_t ext2_file_ops;
                node->ops = &ext2_file_ops;
            }
            vfs_add_child(parent, node);
        }
    } else {
        int acc = flags & 3;
        int req = (acc == VFS_O_WRONLY || acc == VFS_O_RDWR ||
                   (flags & VFS_O_TRUNC)) ? VFS_REQ_WRITE : VFS_REQ_READ;
        int rc = vfs_permission_check(path, node, req);
        if (rc != 0) return rc;
        if (node->type == VFS_DIRECTORY && req != VFS_REQ_READ)
            return VFS_EISDIR;
    }

    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            fd_table[i].node = node;
            fd_table[i].flags = flags;
            fd_table[i].position = (flags & VFS_O_APPEND) ? node->size : 0;
            strncpy(fd_table[i].path, path, VFS_MAX_PATH - 1);
            fd_table[i].path[VFS_MAX_PATH - 1] = '\0';

            if (node->ops && node->ops->open) {
                node->ops->open(node, flags);
            }
            return i;
        }
    }
    return VFS_EMFILE;
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
    fd_table[fd].path[0] = '\0';
    return 0;
}

int vfs_read(int fd, void *buf, uint32_t size)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return -1;

    vfs_fd_t *f = &fd_table[fd];
    vfs_node_t *node = f->node;

    if (!node || !node->ops || !node->ops->read) return -1;
    /* 0/1/2 是控制台/标准流，不按路径能力检查；其余 fd 复查一次，
     * 防止“打开后切换用户”绕过 open 时的检查。 */
    if (fd >= 3) {
        if (!vfs_cap_check(f->path, VFS_REQ_READ)) return VFS_EACCES;
        if (!vfs_mode_check(node, VFS_REQ_READ)) return VFS_EACCES;
    }

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
    if (fd >= 3) {
        if (!vfs_cap_check(f->path, VFS_REQ_WRITE)) return VFS_EACCES;
        if (!vfs_mode_check(node, VFS_REQ_WRITE)) return VFS_EACCES;
    }

    int ret = node->ops->write(node, f->position, buf, size);
    if (ret > 0) {
        f->position += ret;
        if (f->position > node->size) node->size = f->position;
    }
    return ret;
}

/* 2026-11-19: VFS 写只进块缓存，不落盘（ext2_vfs_write → ext2_write_data
 * → cache_dirty，真正写盘靠 ide_ext2_sync）。这里补一个显式同步入口，
 * 给"写完必须落盘"的调用方用；不自动在每次 write 后调，否则每写一次全盘同步。 */
int vfs_sync(void)
{
    extern int ide_ext2_sync(void);
    return ide_ext2_sync();
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
    /* 与 vfs_readdir_fd 同口径：ext2 目录挂载时只填了一层，子目录的 children
     * 是空的，按路径列目录时要直接问 ext2 要真实条目。 */
    if (dir->private_data && !dir->children) {
        extern int ext2_readdir_ino(uint32_t ino, vfs_dirent_t *buf, int max_entries);
        int n = ext2_readdir_ino((uint32_t)(uintptr_t)dir->private_data, buf, max_entries);
        if (n >= 0) return n;
    }

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
    if (!path || !*path) return VFS_EINVAL;
    if (vfs_find(path)) return VFS_EEXIST;

    int prc = vfs_parent_check(path);
    if (prc != 0) return prc;
    if (vfs_enospc_inject) return VFS_ENOSPC;

    /* 先落 ext2 再建节点：落盘失败就整体失败，不留“看起来成功”的假状态。 */
    int ino = 0;
    {
        extern int ext2_mounted;
        extern int ext2_path_mkdir(const char *, uint16_t);
        if (ext2_mounted) {
            ino = ext2_path_mkdir(path, 0x41ED);
            if (ino < 0) {
                vga_puts("[VFS] mkdir: ext2 backend failed\n");
                return vfs_backend_error(ino);
            }
        }
    }
    char pp[VFS_MAX_PATH], name[VFS_MAX_NAME];
    split_path(path, pp, name);
    vfs_node_t *parent = vfs_find(pp);
    if (!parent || parent->type != VFS_DIRECTORY) return VFS_ENOENT;
    vfs_node_t *dir = vfs_create_node(name, VFS_DIRECTORY);
    if (!dir) return VFS_ENOMEM;
    dir->permissions = 0755;
    dir->uid = (uint32_t)vfs_current_uid();
    dir->gid = dir->uid;
    if (ino > 0) dir->inode_num = (uint32_t)ino;
    vfs_add_child(parent, dir);
    return 0;
}

int vfs_rmdir(const char *path)
{
    vfs_node_t *node = vfs_find(path);
    if (!node) return VFS_ENOENT;
    if (node->type != VFS_DIRECTORY) return VFS_ENOTDIR;
    if (node == vfs_root) return VFS_EBUSY;
    if (node->children) return VFS_ENOTEMPTY;

    int prc = vfs_parent_check(path);
    if (prc != 0) return prc;

    /* 空目录才删；先落 ext2，成功再摘内存节点 */
    {
        extern int ext2_mounted;
        extern int ext2_path_unlink_v2(const char *);
        if (ext2_mounted) {
            int rc = ext2_path_unlink_v2(path);
            if (rc < 0) {
                vga_puts("[VFS] rmdir: ext2 backend failed\n");
                return vfs_backend_error(rc);
            }
        }
    }
    remove_child(node->parent, node);
    kfree(node);
    return 0;
}

int vfs_unlink(const char *path)
{
    vfs_node_t *node = vfs_find(path);
    if (!node) return VFS_ENOENT;
    if (node->type == VFS_DIRECTORY) return VFS_EISDIR;

    int prc = vfs_parent_check(path);
    if (prc != 0) return prc;

    /* 先落 ext2 删除，成功再摘内存节点 */
    {
        extern int ext2_mounted;
        extern int ext2_path_unlink_v2(const char *);
        if (ext2_mounted) {
            int rc = ext2_path_unlink_v2(path);
            if (rc < 0) {
                vga_puts("[VFS] unlink: ext2 backend failed\n");
                return vfs_backend_error(rc);
            }
        }
    }
    remove_child(node->parent, node);
    kfree(node);
    return 0;
}

int vfs_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) return VFS_EINVAL;
    if (strcmp(old_path, new_path) == 0) return 0;

    vfs_node_t *node = vfs_find(old_path);
    if (!node) return VFS_ENOENT;
    if (vfs_find(new_path)) return VFS_EEXIST;

    int prc = vfs_parent_check(old_path);
    if (prc != 0) return prc;
    prc = vfs_parent_check(new_path);
    if (prc != 0) return prc;

    char np[VFS_MAX_PATH], name[VFS_MAX_NAME];
    split_path(new_path, np, name);
    vfs_node_t *parent = vfs_find(np);
    if (!parent || parent->type != VFS_DIRECTORY) return VFS_ENOENT;

    /* 先落盘，成功再改内存树——盘上 rename 失败时不允许内存树先变。 */
    {
        extern int ext2_mounted;
        extern int ext2_path_rename(const char *, const char *);
        if (ext2_mounted) {
            int rc = ext2_path_rename(old_path, new_path);
            if (rc < 0) {
                vga_puts("[VFS] rename: ext2 backend failed\n");
                return vfs_backend_error(rc);
            }
        }
    }
    remove_child(node->parent, node);
    strncpy(node->name, name, VFS_MAX_NAME - 1);
    node->name[VFS_MAX_NAME - 1] = 0;
    vfs_add_child(parent, node);
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

int vfs_access(const char *path)
{
    if (!path || !path[0]) return VFS_EINVAL;
    return vfs_find(path) ? 0 : VFS_ENOENT;
}

/* P3: fd→路径映射。失败返回 VFS_EBADF；成功 0。 */
int vfs_fd_get_path(int fd, char *out, uint32_t outsz)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return VFS_EBADF;
    if (!out || outsz == 0) return VFS_EINVAL;
    strncpy(out, fd_table[fd].path, outsz - 1);
    out[outsz - 1] = '\0';
    return 0;
}

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
    /* 来自 ext2 的目录（带 inode 号）且挂载时没填过子节点 —— 直接问 ext2，
     * 别只信挂载时那一层快照，否则 ls 子目录永远是空的。 */
    if (dir->private_data && !dir->children) {
        extern int ext2_readdir_ino(uint32_t ino, vfs_dirent_t *buf, int max_entries);
        int n = ext2_readdir_ino((uint32_t)(uintptr_t)dir->private_data, buf, max_entries);
        if (n >= 0) return n;
    }
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
    if (!path) return VFS_EINVAL;

    vfs_node_t *node = vfs_find(path);
    if (!node) return VFS_ENOENT;
    if (vfs_current_uid() != 0 && node->uid != (uint32_t)vfs_current_uid())
        return VFS_EACCES;

    node->permissions = mode & 07777;  /* 只保留权限位 */

    return 0;
}

/* 修改文件描述符的权限 */
int vfs_fchmod(int fd, uint32_t mode)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return VFS_EBADF;

    vfs_node_t *node = fd_table[fd].node;
    if (!node) return VFS_EBADF;
    if (vfs_current_uid() != 0 && node->uid != (uint32_t)vfs_current_uid())
        return VFS_EACCES;

    node->permissions = mode & 07777;

    return 0;
}

/* 修改文件所有者 */
int vfs_chown(const char *path, uint32_t uid, uint32_t gid)
{
    if (!path) return VFS_EINVAL;

    vfs_node_t *node = vfs_find(path);
    if (!node) return VFS_ENOENT;
    if (vfs_current_uid() != 0) return VFS_EACCES;

    if (uid != (uint32_t)-1) node->uid = uid;
    if (gid != (uint32_t)-1) node->gid = gid;

    return 0;
}

/* 修改文件描述符的所有者 */
int vfs_fchown(int fd, uint32_t uid, uint32_t gid)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used) return VFS_EBADF;

    vfs_node_t *node = fd_table[fd].node;
    if (!node) return VFS_EBADF;
    if (vfs_current_uid() != 0) return VFS_EACCES;

    if (uid != (uint32_t)-1) node->uid = uid;
    if (gid != (uint32_t)-1) node->gid = gid;

    return 0;
}
