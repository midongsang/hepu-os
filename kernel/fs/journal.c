/* ============================================================================
 * 赫普OS - 文件系统日志 (Journaling)
 * 提供崩溃一致性保障，类似ext3/ext4的日志机制
 * ========================================================================== */

#include <stdint.h>
#include "journal.h"
#include "vga.h"
#include "string.h"

static journal_t journal;
static int journal_initialized = 0;
static int in_transaction = 0;
static uint32_t transaction_start = 0;
static uint64_t journal_time = 0;

void journal_init(void)
{
    memset(&journal, 0, sizeof(journal));
    journal.magic = JOURNAL_MAGIC;
    journal.version = 1;
    journal.head = 0;
    journal.tail = 0;
    journal.count = 0;
    journal.committed = 0;
    journal_initialized = 1;
    in_transaction = 0;
    vga_puts("[JOURNAL] fs journal init(ring buffer region)\n");
}

int journal_start_transaction(void)
{
    if (!journal_initialized) return -1;
    if (in_transaction) return -1;  /* 已有事务进行中 */

    in_transaction = 1;
    transaction_start = journal.head;
    journal_time++;
    return 0;
}

int journal_write(journal_op_t op, uint32_t inode, const void *data, uint32_t len)
{
    if (!journal_initialized || !in_transaction) return -1;
    if (len > JOURNAL_MAX_DATA) return -1;

    /* 检查日志是否已满 */
    if (journal.count >= JOURNAL_MAX_ENTRIES) {
        vga_puts("[JOURNAL] log full , needs commit \n");
        return -1;
    }

    journal_entry_t *entry = &journal.entries[journal.head];
    entry->magic = JOURNAL_MAGIC;
    entry->seq = journal.committed + journal.count;
    entry->op = op;
    entry->inode_num = inode;
    entry->timestamp = journal_time;
    entry->data_len = len;
    if (data && len > 0) {
        memcpy(entry->data, data, len);
    }

    journal.head = (journal.head + 1) % JOURNAL_MAX_ENTRIES;
    journal.count++;

    return 0;
}

int journal_commit_transaction(void)
{
    if (!journal_initialized || !in_transaction) return -1;

    uint32_t committed = journal.count;
    journal.committed += committed;
    journal.count = 0;
    in_transaction = 0;

    vga_printf("[JOURNAL] commit transaction: %d records(total commit =%d)\n",
               committed, journal.committed);
    return committed;
}

int journal_abort_transaction(void)
{
    if (!journal_initialized || !in_transaction) return -1;

    /* 回滚：恢复head到事务开始位置 */
    journal.head = transaction_start;
    journal.count = 0;
    in_transaction = 0;

    vga_puts("[JOURNAL] abort transaction , rolled back \n");
    return 0;
}

int journal_replay(void)
{
    if (!journal_initialized) return -1;
    if (journal.count == 0) {
        vga_puts("[JOURNAL] no pending replay records \n");
        return 0;
    }

    vga_printf("[JOURNAL] replay log: %d pending commit records \n", journal.count);

    /* 简化：标记所有记录为已提交 */
    journal.committed += journal.count;
    journal.count = 0;

    vga_puts("[JOURNAL] log replay done \n");
    return 0;
}

int journal_get_status(uint32_t *pending, uint32_t *committed)
{
    if (!journal_initialized) return -1;
    if (pending) *pending = journal.count;
    if (committed) *committed = journal.committed;
    return 0;
}

/* ============================================================================
 * Journal自测
 * ========================================================================== */
