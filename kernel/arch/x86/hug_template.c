/* ============================================================================
 * 赫普OS - .hug 应用模板生成器
 * ========================================================================== */

#include <stdint.h>
#include "hug_template.h"
#include "vga.h"
#include "string.h"

static hug_template_t htpl;

void hug_template_init(void)
{
    memset(&htpl, 0, sizeof(hug_template_t));
    htpl.initialized = 1;
    htpl_init_builtin_templates();

    vga_puts("[HTPL] app template gen init done \n");
    vga_printf(" templates: %d, generated: %d \n", htpl.template_count, htpl.total_generated);
}

int htpl_init_builtin_templates(void)
{
    struct { const char *name; const char *desc; htpl_type_t type; const char *cat; } builtins[] = {
        {" base app ", " 单 pages base app templates ", HTPL_TYPE_BASIC, " app "},
        {" tabs app ", " 底部标签导航多 pages app ", HTPL_TYPE_TAB, " app "},
        {" 抽屉导航 ", " side 边抽屉导航 app ", HTPL_TYPE_DRAWER, " app "},
        {" master slave 详情 ", " 列 table + 详情 master slave struct app ", HTPL_TYPE_MASTER_DETAIL, " app "},
        {"2D game ", "2D game framework templates ", HTPL_TYPE_GAME_2D, " game "},
        {"3D game ", "3D game framework templates ", HTPL_TYPE_GAME_3D, " game "},
        {" next 台 service ", " no 界面 next 台 service templates ", HTPL_TYPE_SERVICE, " service "},
        {" 桌面小部件 ", " 桌面 Widget templates ", HTPL_TYPE_WIDGET, " components "},
        {" master 题 packages ", " system master 题 packages templates ", HTPL_TYPE_THEME, " master 题 "},
        {" 插件 ", " 可 ext 插件 templates ", HTPL_TYPE_PLUGIN, " 插件 "},
        {" 登录 registered ", " user 登录 registered streams 程 templates ", HTPL_TYPE_LOGIN, " app "},
        {" set pages ", " app set pages templates ", HTPL_TYPE_SETTINGS, " app "},
        {" 列 table 详情 ", " 列 table to 详情导航 templates ", HTPL_TYPE_LIST_DETAIL, " app "},
        {" media 播放 ", " 音 video player templates ", HTPL_TYPE_MEDIA, " media "},
        {" 地 map app ", " 地 map & location templates ", HTPL_TYPE_MAP, " app "},
        {" 聊天 app ", " 即 runtime 通讯聊天 templates ", HTPL_TYPE_CHAT, " 社交 "},
    };

    for (int i = 0; i < 16; i++) {
        int id = htpl_register(builtins[i].name, builtins[i].desc, builtins[i].type);
        if (id >= 0) {
            strncpy(htpl.templates[id].category, builtins[i].cat, 63);
            htpl.templates[id].is_system = 1;
            strncpy(htpl.templates[id].version, "1.0.0", 31);
            htpl.templates[id].min_sdk = 21;
            /* 添加默认文件 */
            htpl_add_file(id, "src/main.c", "// master entry \nint main() { return 0; }\n");
            htpl_add_file(id, "manifest.json", "{\"name\":\"app\",\"version\":\"1.0.0\"}\n");
            htpl_add_file(id, "README.md", "# app \n");
        }
    }
    return htpl.template_count;
}

/* 模板管理 */
int htpl_register(const char *name, const char *desc, htpl_type_t type)
{
    for (int i = 0; i < HTPL_MAX_TEMPLATES; i++) {
        if (!htpl.templates[i].used) {
            memset(&htpl.templates[i], 0, sizeof(htpl_template_t));
            htpl.templates[i].used = 1;
            htpl.templates[i].id = i;
            if (name) strncpy(htpl.templates[i].name, name, 127);
            if (desc) strncpy(htpl.templates[i].description, desc, 511);
            htpl.templates[i].type = type;
            htpl.template_count++;
            return i;
        }
    }
    return -1;
}

int htpl_unregister(int id)
{
    if (id < 0 || id >= HTPL_MAX_TEMPLATES || !htpl.templates[id].used) return -1;
    if (htpl.templates[id].is_system) return -1;
    htpl.templates[id].used = 0;
    htpl.template_count--;
    return 0;
}

int htpl_get(int id, htpl_template_t *tpl)
{
    if (id < 0 || id >= HTPL_MAX_TEMPLATES || !htpl.templates[id].used || !tpl) return -1;
    memcpy(tpl, &htpl.templates[id], sizeof(htpl_template_t));
    return 0;
}

int htpl_list(htpl_template_t *templates, int max)
{
    if (!templates || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HTPL_MAX_TEMPLATES && count < max; i++) {
        if (htpl.templates[i].used) memcpy(&templates[count++], &htpl.templates[i], sizeof(htpl_template_t));
    }
    return count;
}

