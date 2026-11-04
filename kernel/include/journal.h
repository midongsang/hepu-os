#ifndef JOURNAL_H
#define JOURNAL_H

#include <stdint.h>

#define JOURNAL_MAGIC 0x4A4F5552  /* "JOUR" */
#define JOURNAL_MAX_ENTRIES 256
#define JOURNAL_MAX_DATA 4096

typedef enum {
    JOURNAL_OP_WRITE = 1,
    JOURNAL_OP_DELETE,
    JOURNAL_OP_RENAME,
    JOURNAL_OP_MKDIR,
    JOURNAL_OP_CREATE,
    JOURNAL_OP_TRUNCATE,
} journal_op_t;

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t op;
    uint32_t inode_num;
    uint64_t timestamp;
    uint32_t data_len;
    uint8_t  data[JOURNAL_MAX_DATA];
} __attribute__((packed)) journal_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t committed;
    journal_entry_t entries[JOURNAL_MAX_ENTRIES];
} journal_t;

void journal_init(void);
int journal_start_transaction(void);
int journal_write(journal_op_t op, uint32_t inode, const void *data, uint32_t len);
int journal_commit_transaction(void);
int journal_abort_transaction(void);
int journal_replay(void);
int journal_get_status(uint32_t *pending, uint32_t *committed);
void journal_selftest(void);

#endif /* JOURNAL_H */
