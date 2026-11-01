/* ============================================================================
 * 赫普OS - INI配置文件解析库实现
 * V8.91: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "ini.h"

/* 去除首尾空格 */
static char *trim(char *str)
{
    if (!str) return NULL;
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

/* 去除引号 */
static char *unquote(char *str)
{
    if (!str) return NULL;
    int len = strlen(str);
    if (len >= 2 && ((str[0] == '"' && str[len-1] == '"') ||
                     (str[0] == '\'' && str[len-1] == '\''))) {
        str[len-1] = '\0';
        return str + 1;
    }
    return str;
}

/* 初始化 */
int ini_init(ini_file_t *ini)
{
    if (!ini) return INI_ERR_MEMORY;
    memset(ini, 0, sizeof(ini_file_t));
    return INI_OK;
}

/* 查找节 */
static ini_section_t *find_section(ini_file_t *ini, const char *name)
{
    if (!ini || !name) return NULL;
    for (int i = 0; i < ini->section_count; i++) {
        if (ini->sections[i].used &&
            strcmp(ini->sections[i].name, name) == 0) {
            return &ini->sections[i];
        }
    }
    return NULL;
}

/* 添加节 */
static ini_section_t *add_section(ini_file_t *ini, const char *name)
{
    if (!ini || !name) return NULL;
    if (ini->section_count >= INI_MAX_SECTIONS) return NULL;

    ini_section_t *sec = &ini->sections[ini->section_count++];
    memset(sec, 0, sizeof(ini_section_t));
    strncpy(sec->name, name, INI_MAX_NAME - 1);
    sec->used = 1;
    return sec;
}

/* 查找键 */
static ini_key_t *find_key(ini_section_t *sec, const char *key)
{
    if (!sec || !key) return NULL;
    for (int i = 0; i < sec->key_count; i++) {
        if (sec->keys[i].used &&
            strcmp(sec->keys[i].key, key) == 0) {
            return &sec->keys[i];
        }
    }
    return NULL;
}

/* 添加键 */
static ini_key_t *add_key(ini_section_t *sec, const char *key, const char *value)
{
    if (!sec || !key) return NULL;
    if (sec->key_count >= INI_MAX_KEYS) return NULL;

    ini_key_t *k = &sec->keys[sec->key_count++];
    memset(k, 0, sizeof(ini_key_t));
    strncpy(k->key, key, INI_MAX_NAME - 1);
    if (value) strncpy(k->value, value, INI_MAX_VALUE - 1);
    k->used = 1;
    return k;
}

/* 从字符串解析 */
int ini_parse_string(ini_file_t *ini, const char *data, uint32_t size)
{
    if (!ini || !data) return INI_ERR_MEMORY;

    ini_init(ini);

    /* 复制数据到可写内存（trim会修改字符串） */
    char *buf = malloc(size + 1);
    if (!buf) return INI_ERR_MEMORY;
    memcpy(buf, data, size);
    buf[size] = '\0';

    char current_section[INI_MAX_NAME] = "";
    char *p = buf;
    char *end = buf + size;
    int line_num = 0;

    while (p < end) {
        /* 读取一行 */
        char line[INI_MAX_LINE];
        int line_len = 0;
        while (p < end && *p != '\n' && line_len < INI_MAX_LINE - 1) {
            line[line_len++] = *p++;
        }
        line[line_len] = '\0';
        if (p < end && *p == '\n') p++;
        line_num++;

        /* 去除注释 */
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        comment = strchr(line, '#');
        if (comment) *comment = '\0';

        /* 去除首尾空格 */
        char *trimmed = trim(line);
        if (*trimmed == '\0') continue;

        /* 节标题 */
        if (trimmed[0] == '[') {
            char *close = strchr(trimmed, ']');
            if (!close) {
                free(buf);
                return INI_ERR_PARSE;
            }
            *close = '\0';
            char *sec_name = trim(trimmed + 1);
            strncpy(current_section, sec_name, INI_MAX_NAME - 1);

            if (!find_section(ini, current_section)) {
                add_section(ini, current_section);
            }
            continue;
        }

        /* 键值对 */
        char *eq = strchr(trimmed, '=');
        if (!eq) {
            /* 尝试冒号分隔 */
            eq = strchr(trimmed, ':');
        }
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(trimmed);
        char *value = trim(eq + 1);
        value = unquote(value);

        /* 确保节存在 */
        ini_section_t *sec = find_section(ini, current_section);
        if (!sec) {
            sec = add_section(ini, current_section);
            if (!sec) {
                free(buf);
                return INI_ERR_MEMORY;
            }
        }

        /* 添加或更新键 */
        ini_key_t *k = find_key(sec, key);
        if (k) {
            strncpy(k->value, value, INI_MAX_VALUE - 1);
        } else {
            add_key(sec, key, value);
        }
    }

    free(buf);
    return INI_OK;
}

/* 从文件加载 */
int ini_load(ini_file_t *ini, const char *filename)
{
    if (!ini || !filename) return INI_ERR_FILE;

    FILE *f = fopen(filename, "r");
    if (!f) return INI_ERR_FILE;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        ini_init(ini);
        return INI_OK;
    }

    char *data = malloc(size + 1);
    if (!data) {
        fclose(f);
        return INI_ERR_MEMORY;
    }

    size_t read = fread(data, 1, size, f);
    fclose(f);
    data[read] = '\0';

    int result = ini_parse_string(ini, data, read);
    free(data);

    if (result == INI_OK) {
        strncpy(ini->filename, filename, 255);
        ini->modified = 0;
    }

    return result;
}

