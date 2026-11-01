/* ============================================================================
 * 赫普OS - ext2 文件系统：自测
 * 从 ide_ext2.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：ide_ext2_selftest（ext2 文件系统功能自测）
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "ide.h"
#include "ext2.h"
#include "ide_ext2.h"
#include "vga.h"
#include "serial.h"

void ide_ext2_selftest(void)
{
    ide_drive_t d;
    if (ide_get_drive_info(0, &d) != 0) {
        serial_puts(0x3F8, "[EXT2] selftest: no disk, skip\n");
        return;
    }
    if (ide_ext2_mount() != 0) {
        serial_puts(0x3F8, "[EXT2] selftest: mount FAIL\n");
        return;
    }
    serial_puts(0x3F8, "[EXT2] selftest: mounted (ext2 fs on IDE disk)\n");

    /* 读 /hello.txt */
    {
        ext2_inode_t root, inode;
        if (ext2_read_inode(EXT2_ROOT_INODE, &root) == 0) {
            uint32_t ino = 0;
            if (ext2_lookup(&root, "hello.txt", &ino) == 0 &&
                ext2_read_inode(ino, &inode) == 0) {
                char buf[128];
                uint32_t rd = ext2_read_data(&inode, 0, buf, 127);
                buf[rd] = 0;
                serial_puts(0x3F8, "[EXT2] read /hello.txt: ");
                serial_puts(0x3F8, buf);
                serial_putc(0x3F8, '\n');
            } else {
                serial_puts(0x3F8, "[EXT2] read /hello.txt FAIL\n");
            }
        }
    }

    /* 写测试(幂等): test_hepu.txt 不存在才创建, 存在直接复用 */
    {
        ext2_inode_t root, ti;
        uint32_t tino = 0;
        int have = 0;
        if (ext2_read_inode(EXT2_ROOT_INODE, &root) == 0) {
            have = (ext2_lookup(&root, "test_hepu.txt", &tino) == 0);
        }
        if (!have) {
            int pc = ext2_path_create("/test_hepu.txt", 0x81A4);
            if (pc <= 0) {
                serial_puts(0x3F8, "[EXT2] create /test_hepu.txt FAIL\n");
                return;
            }
            ext2_read_inode(EXT2_ROOT_INODE, &root);
            if (ext2_lookup(&root, "test_hepu.txt", &tino) != 0) {
                serial_puts(0x3F8, "[EXT2] create ok but lookup FAIL\n");
                return;
            }
        }
        if (ext2_read_inode(tino, &ti) == 0) {
            const char *data = "written by Hepu OS kernel\n";
            uint32_t dl = 27;
            if (ext2_write_data(&ti, 0, data, dl) >= 0) {
                ext2_write_inode(tino, &ti);
                uint8_t rbuf[64];
                uint32_t n = ext2_read_data(&ti, 0, rbuf, 63);
                if (n == dl && memcmp(rbuf, data, dl) == 0) {
                    serial_puts(0x3F8, "[EXT2] write+readback /test_hepu.txt PASS\n");
                } else {
                    serial_puts(0x3F8, "[EXT2] write+readback MISMATCH\n");
                }
            } else {
                serial_puts(0x3F8, "[EXT2] write /test_hepu.txt FAIL\n");
            }
        }
    }

    /* V2.57: 目录/删除/重命名持久化自测(幂等) */
    {
        /* 1. 建目录 /docs (已存在则复用) */
        ext2_inode_t root;
        uint32_t dino = 0;
        int have_dir = 0;
        if (ext2_read_inode(EXT2_ROOT_INODE, &root) == 0) {
            have_dir = (ext2_lookup(&root, "docs", &dino) == 0);
        }
        if (!have_dir) {
            if (ext2_path_mkdir_v2("/docs", 0x1ED) > 0) {
                serial_puts(0x3F8, "[EXT2] mkdir /docs PASS\n");
            } else {
                serial_puts(0x3F8, "[EXT2] mkdir /docs FAIL\n");
            }
        } else {
            serial_puts(0x3F8, "[EXT2] mkdir /docs (already exists)\n");
        }

        /* 2. 往 /docs/note.txt 写数据并读回 */
        {
            uint32_t nino = 0;
            int have_note = 0;
            if (ext2_read_inode(EXT2_ROOT_INODE, &root) == 0) {
                if (ext2_lookup(&root, "docs", &dino) == 0) {
                    ext2_inode_t dir;
                    if (ext2_read_inode(dino, &dir) == 0) {
                        have_note = (ext2_lookup(&dir, "note.txt", &nino) == 0);
                    }
                }
            }
            if (!have_note) {
                int pc = ext2_path_create_v2("/docs/note.txt", 0x81A4);
                if (pc <= 0) {
                    serial_puts(0x3F8, "[EXT2] create /docs/note.txt FAIL\n");
                } else {
                    serial_puts(0x3F8, "[EXT2] create /docs/note.txt PASS\n");
                }
            }
            /* 写数据+读回 */
            if (ext2_read_inode(EXT2_ROOT_INODE, &root) == 0 &&
                ext2_lookup(&root, "docs", &dino) == 0) {
                ext2_inode_t dir, ti;
                if (ext2_read_inode(dino, &dir) == 0 &&
                    ext2_lookup(&dir, "note.txt", &nino) == 0 &&
                    ext2_read_inode(nino, &ti) == 0) {
                    const char *d = "nested file in docs dir";
                    uint32_t dl = 24;
                    if (ext2_write_data(&ti, 0, d, dl) >= 0) {
                        ext2_write_inode(nino, &ti);
                        char rb[64];
                        uint32_t nr = ext2_read_data(&ti, 0, rb, 63);
                        rb[nr] = 0;
                        if (nr == dl && memcmp(rb, d, dl) == 0) {
                            serial_puts(0x3F8, "[EXT2] nested write+readback PASS\n");
                        } else {
                            serial_puts(0x3F8, "[EXT2] nested write+readback MISMATCH\n");
                        }
                    } else {
                        serial_puts(0x3F8, "[EXT2] nested write FAIL\n");
                    }
                }
            }
        }

        /* 3. rm: 创建临时文件后删除, 验证目录项移除 */
        {
            static char tmppath[300];
            int tn = 0;
            tmppath[tn++] = '/'; tmppath[tn++] = 't'; tmppath[tn++] = 'm'; tmppath[tn++] = 'p';
            tmppath[tn++] = 'x'; tmppath[tn++] = '.'; tmppath[tn++] = 't'; tmppath[tn++] = 'x';
            tmppath[tn++] = 't'; tmppath[tn] = 0;
            /* 先清理旧 */
            ext2_path_unlink_v2(tmppath);
            if (ext2_path_create_v2(tmppath, 0x81A4) > 0) {
                if (ext2_path_unlink_v2(tmppath) == 0) {
                    /* 验证已消失: lookup 应失败 */
                    ext2_inode_t rootv;
                    uint32_t gone_ino = 0;
                    int gone = -1;
                    if (ext2_read_inode(EXT2_ROOT_INODE, &rootv) == 0) {
                        if (ext2_lookup(&rootv, "tmp.txt", &gone_ino) != 0) {
                            gone = 0;
                        }
                    }
                    if (gone == 0) {
                        serial_puts(0x3F8, "[EXT2] unlink tmp PASS\n");
                    } else {
                        serial_puts(0x3F8, "[EXT2] unlink tmp but entry remains\n");
                    }
                } else {
                    serial_puts(0x3F8, "[EXT2] unlink tmp FAIL\n");
                }
            } else {
                serial_puts(0x3F8, "[EXT2] create tmp for unlink FAIL\n");
            }
        }

        /* 4. rename: /ren_a.txt -> /ren_b.txt */
        {
            ext2_path_unlink_v2("/ren_a.txt");
            ext2_path_unlink_v2("/ren_b.txt");
            if (ext2_path_create_v2("/ren_a.txt", 0x81A4) > 0) {
                if (ext2_path_rename("/ren_a.txt", "/ren_b.txt") == 0) {
                    uint32_t rb_ino = 0;
                    ext2_inode_t root2;
                    int ok = -1;
                    if (ext2_read_inode(EXT2_ROOT_INODE, &root2) == 0) {
                        if (ext2_lookup(&root2, "ren_b.txt", &rb_ino) == 0) ok = 0;
                    }
                    if (ok == 0) {
                        serial_puts(0x3F8, "[EXT2] rename a->b PASS\n");
                    } else {
                        serial_puts(0x3F8, "[EXT2] rename a->b but b not found\n");
                    }
                } else {
                    serial_puts(0x3F8, "[EXT2] rename FAIL\n");
                }
            } else {
                serial_puts(0x3F8, "[EXT2] create ren_a FAIL\n");
            }
        }
    }

    /* V2.59: 大文件(>12KB 一级间接块)写入 + 读回校验 */
    {
        int big_ok = 1;
        static uint8_t tmpb[1024];
        static uint8_t chkb[1024];
        ext2_path_unlink_v2("/big.bin");
        if (ext2_path_create_v2("/big.bin", 0x81A4) > 0) {
            uint32_t bino = 0;
            ext2_inode_t root3;
            if (ext2_read_inode(EXT2_ROOT_INODE, &root3) == 0 &&
                ext2_lookup(&root3, "big.bin", &bino) == 0) {
                ext2_inode_t bi;
                if (ext2_read_inode(bino, &bi) == 0) {
                    uint32_t total = 512 * 1024;   /* V3.87-D3: 512KB 触发双级间接块(>268块) */
                    uint32_t off = 0;
                    while (off < total) {
                        for (int i = 0; i < 1024; i++) tmpb[i] = (uint8_t)((off + i) * 31 + 7);
                        if (ext2_write_data(&bi, off, tmpb, 1024) < 0) { big_ok = 0; break; }
                        off += 1024;
                    }
                    bi.i_size = total;
                    ext2_write_inode(bino, &bi);
                    if (big_ok) {
                        int bad = 0;
                        uint32_t r = 0;
                        for (; r < total; r += 1024) {
                            if (ext2_read_data(&bi, r, chkb, 1024) != 1024) { bad = 1; break; }
                            for (int i = 0; i < 1024; i++) {
                                if (chkb[i] != (uint8_t)((r + i) * 31 + 7)) { bad = 1; break; }
                            }
                            if (bad) break;
                        }
                        if (!bad) {
                            serial_puts(0x3F8, "[EXT2] bigfile(512KB,dbl-indirect) write+readback PASS\n");
                        } else {
                            serial_puts(0x3F8, "[EXT2] bigfile readback MISMATCH\n");
                        }
                    } else {
                        serial_puts(0x3F8, "[EXT2] bigfile write FAIL\n");
                    }
                }
            }
        }
    }

    if (ide_ext2_sync() == 0) {
        serial_puts(0x3F8, "[EXT2] selftest: synced back to IDE disk\n");
    }

    /* V3.89-E25: 启动自动 fsck 健康检查(只读哨兵).
     * 数据盘同步完成后执行一次完整 fsck: 位图/超级块一致性 + 孤儿块/inode 报告.
     * 发现孤儿时提示用 fs fsck fix 修复; 此哨兵可持续监控"每启动资源消耗"回归. */
#ifdef HEPU_AUTO_FSCK
    {
        extern int ide_ext2_fsck(void);
        serial_puts(0x3F8, "[EXT2] auto-fsck: running\n");
        int r = ide_ext2_fsck();
        serial_puts(0x3F8, "[EXT2] auto-fsck: ");
        if (r == 0) serial_puts(0x3F8, "OK\n");
        else serial_puts(0x3F8, "ISSUES (use 'fs fsck fix' to repair)\n");
    }
#endif

    serial_puts(0x3F8, "[EXT2] selftest done\n");
}

/* ============ shell 辅助(VGA输出) ============ */

/* 列根目录(仅一级, 手机系统场景够用) */
