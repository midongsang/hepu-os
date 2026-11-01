#ifndef WEBENGINE_H
#define WEBENGINE_H

#include <stdint.h>

#define WEB_MAX_DOCUMENTS 2
#define WEB_MAX_NODES 128
#define WEB_MAX_STYLES 32
#define WEB_MAX_SCRIPTS 4
#define WEB_TAG_NAME_LEN 32
#define WEB_ATTR_NAME_LEN 32
#define WEB_ATTR_VALUE_LEN 256
#define WEB_TEXT_LEN 1024

/* HTML标签类型 */
typedef enum {
    TAG_UNKNOWN = 0,
    TAG_HTML, TAG_HEAD, TAG_BODY,
    TAG_DIV, TAG_SPAN, TAG_P, TAG_A,
    TAG_H1, TAG_H2, TAG_H3, TAG_H4, TAG_H5, TAG_H6,
    TAG_UL, TAG_OL, TAG_LI,
    TAG_TABLE, TAG_TR, TAG_TD, TAG_TH,
    TAG_IMG, TAG_VIDEO, TAG_AUDIO,
    TAG_INPUT, TAG_BUTTON, TAG_FORM,
    TAG_SCRIPT, TAG_STYLE, TAG_LINK, TAG_META, TAG_TITLE,
    TAG_BR, TAG_HR,
    TAG_NAV, TAG_HEADER, TAG_FOOTER, TAG_MAIN, TAG_SECTION, TAG_ARTICLE,
    TAG_IFRAME, TAG_CANVAS,
} html_tag_t;

/* CSS属性 */
typedef struct {
    char color[16];
    char background[16];
    int width;
    int height;
    int margin_top, margin_right, margin_bottom, margin_left;
    int padding_top, padding_right, padding_bottom, padding_left;
    int font_size;
    char font_family[32];
    int font_weight;  /* 100-900 */
    int display;      /* 0=none,1=block,2=inline,3=flex */
    int position;     /* 0=static,1=relative,2=absolute,3=fixed */
    int top, left, right, bottom;
    int border_width;
    char border_color[16];
    int border_radius;
    float opacity;
    int z_index;
    int text_align;   /* 0=left,1=center,2=right */
    int line_height;
    int letter_spacing;
} css_style_t;

/* DOM属性 */
typedef struct {
    char name[WEB_ATTR_NAME_LEN];
    char value[WEB_ATTR_VALUE_LEN];
} dom_attr_t;

/* DOM节点 */
typedef struct {
    int used;
    int id;
    html_tag_t tag;
    char tag_name[WEB_TAG_NAME_LEN];
    char text[WEB_TEXT_LEN];
    dom_attr_t attributes[8];
    int attr_count;
    css_style_t style;
    css_style_t computed_style;
    int parent;
    int children[32];
    int child_count;
    int first_child;
    int next_sibling;
    /* 布局 */
    int layout_x, layout_y, layout_w, layout_h;
    int visible;
    int dirty;
} dom_node_t;

/* 文档 */
typedef struct {
    int used;
    int id;
    char url[256];
    char title[128];
    dom_node_t nodes[WEB_MAX_NODES];
    int node_count;
    int root_node;
    int body_node;
    int head_node;
    /* 脚本 */
    char scripts[WEB_MAX_SCRIPTS][4096];
    int script_count;
    /* 样式表 */
    char stylesheets[WEB_MAX_STYLES][1024];
    int stylesheet_count;
    /* 视口 */
    int viewport_width;
    int viewport_height;
    int scroll_x, scroll_y;
    /* 状态 */
    int loaded;
    int rendering;
} web_document_t;

/* JS值类型 */
typedef enum {
    JS_UNDEFINED = 0,
    JS_NULL,
    JS_BOOLEAN,
    JS_NUMBER,
    JS_STRING,
    JS_OBJECT,
    JS_FUNCTION,
} js_type_t;

/* JS值 */
typedef struct {
    js_type_t type;
    union {
        int boolean;
        double number;
        char string[256];
        int object_id;
        int func_id;
    } value;
} js_value_t;

/* JS引擎 */
typedef struct {
    int initialized;
    js_value_t globals[64];
    int global_count;
    int pc;
    int running;
} js_engine_t;

/* 渲染器 */
typedef struct {
    int initialized;
    int width, height;
    void *framebuffer;
    uint32_t fb_size;
} web_renderer_t;

/* 初始化Web引擎 */
void webengine_init(void);

/* 文档管理 */
int web_create_document(const char *url);
int web_destroy_document(int doc_id);
int web_load_html(int doc_id, const char *html);
int web_get_document(int doc_id, web_document_t *doc);

/* HTML解析 */
int web_parse_html(int doc_id, const char *html);
int web_parse_css(int doc_id, const char *css);
int web_apply_styles(int doc_id);

/* DOM操作 */
int web_dom_create_node(int doc_id, html_tag_t tag, const char *text);
int web_dom_append_child(int doc_id, int parent_id, int child_id);
int web_dom_get_element_by_id(int doc_id, const char *id);
int web_dom_get_elements_by_tag(int doc_id, html_tag_t tag, int *ids, int max);
int web_dom_set_attribute(int doc_id, int node_id, const char *name, const char *value);
int web_dom_get_attribute(int doc_id, int node_id, const char *name, char *value, int len);
int web_dom_set_text(int doc_id, int node_id, const char *text);
int web_dom_remove_child(int doc_id, int parent_id, int child_id);

/* 布局与渲染 */
int web_layout(int doc_id);
int web_render(int doc_id, void *framebuffer, int width, int height);
int web_scroll(int doc_id, int dx, int dy);
int web_hit_test(int doc_id, int x, int y);

/* JS引擎 */
void js_init(void);
int js_eval(int doc_id, const char *code);
int js_call_function(int doc_id, const char *name, js_value_t *args, int arg_count, js_value_t *result);
int js_set_global(const char *name, js_value_t value);
int js_get_global(const char *name, js_value_t *value);

/* 网络加载 */
int web_load_url(int doc_id, const char *url);
int web_reload(int doc_id);
int web_go_back(int doc_id);
int web_go_forward(int doc_id);

/* Cookie */
int web_set_cookie(int doc_id, const char *cookie);
int web_get_cookie(int doc_id, char *cookie, int len);

/* 历史 */
int web_history_length(int doc_id);
int web_history_get(int doc_id, int index, char *url, int len);

/* 获取活跃文档数 */
int web_document_count(void);

#endif /* WEBENGINE_H */
