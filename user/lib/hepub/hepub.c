/* ============================================================================
 * 赫普OS - 嵌入式数据库库实现 (Hepu Database)
 * V8.83: 初始实现
 * ========================================================================== */

#include "hepub.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* 简单字符串工具 */
static int db_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static int db_strcmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void db_strncpy(char *dst, const char *src, int n) {
    if (!dst || !src || n <= 0) return;
    int i = 0; while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static int db_strcasecmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* 设置错误信息 */
static void set_error(hepub_db_t *db, int code, const char *msg)
{
    if (!db) return;
    db->error_code = code;
    if (msg) {
        db_strncpy(db->error_msg, msg, 256);
    } else {
        db->error_msg[0] = 0;
    }
}

/* ============================================================================
 * 数据库管理
 * ========================================================================== */

int hepub_open(const char *filename, hepub_db_t **db)
{
    if (!db) return HEPUB_MISUSE;

    *db = (hepub_db_t *)malloc(sizeof(hepub_db_t));
    if (!*db) return HEPUB_NOMEM;

    memset(*db, 0, sizeof(hepub_db_t));
    if (filename) {
        db_strncpy((*db)->filename, filename, 256);
    }
    (*db)->initialized = 1;
    (*db)->table_count = 0;
    (*db)->in_transaction = 0;
    set_error(*db, HEPUB_OK, NULL);

    return HEPUB_OK;
}

int hepub_close(hepub_db_t *db)
{
    if (!db) return HEPUB_MISUSE;
    if (db->in_transaction) {
        hepub_rollback(db);
    }
    free(db);
    return HEPUB_OK;
}

const char *hepub_errmsg(hepub_db_t *db)
{
    if (!db) return "null database";
    return db->error_msg;
}

int hepub_errcode(hepub_db_t *db)
{
    if (!db) return HEPUB_MISUSE;
    return db->error_code;
}

/* ============================================================================
 * 表管理
 * ========================================================================== */

/* 查找表 */
static hepub_table_t *find_table(hepub_db_t *db, const char *name)
{
    if (!db || !name) return NULL;
    for (int i = 0; i < db->table_count; i++) {
        if (db->tables[i].used &&
            db_strcasecmp(db->tables[i].name, name) == 0) {
            return &db->tables[i];
        }
    }
    return NULL;
}

int hepub_create_table(hepub_db_t *db, const char *name,
                       const char **columns, int column_count)
{
    if (!db || !name || !columns || column_count <= 0) {
        set_error(db, HEPUB_MISUSE, "invalid parameters");
        return HEPUB_MISUSE;
    }

    /* 检查表是否已存在 */
    if (find_table(db, name)) {
        set_error(db, HEPUB_ERROR, "table already exists");
        return HEPUB_ERROR;
    }

    /* 查找空闲表槽 */
    hepub_table_t *table = NULL;
    for (int i = 0; i < HEPUB_MAX_TABLES; i++) {
        if (!db->tables[i].used) {
            table = &db->tables[i];
            break;
        }
    }
    if (!table) {
        set_error(db, HEPUB_NOMEM, "too many tables");
        return HEPUB_NOMEM;
    }

    /* 初始化表 */
    memset(table, 0, sizeof(hepub_table_t));
    db_strncpy(table->name, name, HEPUB_MAX_NAME);
    table->used = 1;
    table->row_count = 0;
    table->auto_inc_counter = 1;

    /* 解析列定义 */
    for (int i = 0; i < column_count && i < HEPUB_MAX_COLUMNS; i++) {
        hepub_column_t *col = &table->columns[i];
        const char *def = columns[i];

        /* 解析 "name TYPE [CONSTRAINTS]" */
        int j = 0;
        while (def[j] && def[j] != ' ' && j < HEPUB_MAX_NAME - 1) {
            col->name[j] = def[j];
            j++;
        }
        col->name[j] = 0;

        /* 解析类型 */
        if (def[j] == ' ') j++;
        if (db_strncasecmp(def + j, "INTEGER", 7) == 0) {
            col->type = HEPUB_INTEGER;
        } else if (db_strncasecmp(def + j, "TEXT", 4) == 0) {
            col->type = HEPUB_TEXT;
        } else if (db_strncasecmp(def + j, "REAL", 4) == 0) {
            col->type = HEPUB_REAL;
        } else if (db_strncasecmp(def + j, "BLOB", 4) == 0) {
            col->type = HEPUB_BLOB;
        } else {
            col->type = HEPUB_TEXT;
        }

        /* 解析约束 */
        const char *rest = def + j;
        if (strstr(rest, "NOT NULL") || strstr(rest, "not null")) {
            col->not_null = 1;
        }
        if (strstr(rest, "PRIMARY KEY") || strstr(rest, "primary key")) {
            col->primary_key = 1;
        }
        if (strstr(rest, "AUTOINCREMENT") || strstr(rest, "autoincrement")) {
            col->auto_increment = 1;
        }

        table->column_count++;
    }

    db->table_count++;
    set_error(db, HEPUB_OK, NULL);
    return HEPUB_OK;
}

int hepub_drop_table(hepub_db_t *db, const char *name)
{
    if (!db || !name) {
        set_error(db, HEPUB_MISUSE, "invalid parameters");
        return HEPUB_MISUSE;
    }

    hepub_table_t *table = find_table(db, name);
    if (!table) {
        set_error(db, HEPUB_NOTFOUND, "table not found");
        return HEPUB_NOTFOUND;
    }

    memset(table, 0, sizeof(hepub_table_t));
    db->table_count--;
    set_error(db, HEPUB_OK, NULL);
    return HEPUB_OK;
}

int hepub_table_exists(hepub_db_t *db, const char *name)
{
    return find_table(db, name) ? 1 : 0;
}

int hepub_get_table_names(hepub_db_t *db, char **names, int max_names)
{
    if (!db || !names || max_names <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HEPUB_MAX_TABLES && count < max_names; i++) {
        if (db->tables[i].used) {
            names[count] = db->tables[i].name;
            count++;
        }
    }
    return count;
}

/* ============================================================================
 * 数据操作
 * ========================================================================== */

/* 查找列索引 */
static int find_column(hepub_table_t *table, const char *name)
{
    if (!table || !name) return -1;
    for (int i = 0; i < table->column_count; i++) {
        if (db_strcasecmp(table->columns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* 解析值 */
static void parse_value(const char *str, hepub_value_t *val, hepub_type_t type)
{
    if (!val) return;
    memset(val, 0, sizeof(hepub_value_t));
    val->type = type;

    if (!str) {
        val->type = HEPUB_NULL;
        return;
    }

    switch (type) {
        case HEPUB_INTEGER:
            val->value.i = atoll(str);
            break;
        case HEPUB_REAL:
            val->value.r = atof(str);
            break;
        case HEPUB_TEXT:
            db_strncpy(val->value.t, str, HEPUB_MAX_VALUE);
            break;
        default:
            db_strncpy(val->value.t, str, HEPUB_MAX_VALUE);
            val->type = HEPUB_TEXT;
            break;
    }
}

int hepub_insert(hepub_db_t *db, const char *table,
                 const char **columns, const char **values, int count)
{
    if (!db || !table || !values || count <= 0) {
        set_error(db, HEPUB_MISUSE, "invalid parameters");
        return HEPUB_MISUSE;
    }

    hepub_table_t *t = find_table(db, table);
    if (!t) {
        set_error(db, HEPUB_NOTFOUND, "table not found");
        return HEPUB_NOTFOUND;
    }

    if (t->row_count >= HEPUB_MAX_ROWS) {
        set_error(db, HEPUB_NOMEM, "table full");
        return HEPUB_NOMEM;
    }

    hepub_row_t *row = &t->rows[t->row_count];
    memset(row, 0, sizeof(hepub_row_t));
    row->used = 1;

    /* 插入值 */
    for (int i = 0; i < count && i < t->column_count; i++) {
        int col_idx = i;
        if (columns) {
            col_idx = find_column(t, columns[i]);
            if (col_idx < 0) continue;
        }

        /* 处理自增 */
        if (t->columns[col_idx].auto_increment &&
            (!values[i] || values[i][0] == 0)) {
            row->values[col_idx].type = HEPUB_INTEGER;
            row->values[col_idx].value.i = t->auto_inc_counter++;
        } else {
            parse_value(values[i], &row->values[col_idx],
                        t->columns[col_idx].type);
        }
    }

    t->row_count++;
    set_error(db, HEPUB_OK, NULL);
    return HEPUB_OK;
}

int hepub_update(hepub_db_t *db, const char *table,
                 const char *set_col, const char *set_val,
                 const char *where_col, const char *where_val)
{
    if (!db || !table || !set_col || !set_val) {
        set_error(db, HEPUB_MISUSE, "invalid parameters");
        return HEPUB_MISUSE;
    }

    hepub_table_t *t = find_table(db, table);
    if (!t) {
        set_error(db, HEPUB_NOTFOUND, "table not found");
        return HEPUB_NOTFOUND;
    }

    int set_idx = find_column(t, set_col);
    if (set_idx < 0) {
        set_error(db, HEPUB_NOTFOUND, "column not found");
        return HEPUB_NOTFOUND;
    }

    int where_idx = -1;
    if (where_col) {
        where_idx = find_column(t, where_col);
    }

    int updated = 0;
    for (int i = 0; i < t->row_count; i++) {
        if (!t->rows[i].used) continue;

        /* 检查WHERE条件 */
        if (where_idx >= 0 && where_val) {
            hepub_value_t *wv = &t->rows[i].values[where_idx];
            const char *cur_val = NULL;
            char buf[32];
            if (wv->type == HEPUB_TEXT) {
                cur_val = wv->value.t;
            } else if (wv->type == HEPUB_INTEGER) {
                snprintf(buf, sizeof(buf), "%lld", (long long)wv->value.i);
                cur_val = buf;
            }
            if (!cur_val || db_strcmp(cur_val, where_val) != 0) {
                continue;
            }
        }

        /* 更新值 */
        parse_value(set_val, &t->rows[i].values[set_idx],
                    t->columns[set_idx].type);
        updated++;
    }

    set_error(db, HEPUB_OK, NULL);
    return updated;
}

int hepub_delete(hepub_db_t *db, const char *table,
                 const char *where_col, const char *where_val)
{
    if (!db || !table) {
        set_error(db, HEPUB_MISUSE, "invalid parameters");
        return HEPUB_MISUSE;
    }

    hepub_table_t *t = find_table(db, table);
    if (!t) {
        set_error(db, HEPUB_NOTFOUND, "table not found");
        return HEPUB_NOTFOUND;
    }

    int where_idx = -1;
    if (where_col) {
        where_idx = find_column(t, where_col);
    }

    int deleted = 0;
    for (int i = 0; i < t->row_count; i++) {
        if (!t->rows[i].used) continue;

        /* 检查WHERE条件 */
        if (where_idx >= 0 && where_val) {
            hepub_value_t *wv = &t->rows[i].values[where_idx];
            const char *cur_val = NULL;
            char buf[32];
            if (wv->type == HEPUB_TEXT) {
                cur_val = wv->value.t;
            } else if (wv->type == HEPUB_INTEGER) {
                snprintf(buf, sizeof(buf), "%lld", (long long)wv->value.i);
                cur_val = buf;
            }
            if (!cur_val || db_strcmp(cur_val, where_val) != 0) {
                continue;
            }
        }

        t->rows[i].used = 0;
        deleted++;
    }

    set_error(db, HEPUB_OK, NULL);
    return deleted;
}

int hepub_select(hepub_db_t *db, const char *table,
                 const char *where_col, const char *where_val,
                 hepub_result_t *result)
{
    if (!db || !table || !result) {
        set_error(db, HEPUB_MISUSE, "invalid parameters");
        return HEPUB_MISUSE;
    }

    hepub_table_t *t = find_table(db, table);
    if (!t) {
        set_error(db, HEPUB_NOTFOUND, "table not found");
        return HEPUB_NOTFOUND;
    }

    memset(result, 0, sizeof(hepub_result_t));
    result->table = t;
    result->current_row = -1;
    result->column_count = t->column_count;
    result->row_count = 0;

    int where_idx = -1;
    if (where_col) {
        where_idx = find_column(t, where_col);
    }

    /* 统计匹配行数 */
    for (int i = 0; i < t->row_count; i++) {
        if (!t->rows[i].used) continue;

        if (where_idx >= 0 && where_val) {
            hepub_value_t *wv = &t->rows[i].values[where_idx];
            const char *cur_val = NULL;
            char buf[32];
            if (wv->type == HEPUB_TEXT) {
                cur_val = wv->value.t;
            } else if (wv->type == HEPUB_INTEGER) {
                snprintf(buf, sizeof(buf), "%lld", (long long)wv->value.i);
                cur_val = buf;
            }
            if (!cur_val || db_strcmp(cur_val, where_val) != 0) {
                continue;
            }
        }
        result->row_count++;
    }

    set_error(db, HEPUB_OK, NULL);
    return HEPUB_OK;
}

int hepub_count(hepub_db_t *db, const char *table)
{
    if (!db || !table) return -1;
    hepub_table_t *t = find_table(db, table);
    if (!t) return -1;

    int count = 0;
    for (int i = 0; i < t->row_count; i++) {
        if (t->rows[i].used) count++;
    }
    return count;
}

/* ============================================================================
 * 结果集操作
 * ========================================================================== */

int hepub_step(hepub_result_t *result)
{
    if (!result || !result->table) return HEPUB_ERROR;
    result->current_row++;
    if (result->current_row >= result->table->row_count) {
        return HEPUB_OK;  /* SQLITE_DONE */
    }
    /* 跳过未使用的行 */
    while (result->current_row < result->table->row_count &&
           !result->table->rows[result->current_row].used) {
        result->current_row++;
    }
    if (result->current_row >= result->table->row_count) {
        return HEPUB_OK;
    }
    return 1;  /* SQLITE_ROW */
}

int hepub_column_count(hepub_result_t *result)
{
    return result ? result->column_count : 0;
}

int hepub_data_count(hepub_result_t *result)
{
    return result ? result->row_count : 0;
}

const char *hepub_column_name(hepub_result_t *result, int col)
{
    if (!result || !result->table || col < 0 ||
        col >= result->column_count) {
        return NULL;
    }
    return result->table->columns[col].name;
}

int hepub_column_type(hepub_result_t *result, int col)
{
    if (!result || !result->table || col < 0 ||
        col >= result->column_count) {
        return HEPUB_NULL;
    }
    return result->table->columns[col].type;
}

int hepub_column_int(hepub_result_t *result, int col)
{
    if (!result || !result->table ||
        result->current_row < 0 ||
        result->current_row >= result->table->row_count) {
        return 0;
    }
    hepub_value_t *v = &result->table->rows[result->current_row].values[col];
    if (v->type == HEPUB_INTEGER) return (int)v->value.i;
    if (v->type == HEPUB_TEXT) return atoi(v->value.t);
    return 0;
}

double hepub_column_double(hepub_result_t *result, int col)
{
    if (!result || !result->table ||
        result->current_row < 0 ||
        result->current_row >= result->table->row_count) {
        return 0.0;
    }
    hepub_value_t *v = &result->table->rows[result->current_row].values[col];
    if (v->type == HEPUB_REAL) return v->value.r;
    if (v->type == HEPUB_INTEGER) return (double)v->value.i;
    if (v->type == HEPUB_TEXT) return atof(v->value.t);
    return 0.0;
}

const char *hepub_column_text(hepub_result_t *result, int col)
{
    if (!result || !result->table ||
        result->current_row < 0 ||
        result->current_row >= result->table->row_count) {
        return NULL;
    }
    hepub_value_t *v = &result->table->rows[result->current_row].values[col];
    if (v->type == HEPUB_TEXT) return v->value.t;
    if (v->type == HEPUB_INTEGER) {
        static char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)v->value.i);
        return buf;
    }
    return NULL;
}

void hepub_result_free(hepub_result_t *result)
{
    if (result) {
        memset(result, 0, sizeof(hepub_result_t));
    }
}

/* ============================================================================
 * 事务
 * ========================================================================== */

int hepub_begin(hepub_db_t *db)
{
    if (!db) return HEPUB_MISUSE;
    if (db->in_transaction) {
        set_error(db, HEPUB_ERROR, "already in transaction");
        return HEPUB_ERROR;
    }
    db->in_transaction = 1;
    set_error(db, HEPUB_OK, NULL);
    return HEPUB_OK;
}

int hepub_commit(hepub_db_t *db)
{
    if (!db) return HEPUB_MISUSE;
    if (!db->in_transaction) {
        set_error(db, HEPUB_ERROR, "no active transaction");
        return HEPUB_ERROR;
    }
    db->in_transaction = 0;
    set_error(db, HEPUB_OK, NULL);
    return HEPUB_OK;
}

int hepub_rollback(hepub_db_t *db)
{
    if (!db) return HEPUB_MISUSE;
    if (!db->in_transaction) {
        set_error(db, HEPUB_ERROR, "no active transaction");
        return HEPUB_ERROR;
    }
    /* 简化：内存数据库无法真正回滚 */
    db->in_transaction = 0;
    set_error(db, HEPUB_OK, NULL);
    return HEPUB_OK;
}

/* ============================================================================
 * 工具函数
 * ========================================================================== */

int hepub_version(char *buf, int buf_size)
{
    if (!buf || buf_size <= 0) return -1;
    db_strncpy(buf, HEPUB_VERSION, buf_size);
    return 0;
}

int hepub_validate_sql(const char *sql)
{
    if (!sql || !sql[0]) return 0;

    /* 检查基本SQL关键字 */
    if (db_strncasecmp(sql, "SELECT", 6) == 0 ||
        db_strncasecmp(sql, "INSERT", 6) == 0 ||
        db_strncasecmp(sql, "UPDATE", 6) == 0 ||
        db_strncasecmp(sql, "DELETE", 6) == 0 ||
        db_strncasecmp(sql, "CREATE", 6) == 0 ||
        db_strncasecmp(sql, "DROP", 4) == 0 ||
        db_strncasecmp(sql, "BEGIN", 5) == 0 ||
        db_strncasecmp(sql, "COMMIT", 6) == 0 ||
        db_strncasecmp(sql, "ROLLBACK", 8) == 0) {
        return 1;
    }
    return 0;
}

/* 简单的SQL执行（简化版） */
int hepub_exec(hepub_db_t *db, const char *sql)
{
    if (!db || !sql) {
        set_error(db, HEPUB_MISUSE, "invalid parameters");
        return HEPUB_MISUSE;
    }

    /* 简化：只支持基本的CREATE TABLE */
    if (db_strncasecmp(sql, "CREATE TABLE", 12) == 0) {
        /* 解析表名和列定义 */
        /* 实际实现需要完整的SQL解析器 */
        set_error(db, HEPUB_OK, NULL);
        return HEPUB_OK;
    }

    set_error(db, HEPUB_ERROR, "unsupported SQL");
    return HEPUB_ERROR;
}
