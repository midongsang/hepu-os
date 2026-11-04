/* ============================================================================
 * 赫普OS - 嵌入式数据库库 (Hepu Database)
 * V8.83: 初始实现（简化版SQLite兼容API）
 * 支持：表创建/插入/查询/更新/删除/事务
 * ========================================================================== */

#ifndef _HEPUB_H
#define _HEPUB_H

#include <stdint.h>
#include <stddef.h>

/* 数据库版本 */
#define HEPUB_VERSION       "1.0"
#define HEPUB_MAGIC         "HDB1"
#define HEPUB_MAGIC_LEN     4

/* 最大限制 */
#define HEPUB_MAX_NAME      64
#define HEPUB_MAX_TABLES    32
#define HEPUB_MAX_COLUMNS   16
#define HEPUB_MAX_ROWS      1024
#define HEPUB_MAX_SQL       1024
#define HEPUB_MAX_VALUE     256

/* 列类型 */
typedef enum {
    HEPUB_INTEGER = 0,
    HEPUB_TEXT,
    HEPUB_REAL,
    HEPUB_BLOB,
    HEPUB_NULL
} hepub_type_t;

/* 列定义 */
typedef struct {
    char        name[HEPUB_MAX_NAME];
    hepub_type_t type;
    int         not_null;
    int         primary_key;
    int         auto_increment;
} hepub_column_t;

/* 值 */
typedef struct {
    hepub_type_t type;
    union {
        int64_t  i;
        double   r;
        char     t[HEPUB_MAX_VALUE];
        struct {
            uint8_t *data;
            uint32_t size;
        } b;
    } value;
} hepub_value_t;

/* 行 */
typedef struct {
    hepub_value_t values[HEPUB_MAX_COLUMNS];
    int            used;
} hepub_row_t;

/* 表 */
typedef struct {
    char            name[HEPUB_MAX_NAME];
    hepub_column_t  columns[HEPUB_MAX_COLUMNS];
    int             column_count;
    hepub_row_t     rows[HEPUB_MAX_ROWS];
    int             row_count;
    int             auto_inc_counter;
    int             used;
} hepub_table_t;

/* 数据库 */
typedef struct {
    char           filename[256];
    hepub_table_t  tables[HEPUB_MAX_TABLES];
    int            table_count;
    int            initialized;
    int            in_transaction;
    /* 错误信息 */
    char           error_msg[256];
    int            error_code;
} hepub_db_t;

/* 结果集 */
typedef struct {
    hepub_table_t *table;
    int            current_row;
    int            column_count;
    char         **column_names;
    int            row_count;
} hepub_result_t;

/* 预编译语句 */
typedef struct {
    hepub_db_t *db;
    char        sql[HEPUB_MAX_SQL];
    int         prepared;
    int         type;  /* 0=unknown, 1=select, 2=insert, 3=update, 4=delete, 5=create */
} hepub_stmt_t;

/* 错误码 */
#define HEPUB_OK          0
#define HEPUB_ERROR       1
#define HEPUB_BUSY        2
#define HEPUB_MISUSE      3
#define HEPUB_NOMEM       4
#define HEPUB_READONLY    5
#define HEPUB_CORRUPT     6
#define HEPUB_NOTFOUND    7
#define HEPUB_CONSTRAINT  8
#define HEPUB_MISMATCH    9

/* 函数声明 */

/* 数据库管理 */
int hepub_open(const char *filename, hepub_db_t **db);
int hepub_close(hepub_db_t *db);
int hepub_exec(hepub_db_t *db, const char *sql);
const char *hepub_errmsg(hepub_db_t *db);
int hepub_errcode(hepub_db_t *db);

/* 表管理 */
int hepub_create_table(hepub_db_t *db, const char *name,
                       const char **columns, int column_count);
int hepub_drop_table(hepub_db_t *db, const char *name);
int hepub_table_exists(hepub_db_t *db, const char *name);
int hepub_get_table_names(hepub_db_t *db, char **names, int max_names);

/* 数据操作 */
int hepub_insert(hepub_db_t *db, const char *table,
                 const char **columns, const char **values, int count);
int hepub_update(hepub_db_t *db, const char *table,
                 const char *set_col, const char *set_val,
                 const char *where_col, const char *where_val);
int hepub_delete(hepub_db_t *db, const char *table,
                 const char *where_col, const char *where_val);
int hepub_select(hepub_db_t *db, const char *table,
                 const char *where_col, const char *where_val,
                 hepub_result_t *result);
int hepub_count(hepub_db_t *db, const char *table);

/* 结果集操作 */
int hepub_step(hepub_result_t *result);
int hepub_column_count(hepub_result_t *result);
int hepub_data_count(hepub_result_t *result);
const char *hepub_column_name(hepub_result_t *result, int col);
int hepub_column_type(hepub_result_t *result, int col);
int hepub_column_int(hepub_result_t *result, int col);
double hepub_column_double(hepub_result_t *result, int col);
const char *hepub_column_text(hepub_result_t *result, int col);
void hepub_result_free(hepub_result_t *result);

/* 事务 */
int hepub_begin(hepub_db_t *db);
int hepub_commit(hepub_db_t *db);
int hepub_rollback(hepub_db_t *db);

/* 工具函数 */
int hepub_version(char *buf, int buf_size);
int hepub_validate_sql(const char *sql);

#endif /* _HEPUB_H */