/* 保存到文件 */
int ini_save(ini_file_t *ini, const char *filename)
{
    if (!ini) return INI_ERR_MEMORY;

    const char *fname = filename ? filename : ini->filename;
    if (!fname || !fname[0]) return INI_ERR_FILE;

    FILE *f = fopen(fname, "w");
    if (!f) return INI_ERR_FILE;

    for (int i = 0; i < ini->section_count; i++) {
        ini_section_t *sec = &ini->sections[i];
        if (!sec->used) continue;

        if (sec->name[0]) {
            fprintf(f, "[%s]\n", sec->name);
        }

        for (int j = 0; j < sec->key_count; j++) {
            ini_key_t *k = &sec->keys[j];
            if (!k->used) continue;
            fprintf(f, "%s = %s\n", k->key, k->value);
        }
        fprintf(f, "\n");
    }

    fclose(f);
    ini->modified = 0;
    return INI_OK;
}

/* 获取字符串值 */
const char *ini_get_string(ini_file_t *ini, const char *section,
                           const char *key, const char *default_value)
{
    ini_section_t *sec = find_section(ini, section);
    if (!sec) return default_value;

    ini_key_t *k = find_key(sec, key);
    if (!k) return default_value;

    return k->value;
}

/* 获取整数值 */
int ini_get_int(ini_file_t *ini, const char *section,
                const char *key, int default_value)
{
    const char *val = ini_get_string(ini, section, key, NULL);
    if (!val) return default_value;
    return atoi(val);
}

/* 获取浮点值 */
float ini_get_float(ini_file_t *ini, const char *section,
                    const char *key, float default_value)
{
    const char *val = ini_get_string(ini, section, key, NULL);
    if (!val) return default_value;
    return atof(val);
}

/* 获取布尔值 */
int ini_get_bool(ini_file_t *ini, const char *section,
                 const char *key, int default_value)
{
    const char *val = ini_get_string(ini, section, key, NULL);
    if (!val) return default_value;

    if (strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "1") == 0 ||
        strcasecmp(val, "on") == 0) {
        return 1;
    }
    if (strcasecmp(val, "false") == 0 ||
        strcasecmp(val, "no") == 0 ||
        strcasecmp(val, "0") == 0 ||
        strcasecmp(val, "off") == 0) {
        return 0;
    }
    return default_value;
}