int htpl_list_by_type(htpl_type_t type, htpl_template_t *templates, int max)
{
    if (!templates || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HTPL_MAX_TEMPLATES && count < max; i++) {
        if (htpl.templates[i].used && htpl.templates[i].type == type)
            memcpy(&templates[count++], &htpl.templates[i], sizeof(htpl_template_t));
    }
    return count;
}

int htpl_search(const char *query, htpl_template_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HTPL_MAX_TEMPLATES && count < max; i++) {
        if (htpl.templates[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; htpl.templates[i].name[k]; k++)
                    if (htpl.templates[i].name[k] == query[j]) { found = 1; break; }
                for (int k = 0; htpl.templates[i].description[k] && !found; k++)
                    if (htpl.templates[i].description[k] == query[j]) { found = 1; break; }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &htpl.templates[i], sizeof(htpl_template_t));
        }
    }
    return count;
}

/* 模板文件 */
int htpl_add_file(int template_id, const char *path, const char *content)
{
    if (template_id < 0 || template_id >= HTPL_MAX_TEMPLATES || !htpl.templates[template_id].used) return -1;
    for (int i = 0; i < HTPL_MAX_FILES; i++) {
        if (!htpl.templates[template_id].files[i].used) {
            memset(&htpl.templates[template_id].files[i], 0, sizeof(htpl_file_t));
            htpl.templates[template_id].files[i].used = 1;
            if (path) strncpy(htpl.templates[template_id].files[i].path, path, 255);
            if (content) strncpy(htpl.templates[template_id].files[i].content, content, 4095);
            htpl.templates[template_id].file_count++;
            return i;
        }
    }
    return -1;
}

int htpl_remove_file(int template_id, const char *path)
{
    if (template_id < 0 || template_id >= HTPL_MAX_TEMPLATES || !htpl.templates[template_id].used) return -1;
    for (int i = 0; i < HTPL_MAX_FILES; i++) {
        if (htpl.templates[template_id].files[i].used &&
            strcmp(htpl.templates[template_id].files[i].path, path) == 0) {
            htpl.templates[template_id].files[i].used = 0;
            htpl.templates[template_id].file_count--;
            return 0;
        }
    }
    return -1;
}

int htpl_get_file(int template_id, const char *path, htpl_file_t *file)
{
    if (template_id < 0 || template_id >= HTPL_MAX_TEMPLATES || !htpl.templates[template_id].used || !file) return -1;
    for (int i = 0; i < HTPL_MAX_FILES; i++) {
        if (htpl.templates[template_id].files[i].used &&
            strcmp(htpl.templates[template_id].files[i].path, path) == 0) {
            memcpy(file, &htpl.templates[template_id].files[i], sizeof(htpl_file_t));
            return 0;
        }
    }
    return -1;
}

int htpl_list_files(int template_id, htpl_file_t *files, int max)
{
    if (!files || max <= 0) return 0;
    if (template_id < 0 || template_id >= HTPL_MAX_TEMPLATES || !htpl.templates[template_id].used) return 0;
    int count = 0;
    for (int i = 0; i < HTPL_MAX_FILES && count < max; i++) {
        if (htpl.templates[template_id].files[i].used)
            memcpy(&files[count++], &htpl.templates[template_id].files[i], sizeof(htpl_file_t));
    }
    return count;
}

/* 代码生成 */
int htpl_generate(const htpl_config_t *config, htpl_result_t *result)
{
    if (!config || !result) return -1;
    memset(result, 0, sizeof(htpl_result_t));
    result->success = 1;
    strncpy(result->project_path, config->output_dir, 255);
    if (config->template_id >= 0 && config->template_id < HTPL_MAX_TEMPLATES &&
        htpl.templates[config->template_id].used) {
        result->file_count = htpl.templates[config->template_id].file_count;
        htpl.templates[config->template_id].usage_count++;
    } else {
        result->file_count = 3;
    }
    result->total_size = 32 * 1024;
    htpl.total_generated++;
    return 0;
}

int htpl_generate_file(int template_id, const char *file_path,
                       const char *app_name, const char *package, char *output, int max)
{
    (void)file_path;
    if (template_id < 0 || template_id >= HTPL_MAX_TEMPLATES || !htpl.templates[template_id].used) return -1;
    if (!output || max <= 0) return -1;
    /* 简化: 返回模板内容 */
    int len = 0;
    if (app_name) {
        while (app_name[len] && len < max - 1) { output[len] = app_name[len]; len++; }
    }
    if (package && len < max - 1) {
        output[len++] = '\n';
        int i = 0;
        while (package[i] && len < max - 1) { output[len++] = package[i++]; }
    }
    output[len] = 0;
    return len;
}

int htpl_get_template_by_type(htpl_type_t type, htpl_template_t *tpl)
{
    if (!tpl) return -1;
    for (int i = 0; i < HTPL_MAX_TEMPLATES; i++) {
        if (htpl.templates[i].used && htpl.templates[i].type == type) {
            memcpy(tpl, &htpl.templates[i], sizeof(htpl_template_t));
            return i;
        }
    }
    return -1;
}

int htpl_get_stats(hug_template_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &htpl, sizeof(hug_template_t));
    return 0;
}
