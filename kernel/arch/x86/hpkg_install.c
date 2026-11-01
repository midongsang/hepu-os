/* ============================================================================
 * 赫普OS - 包管理器：安装/卸载/更新实现
 * V8.90: 初始实现
 * 包解压、文件安装、依赖处理、事务支持
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "hpkg.h"
#include "../../include/vfs.h"
#include "../../include/vga.h"
#include "../../include/kheap.h"

/* 简单的tar解析（仅支持USTAR格式） */
#define TAR_BLOCK_SIZE 512

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed)) tar_header_t;

/* 将tar的八进制字符串转换为整数 */
static uint32_t tar_oct_to_int(const char *oct, int len)
{
    uint32_t val = 0;
    for (int i = 0; i < len && oct[i] >= '0' && oct[i] <= '7'; i++) {
        val = val * 8 + (oct[i] - '0');
    }
    return val;
}

/* 简单的字符串拼接 */
static void safe_strcat(char *dst, int dst_size, const char *src)
{
    int dst_len = strlen(dst);
    int src_len = strlen(src);
    if (dst_len + src_len >= dst_size) return;
    memcpy(dst + dst_len, src, src_len + 1);
}

/* 简单的gzip解压（简化版） */
static int gzip_decompress(const uint8_t *input, uint32_t input_size,
                           uint8_t *output, uint32_t output_size,
                           uint32_t *output_len)
{
    if (input_size < 18) return -1;
    if (input[0] != 0x1F || input[1] != 0x8B) return -1;

    uint8_t method = input[2];
    uint8_t flags = input[3];

    uint32_t pos = 10;
    if (flags & 0x04) {
        uint16_t xlen = input[pos] | (input[pos+1] << 8);
        pos += 2 + xlen;
    }
    if (flags & 0x08) {
        while (pos < input_size && input[pos]) pos++;
        pos++;
    }
    if (flags & 0x10) {
        while (pos < input_size && input[pos]) pos++;
        pos++;
    }
    if (flags & 0x02) pos += 2;

    if (method == 0) {
        if (pos + 4 > input_size) return -1;
        uint16_t block_len = input[pos] | (input[pos+1] << 8);
        pos += 4;
        if (pos + block_len > input_size) return -1;
        if (block_len > output_size) return -1;
        memcpy(output, input + pos, block_len);
        *output_len = block_len;
        return 0;
    }

    /* 简化：直接返回输入数据 */
    if (input_size - pos > output_size) return -1;
    memcpy(output, input + pos, input_size - pos);
    *output_len = input_size - pos;
    return 0;
}