/* 设置字符串值 */
int ini_set_string(ini_file_t *ini, const char *section,
                   const char *key, const char *value)
{
    if (!ini || !section || !key) return INI_ERR_MEMORY;

    ini_section_t *sec = find_section(ini, section);
    if (!sec) {
        sec = add_section(ini, section);
        if (!sec) return INI_ERR_MEMORY;
    }

    ini_key_t *k = find_key(sec, key);
    if (k) {
        strncpy(k->value, value ? value : "", INI_MAX_VALUE - 1);
    } else {
        k = add_key(sec, key, value);
        if (!k) return INI_ERR_MEMORY;
    }

    ini->modified = 1;
    return INI_OK;
}

/* 设置整数值 */
int ini_set_int(ini_file_t *ini, const char *section,
                const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return ini_set_string(ini, section, key, buf);
}

/* 设置浮点值 */
int ini_set_float(ini_file_t *ini, const char *section,
                  const char *key, float value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%f", value);
    return ini_set_string(ini, section, key, buf);
}

/* 设置布尔值 */
int ini_set_bool(ini_file_t *ini, const char *section,
                 const char *key, int value)
{
    return ini_set_string(ini, section, key, value ? "true" : "false");
}

/* 删除键 */
int ini_remove_key(ini_file_t *ini, const char *section, const char *key)
{
    ini_section_t *sec = find_section(ini, section);
    if (!sec) return INI_ERR_NOT_FOUND;

    ini_key_t *k = find_key(sec, key);
    if (!k) return INI_ERR_NOT_FOUND;

    k->used = 0;
    ini->modified = 1;
    return INI_OK;
}

/* 删除节 */
int ini_remove_section(ini_file_t *ini, const char *section)
{
    ini_section_t *sec = find_section(ini, section);
    if (!sec) return INI_ERR_NOT_FOUND;

    sec->used = 0;
    ini->modified = 1;
    return INI_OK;
}

/* 查询 */
int ini_has_section(ini_file_t *ini, const char *section)
{
    return find_section(ini, section) != NULL;
}

int ini_has_key(ini_file_t *ini, const char *section, const char *key)
{
    ini_section_t *sec = find_section(ini, section);
    if (!sec) return 0;
    return find_key(sec, key) != NULL;
}

int ini_section_count(ini_file_t *ini)
{
    if (!ini) return 0;
    int count = 0;
    for (int i = 0; i < ini->section_count; i++) {
        if (ini->sections[i].used) count++;
    }
    return count;
}

int ini_key_count(ini_file_t *ini, const char *section)
{
    ini_section_t *sec = find_section(ini, section);
    if (!sec) return 0;
    int count = 0;
    for (int i = 0; i < sec->key_count; i++) {
        if (sec->keys[i].used) count++;
    }
    return count;
}

/* 遍历 */
int ini_foreach_section(ini_file_t *ini, ini_section_callback cb, void *user_data)
{
    if (!ini || !cb) return 0;
    int count = 0;
    for (int i = 0; i < ini->section_count; i++) {
        if (ini->sections[i].used) {
            if (cb(ini->sections[i].name, user_data) != 0) break;
            count++;
        }
    }
    return count;
}

int ini_foreach_key(ini_file_t *ini, const char *section,
                    ini_key_callback cb, void *user_data)
{
    ini_section_t *sec = find_section(ini, section);
    if (!sec || !cb) return 0;
    int count = 0;
    for (int i = 0; i < sec->key_count; i++) {
        if (sec->keys[i].used) {
            if (cb(section, sec->keys[i].key, sec->keys[i].value, user_data) != 0) break;
            count++;
        }
    }
    return count;
}

/* 错误信息 */
const char *ini_strerror(int error)
{
    switch (error) {
        case INI_OK:          return "OK";
        case INI_ERR_FILE:    return "File error";
        case INI_ERR_MEMORY:  return "Memory error";
        case INI_ERR_PARSE:   return "Parse error";
        case INI_ERR_NOT_FOUND: return "Not found";
        case INI_ERR_TYPE:    return "Type error";
        default:              return "Unknown error";
    }
}
