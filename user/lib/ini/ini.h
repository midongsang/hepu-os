/* ============================================================================
 * 赫普OS - INI配置文件解析库
 * V8.91: 初始实现
 * 轻量级INI解析器，支持节/键值对/注释/引号
 * ========================================================================== */

#ifndef _INI_H
#define _INI_H

#include <stdint.h>

/* 最大限制 */
#define INI_MAX_SECTIONS   64
#define INI_MAX_KEYS       256
#define INI_MAX_LINE       512
#define INI_MAX_NAME       128
#define INI_MAX_VALUE      512

/* INI错误码 */
typedef enum {
    INI_OK = 0,
    INI_ERR_FILE,
    INI_ERR_MEMORY,
    INI_ERR_PARSE,
    INI_ERR_NOT_FOUND,
    INI_ERR_TYPE
} ini_error_t;

/* 键值对 */
typedef struct {
    char key[INI_MAX_NAME];
    char value[INI_MAX_VALUE];
    int  used;
} ini_key_t;

/* 节 */
typedef struct {
    char      name[INI_MAX_NAME];
    ini_key_t keys[INI_MAX_KEYS];
    int       key_count;
    int       used;
} ini_section_t;

/* INI文件 */
typedef struct {
    ini_section_t sections[INI_MAX_SECTIONS];
    int           section_count;
    char          filename[256];
    int           modified;
} ini_file_t;

/* 初始化 */
int ini_init(ini_file_t *ini);

/* 从字符串解析 */
int ini_parse_string(ini_file_t *ini, const char *data, uint32_t size);

/* 从文件加载 */
int ini_load(ini_file_t *ini, const char *filename);

/* 保存到文件 */
int ini_save(ini_file_t *ini, const char *filename);

/* 获取值 */
const char *ini_get_string(ini_file_t *ini, const char *section,
                           const char *key, const char *default_value);
int ini_get_int(ini_file_t *ini, const char *section,
                const char *key, int default_value);
float ini_get_float(ini_file_t *ini, const char *section,
                    const char *key, float default_value);
int ini_get_bool(ini_file_t *ini, const char *section,
                 const char *key, int default_value);

/* 设置值 */
int ini_set_string(ini_file_t *ini, const char *section,
                   const char *key, const char *value);
int ini_set_int(ini_file_t *ini, const char *section,
                const char *key, int value);
int ini_set_float(ini_file_t *ini, const char *section,
                  const char *key, float value);
int ini_set_bool(ini_file_t *ini, const char *section,
                 const char *key, int value);

/* 删除 */
int ini_remove_key(ini_file_t *ini, const char *section, const char *key);
int ini_remove_section(ini_file_t *ini, const char *section);

/* 查询 */
int ini_has_section(ini_file_t *ini, const char *section);
int ini_has_key(ini_file_t *ini, const char *section, const char *key);
int ini_section_count(ini_file_t *ini);
int ini_key_count(ini_file_t *ini, const char *section);

/* 遍历 */
typedef int (*ini_section_callback)(const char *section, void *user_data);
typedef int (*ini_key_callback)(const char *section, const char *key,
                                const char *value, void *user_data);

int ini_foreach_section(ini_file_t *ini, ini_section_callback cb, void *user_data);
int ini_foreach_key(ini_file_t *ini, const char *section,
                    ini_key_callback cb, void *user_data);

/* 错误信息 */
const char *ini_strerror(int error);

#endif /* _INI_H */