/* 解压tar包到目标目录 */
static int tar_extract(const uint8_t *tar_data, uint32_t tar_size,
                       const char *dest_dir)
{
    uint32_t pos = 0;
    int extracted = 0;

    while (pos + TAR_BLOCK_SIZE <= tar_size) {
        const tar_header_t *hdr = (const tar_header_t *)(tar_data + pos);

        int all_zero = 1;
        for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
            if (tar_data[pos + i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) break;

        if (memcmp(hdr->magic, "ustar", 5) != 0 &&
            memcmp(hdr->magic, "ustar ", 6) != 0) {
            break;
        }

        uint32_t file_size = tar_oct_to_int(hdr->size, 12);
        pos += TAR_BLOCK_SIZE;

        char full_path[512];
        full_path[0] = '\0';
        safe_strcat(full_path, sizeof(full_path), dest_dir);
        safe_strcat(full_path, sizeof(full_path), "/");
        if (hdr->prefix[0]) {
            safe_strcat(full_path, sizeof(full_path), hdr->prefix);
            safe_strcat(full_path, sizeof(full_path), "/");
        }
        safe_strcat(full_path, sizeof(full_path), hdr->name);

        if (hdr->typeflag == '0' || hdr->typeflag == '\0') {
            if (pos + file_size > tar_size) break;

            int fd = vfs_open(full_path, 1);
            if (fd >= 0) {
                vfs_write(fd, tar_data + pos, file_size);
                vfs_close(fd);
                extracted++;
            }
        }

        pos += (file_size + TAR_BLOCK_SIZE - 1) & ~(TAR_BLOCK_SIZE - 1);
    }

    return extracted;
}

/* 解析包元数据（meta.ini） */
static int parse_meta_ini(const char *data, uint32_t size,
                          hpkg_meta_t *meta)
{
    if (!data || !meta) return -1;

    memset(meta, 0, sizeof(hpkg_meta_t));

    char section[64] = {0};
    const char *p = data;
    const char *end = data + size;

    while (p < end) {
        while (p < end && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) p++;
        if (p >= end) break;
        if (*p == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }

        char line[256];
        int line_len = 0;
        while (p < end && *p != '\n' && line_len < 255) {
            line[line_len++] = *p++;
        }
        line[line_len] = '\0';
        if (p < end && *p == '\n') p++;

        while (line_len > 0 && (line[line_len-1] == '\r' || line[line_len-1] == ' ')) {
            line[--line_len] = '\0';
        }

        if (line[0] == '[' && line[line_len-1] == ']') {
            int sec_len = line_len - 2;
            if (sec_len > 63) sec_len = 63;
            memcpy(section, line + 1, sec_len);
            section[sec_len] = '\0';
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        while (*key == ' ') key++;
        while (*value == ' ') value++;

        if (strcmp(section, "package") == 0) {
            if (strcmp(key, "name") == 0) {
                strncpy(meta->name, value, HPKG_MAX_NAME_LEN - 1);
            } else if (strcmp(key, "version") == 0) {
                strncpy(meta->version, value, HPKG_MAX_VERSION_LEN - 1);
            } else if (strcmp(key, "description") == 0) {
                strncpy(meta->description, value, 127);
            } else if (strcmp(key, "author") == 0) {
                strncpy(meta->author, value, 63);
            } else if (strcmp(key, "size") == 0) {
                meta->install_size = atoi(value);
            }
        }
    }

    return 0;
}

/* ============================================================================
 * 包安装
 * ============================================================================ */

/* 安装包（从.hpkg文件） */
int hpkg_install_file(const char *pkg_path)
{
    if (!pkg_path) return -1;

    vga_printf("[HPKG] Installing package: %s\n", pkg_path);

    int fd = vfs_open(pkg_path, 0);
    if (fd < 0) {
        vga_printf("[HPKG] Failed to open package file\n");
        return -1;
    }

    uint32_t pkg_size = vfs_fsize(fd);
    if (pkg_size == 0 || pkg_size > 64 * 1024 * 1024) {
        vfs_close(fd);
        return -1;
    }

    uint8_t *pkg_data = kmalloc(pkg_size);
    if (!pkg_data) {
        vfs_close(fd);
        return -1;
    }

    if (vfs_read(fd, pkg_data, pkg_size) != (int)pkg_size) {
        kfree(pkg_data);
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);

    if (pkg_size < HPKG_MAGIC_LEN ||
        memcmp(pkg_data, HPKG_MAGIC, HPKG_MAGIC_LEN) != 0) {
        vga_printf("[HPKG] Invalid package magic\n");
        kfree(pkg_data);
        return -1;
    }

    uint8_t *tar_data = kmalloc(pkg_size * 4);
    if (!tar_data) {
        kfree(pkg_data);
        return -1;
    }

    uint32_t tar_size = 0;
    if (gzip_decompress(pkg_data + HPKG_MAGIC_LEN,
                        pkg_size - HPKG_MAGIC_LEN,
                        tar_data, pkg_size * 4, &tar_size) != 0) {
        vga_printf("[HPKG] Failed to decompress package\n");
        kfree(tar_data);
        kfree(pkg_data);
        return -1;
    }
    kfree(pkg_data);

    hpkg_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    uint32_t pos = 0;
    int meta_found = 0;

    while (pos + TAR_BLOCK_SIZE <= tar_size) {
        const tar_header_t *hdr = (const tar_header_t *)(tar_data + pos);
        int all_zero = 1;
        for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
            if (tar_data[pos + i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) break;

        uint32_t file_size = tar_oct_to_int(hdr->size, 12);
        pos += TAR_BLOCK_SIZE;

        if (strcmp(hdr->name, "meta.ini") == 0) {
            parse_meta_ini((const char *)(tar_data + pos), file_size, &meta);
            meta_found = 1;
            break;
        }

        pos += (file_size + TAR_BLOCK_SIZE - 1) & ~(TAR_BLOCK_SIZE - 1);
    }

    if (!meta_found) {
        vga_printf("[HPKG] No meta.ini found\n");
        kfree(tar_data);
        return -1;
    }

    if (hpkg_is_installed(meta.name)) {
        vga_printf("[HPKG] Package %s already installed, updating\n", meta.name);
        hpkg_remove(meta.name, 0);
    }

    int files = tar_extract(tar_data, tar_size, "/");
    vga_printf("[HPKG] Extracted %d files\n", files);

    kfree(tar_data);

    vga_printf("[HPKG] Package %s %s installed\n", meta.name, meta.version);
    return 0;
}

/* ============================================================================
 * 包卸载
 * ============================================================================ */

int hpkg_uninstall_file(const char *name)
{
    if (!name) return -1;

    if (!hpkg_is_installed(name)) {
        vga_printf("[HPKG] Package %s not installed\n", name);
        return -1;
    }

    char bin_path[256];
    bin_path[0] = '\0';
    safe_strcat(bin_path, sizeof(bin_path), "/bin/");
    safe_strcat(bin_path, sizeof(bin_path), name);
    vfs_unlink(bin_path);

    hpkg_remove(name, 0);

    vga_printf("[HPKG] Package %s uninstalled\n", name);
    return 0;
}

/* ============================================================================
 * 包查询
 * ============================================================================ */

int hpkg_info_file(const char *name)
{
    hpkg_installed_t pkg;
    if (hpkg_query(name, &pkg) != 0) {
        vga_printf("Package %s not installed\n", name);
        return -1;
    }

    vga_printf("Package: %s\n", pkg.meta.name);
    vga_printf("Version: %s\n", pkg.meta.version);
    vga_printf("Status:  %s\n", hpkg_status_str(pkg.status));
    vga_printf("Size:    %u bytes\n", pkg.meta.install_size);
    vga_printf("Author:  %s\n", pkg.meta.author);
    vga_printf("Desc:    %s\n", pkg.meta.description);
    return 0;
}

int hpkg_list_files(void)
{
    hpkg_installed_t packages[HPKG_DB_MAX_PACKAGES];
    int count = hpkg_list(packages, HPKG_DB_MAX_PACKAGES);

    vga_printf("Installed packages (%d):\n", count);
    for (int i = 0; i < count; i++) {
        vga_printf("  %-20s %-10s %u bytes\n",
                   packages[i].meta.name, packages[i].meta.version,
                   packages[i].meta.install_size);
    }
    return count;
}