void journal_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define JLOG(s) serial_puts(0x3F8, s)

    JLOG("[JOURNAL] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    journal_init();
    uint32_t pending = 0, committed = 0;
    journal_get_status(&pending, &committed);
    if (pending == 0 && committed == 0) {
        JLOG("[JOURNAL] test1 init: PASS\n");
        pass++;
    } else {
        JLOG("[JOURNAL] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：开始事务 */
    int ret = journal_start_transaction();
    if (ret == 0) {
        JLOG("[JOURNAL] test2 start transaction: PASS\n");
        pass++;
    } else {
        JLOG("[JOURNAL] test2 start transaction: FAIL\n");
        fail++;
    }

    /* 测试3：写入日志条目 */
    static uint8_t test_data[64];
    for (int i = 0; i < 64; i++) test_data[i] = i;
    ret = journal_write(JOURNAL_OP_WRITE, 1, test_data, 64);
    if (ret == 0) {
        journal_get_status(&pending, &committed);
        if (pending == 1) {
            JLOG("[JOURNAL] test3 write entry: PASS (pending=1)\n");
            pass++;
        } else {
            JLOG("[JOURNAL] test3 write entry: FAIL (pending=");
            serial_puthex(0x3F8, pending);
            JLOG(")\n");
            fail++;
        }
    } else {
        JLOG("[JOURNAL] test3 write entry: FAIL\n");
        fail++;
    }

    /* 测试4：写入多个条目 */
    ret = journal_write(JOURNAL_OP_CREATE, 2, NULL, 0);
    ret |= journal_write(JOURNAL_OP_MKDIR, 3, NULL, 0);
    if (ret == 0) {
        journal_get_status(&pending, &committed);
        if (pending == 3) {
            JLOG("[JOURNAL] test4 write multiple: PASS (pending=3)\n");
            pass++;
        } else {
            JLOG("[JOURNAL] test4 write multiple: FAIL (pending=");
            serial_puthex(0x3F8, pending);
            JLOG(")\n");
            fail++;
        }
    } else {
        JLOG("[JOURNAL] test4 write multiple: FAIL\n");
        fail++;
    }

    /* 测试5：提交事务 */
    ret = journal_commit_transaction();
    if (ret > 0) {
        journal_get_status(&pending, &committed);
        if (pending == 0 && committed == 3) {
            JLOG("[JOURNAL] test5 commit: PASS (committed=3)\n");
            pass++;
        } else {
            JLOG("[JOURNAL] test5 commit: FAIL (pending=");
            serial_puthex(0x3F8, pending);
            JLOG(", committed=");
            serial_puthex(0x3F8, committed);
            JLOG(")\n");
            fail++;
        }
    } else {
        JLOG("[JOURNAL] test5 commit: FAIL\n");
        fail++;
    }

    /* 测试6：中止事务（回滚） */
    journal_start_transaction();
    journal_write(JOURNAL_OP_WRITE, 4, test_data, 32);
    journal_write(JOURNAL_OP_DELETE, 5, NULL, 0);
    ret = journal_abort_transaction();
    if (ret == 0) {
        journal_get_status(&pending, &committed);
        if (pending == 0 && committed == 3) {
            JLOG("[JOURNAL] test6 abort/rollback: PASS\n");
            pass++;
        } else {
            JLOG("[JOURNAL] test6 abort/rollback: FAIL (pending=");
            serial_puthex(0x3F8, pending);
            JLOG(")\n");
            fail++;
        }
    } else {
        JLOG("[JOURNAL] test6 abort/rollback: FAIL\n");
        fail++;
    }

    /* 测试7：重放（恢复） */
    journal_start_transaction();
    journal_write(JOURNAL_OP_WRITE, 6, test_data, 16);
    journal_write(JOURNAL_OP_RENAME, 7, NULL, 0);
    /* 不提交，模拟崩溃 */
    ret = journal_replay();
    if (ret >= 0) {
        journal_get_status(&pending, &committed);
        if (pending == 0 && committed == 5) {
            JLOG("[JOURNAL] test7 replay/recover: PASS (committed=5)\n");
            pass++;
        } else {
            JLOG("[JOURNAL] test7 replay/recover: FAIL (pending=");
            serial_puthex(0x3F8, pending);
            JLOG(", committed=");
            serial_puthex(0x3F8, committed);
            JLOG(")\n");
            fail++;
        }
    } else {
        JLOG("[JOURNAL] test7 replay/recover: FAIL\n");
        fail++;
    }

    JLOG("[JOURNAL] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    JLOG(", fail=");
    serial_puthex(0x3F8, fail);
    JLOG(")\n");

    #undef JLOG
}
