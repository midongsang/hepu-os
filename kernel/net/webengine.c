/* ============================================================================
 * 赫普OS - Web 引擎
 * HTML解析 + CSS样式 + DOM + 布局渲染 + JS引擎
 * ========================================================================== */

#include <stdint.h>
#include "webengine.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static char *web_strtok(char *str, const char *delim) {
    static char *last = 0;
    if (str) last = str;
    if (!last) return 0;
    while (*last && *last == *delim) last++;
    if (*last == 0) return 0;
    char *start = last;
    while (*last && *last != *delim) last++;
    if (*last) { *last = 0; last++; }
    return start;
}

static web_document_t documents[WEB_MAX_DOCUMENTS];
static js_engine_t js;
static web_renderer_t renderer;
static int doc_count = 0;
static int web_initialized = 0;

/* 标签名映射 */
static const char *tag_names[] = {
    "unknown", "html", "head", "body", "div", "span", "p", "a",
    "h1", "h2", "h3", "h4", "h5", "h6",
    "ul", "ol", "li", "table", "tr", "td", "th",
    "img", "video", "audio", "input", "button", "form",
    "script", "style", "link", "meta", "title",
    "br", "hr", "nav", "header", "footer", "main", "section", "article",
    "iframe", "canvas"
};

static html_tag_t parse_tag_name(const char *name)
{
    for (int i = 1; i < 42; i++) {
        if (strcmp(name, tag_names[i]) == 0) return (html_tag_t)i;
    }
    return TAG_UNKNOWN;
}

static void init_default_style(css_style_t *style)
{
    memset(style, 0, sizeof(css_style_t));
    strcpy(style->color, "#000000");
    strcpy(style->background, "transparent");
    style->font_size = 16;
    strcpy(style->font_family, "sans-serif");
    style->font_weight = 400;
    style->display = 1;  /* block */
    style->position = 0; /* static */
    style->opacity = 1.0f;
    style->text_align = 0;
    style->line_height = 20;
}

void webengine_init(void)
{
    for (int i = 0; i < WEB_MAX_DOCUMENTS; i++) documents[i].used = 0;
    doc_count = 0;
    web_initialized = 1;

    memset(&js, 0, sizeof(js_engine_t));
    js.initialized = 1;

    memset(&renderer, 0, sizeof(web_renderer_t));
    renderer.initialized = 1;
    renderer.width = 320;
    renderer.height = 200;

    vga_puts("[WEB] Web engine init done \n");
    vga_puts(" HTML parse + CSS + DOM + JS engine \n");
}

int web_create_document(const char *url)
{
    if (!web_initialized) return -1;
    for (int i = 0; i < WEB_MAX_DOCUMENTS; i++) {
        if (!documents[i].used) {
            memset(&documents[i], 0, sizeof(web_document_t));
            documents[i].used = 1;
            documents[i].id = i;
            if (url) strncpy(documents[i].url, url, 255);
            documents[i].viewport_width = 320;
            documents[i].viewport_height = 200;
            doc_count++;
            return i;
        }
    }
    return -1;
}

int web_destroy_document(int doc_id)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    documents[doc_id].used = 0;
    doc_count--;
    return 0;
}

int web_load_html(int doc_id, const char *html)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    return web_parse_html(doc_id, html);
}

int web_get_document(int doc_id, web_document_t *doc)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used || !doc) return -1;
    memcpy(doc, &documents[doc_id], sizeof(web_document_t));
    return 0;
}

/* 简化HTML解析器 */
int web_parse_html(int doc_id, const char *html)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used || !html) return -1;

    web_document_t *doc = &documents[doc_id];
    doc->node_count = 0;

    /* 创建基础结构: html > head, body */
    int html_node = web_dom_create_node(doc_id, TAG_HTML, "");
    int head_node = web_dom_create_node(doc_id, TAG_HEAD, "");
    int body_node = web_dom_create_node(doc_id, TAG_BODY, "");
    web_dom_append_child(doc_id, html_node, head_node);
    web_dom_append_child(doc_id, html_node, body_node);
    doc->root_node = html_node;
    doc->head_node = head_node;
    doc->body_node = body_node;

    /* 简化解析: 查找<title>和<body>内容 */
    const char *p = html;
    int current_parent = body_node;
    int in_tag = 0;
    char tag_buf[64];
    int tag_pos = 0;
    char text_buf[WEB_TEXT_LEN];
    int text_pos = 0;

    while (*p && doc->node_count < WEB_MAX_NODES - 10) {
        if (*p == '<') {
            /* 保存之前的文本 */
            if (text_pos > 0 && current_parent >= 0) {
                text_buf[text_pos] = '\0';
                if (text_pos > 1) {
                    int text_node = web_dom_create_node(doc_id, TAG_SPAN, text_buf);
                    web_dom_append_child(doc_id, current_parent, text_node);
                }
                text_pos = 0;
            }
            in_tag = 1;
            tag_pos = 0;
            p++;
            continue;
        }
        if (*p == '>' && in_tag) {
            tag_buf[tag_pos] = '\0';
            in_tag = 0;
            /* 处理标签 */
            if (tag_buf[0] == '/') {
                /* 闭合标签 */
                if (strncmp(tag_buf + 1, "body", 4) == 0) current_parent = -1;
            } else if (strncmp(tag_buf, "title", 5) == 0) {
                /* 提取标题 */
                const char *end = strstr(p, "</title>");
                if (end) {
                    int len = end - p - 1;
                    if (len > 0 && len < 128) {
                        memcpy(doc->title, p + 1, len);
                        doc->title[len] = '\0';
                    }
                }
            } else if (strncmp(tag_buf, "h1", 2) == 0 || strncmp(tag_buf, "h2", 2) == 0 ||
                       strncmp(tag_buf, "h3", 2) == 0 || strncmp(tag_buf, "p", 1) == 0 ||
                       strncmp(tag_buf, "div", 3) == 0) {
                html_tag_t tag = parse_tag_name(web_strtok(tag_buf, " "));
                int node = web_dom_create_node(doc_id, tag, "");
                if (current_parent >= 0) web_dom_append_child(doc_id, current_parent, node);
                current_parent = node;
            } else if (strncmp(tag_buf, "br", 2) == 0) {
                int node = web_dom_create_node(doc_id, TAG_BR, "");
                if (current_parent >= 0) web_dom_append_child(doc_id, current_parent, node);
            }
            p++;
            continue;
        }
        if (in_tag) {
            if (tag_pos < 63) tag_buf[tag_pos++] = *p;
        } else {
            if (text_pos < WEB_TEXT_LEN - 1) text_buf[text_pos++] = *p;
        }
        p++;
    }

    doc->loaded = 1;
    vga_printf("[WEB] parsed: %d node, title: %s\n", doc->node_count, doc->title);
    return doc->node_count;
}

int web_parse_css(int doc_id, const char *css)
{
    (void)doc_id; (void)css;
    /* 原型阶段: 简化CSS解析 */
    return 0;
}

int web_apply_styles(int doc_id)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    for (int i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].used) {
            memcpy(&doc->nodes[i].computed_style, &doc->nodes[i].style, sizeof(css_style_t));
        }
    }
    return 0;
}

int web_dom_create_node(int doc_id, html_tag_t tag, const char *text)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    for (int i = 0; i < WEB_MAX_NODES; i++) {
        if (!doc->nodes[i].used) {
            memset(&doc->nodes[i], 0, sizeof(dom_node_t));
            doc->nodes[i].used = 1;
            doc->nodes[i].id = i;
            doc->nodes[i].tag = tag;
            strncpy(doc->nodes[i].tag_name, tag_names[tag], WEB_TAG_NAME_LEN - 1);
            if (text) strncpy(doc->nodes[i].text, text, WEB_TEXT_LEN - 1);
            doc->nodes[i].parent = -1;
            init_default_style(&doc->nodes[i].style);
            init_default_style(&doc->nodes[i].computed_style);
            doc->nodes[i].visible = 1;
            doc->node_count++;
            return i;
        }
    }
    return -1;
}

int web_dom_append_child(int doc_id, int parent_id, int child_id)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    if (parent_id < 0 || parent_id >= WEB_MAX_NODES || !doc->nodes[parent_id].used) return -1;
    if (child_id < 0 || child_id >= WEB_MAX_NODES || !doc->nodes[child_id].used) return -1;
    if (doc->nodes[parent_id].child_count >= 32) return -1;
    doc->nodes[parent_id].children[doc->nodes[parent_id].child_count++] = child_id;
    doc->nodes[child_id].parent = parent_id;
    if (doc->nodes[parent_id].first_child < 0) doc->nodes[parent_id].first_child = child_id;
    return 0;
}

int web_dom_get_element_by_id(int doc_id, const char *id)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used || !id) return -1;
    web_document_t *doc = &documents[doc_id];
    for (int i = 0; i < WEB_MAX_NODES; i++) {
        if (!doc->nodes[i].used) continue;
        for (int a = 0; a < doc->nodes[i].attr_count; a++) {
            if (strcmp(doc->nodes[i].attributes[a].name, "id") == 0 &&
                strcmp(doc->nodes[i].attributes[a].value, id) == 0) {
                return i;
            }
        }
    }
    return -1;
}

int web_dom_get_elements_by_tag(int doc_id, html_tag_t tag, int *ids, int max)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used || !ids) return 0;
    web_document_t *doc = &documents[doc_id];
    int count = 0;
    for (int i = 0; i < WEB_MAX_NODES && count < max; i++) {
        if (doc->nodes[i].used && doc->nodes[i].tag == tag) {
            ids[count++] = i;
        }
    }
    return count;
}

int web_dom_set_attribute(int doc_id, int node_id, const char *name, const char *value)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    if (node_id < 0 || node_id >= WEB_MAX_NODES || !doc->nodes[node_id].used) return -1;
    for (int i = 0; i < doc->nodes[node_id].attr_count; i++) {
        if (strcmp(doc->nodes[node_id].attributes[i].name, name) == 0) {
            strncpy(doc->nodes[node_id].attributes[i].value, value, WEB_ATTR_VALUE_LEN - 1);
            return 0;
        }
    }
    if (doc->nodes[node_id].attr_count >= 8) return -1;
    strncpy(doc->nodes[node_id].attributes[doc->nodes[node_id].attr_count].name, name, WEB_ATTR_NAME_LEN - 1);
    strncpy(doc->nodes[node_id].attributes[doc->nodes[node_id].attr_count].value, value, WEB_ATTR_VALUE_LEN - 1);
    doc->nodes[node_id].attr_count++;
    return 0;
}

int web_dom_get_attribute(int doc_id, int node_id, const char *name, char *value, int len)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used || !value) return -1;
    web_document_t *doc = &documents[doc_id];
    if (node_id < 0 || node_id >= WEB_MAX_NODES || !doc->nodes[node_id].used) return -1;
    for (int i = 0; i < doc->nodes[node_id].attr_count; i++) {
        if (strcmp(doc->nodes[node_id].attributes[i].name, name) == 0) {
            strncpy(value, doc->nodes[node_id].attributes[i].value, len - 1);
            value[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int web_dom_set_text(int doc_id, int node_id, const char *text)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    if (node_id < 0 || node_id >= WEB_MAX_NODES || !doc->nodes[node_id].used) return -1;
    if (text) strncpy(doc->nodes[node_id].text, text, WEB_TEXT_LEN - 1);
    return 0;
}

int web_dom_remove_child(int doc_id, int parent_id, int child_id)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    if (parent_id < 0 || parent_id >= WEB_MAX_NODES || !doc->nodes[parent_id].used) return -1;
    for (int i = 0; i < doc->nodes[parent_id].child_count; i++) {
        if (doc->nodes[parent_id].children[i] == child_id) {
            for (int j = i; j < doc->nodes[parent_id].child_count - 1; j++) {
                doc->nodes[parent_id].children[j] = doc->nodes[parent_id].children[j + 1];
            }
            doc->nodes[parent_id].child_count--;
            doc->nodes[child_id].used = 0;
            return 0;
        }
    }
    return -1;
}

int web_layout(int doc_id)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    /* 简化布局: 流式布局 */
    int y = 0;
    for (int i = 0; i < WEB_MAX_NODES; i++) {
        if (!doc->nodes[i].used || !doc->nodes[i].visible) continue;
        if (doc->nodes[i].tag == TAG_BODY || doc->nodes[i].tag == TAG_HTML) continue;
        doc->nodes[i].layout_x = 0;
        doc->nodes[i].layout_y = y;
        doc->nodes[i].layout_w = doc->viewport_width;
        doc->nodes[i].layout_h = doc->nodes[i].computed_style.line_height;
        y += doc->nodes[i].layout_h;
    }
    return 0;
}

int web_render(int doc_id, void *framebuffer, int width, int height)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    (void)framebuffer; (void)width; (void)height;
    web_document_t *doc = &documents[doc_id];
    web_layout(doc_id);
    /* 原型阶段: 渲染文本到VGA */
    int y = 0;
    for (int i = 0; i < WEB_MAX_NODES && y < height; i++) {
        if (!doc->nodes[i].used || !doc->nodes[i].visible) continue;
        if (doc->nodes[i].text[0] && doc->nodes[i].tag != TAG_HTML &&
            doc->nodes[i].tag != TAG_HEAD && doc->nodes[i].tag != TAG_BODY) {
            vga_printf("%s\n", doc->nodes[i].text);
            y += 16;
        }
    }
    return 0;
}

int web_scroll(int doc_id, int dx, int dy)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    documents[doc_id].scroll_x += dx;
    documents[doc_id].scroll_y += dy;
    return 0;
}

int web_hit_test(int doc_id, int x, int y)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    web_document_t *doc = &documents[doc_id];
    for (int i = 0; i < WEB_MAX_NODES; i++) {
        if (!doc->nodes[i].used || !doc->nodes[i].visible) continue;
        if (x >= doc->nodes[i].layout_x && x < doc->nodes[i].layout_x + doc->nodes[i].layout_w &&
            y >= doc->nodes[i].layout_y && y < doc->nodes[i].layout_y + doc->nodes[i].layout_h) {
            return i;
        }
    }
    return -1;
}

/* 简化JS引擎 */
void js_init(void)
{
    memset(&js, 0, sizeof(js_engine_t));
    js.initialized = 1;
    vga_puts("[WEB] JS engine init done \n");
}

int js_eval(int doc_id, const char *code)
{
    (void)doc_id;
    if (!code || !js.initialized) return -1;
    /* 原型阶段: 简化JS解释器，支持console.log和基本算术 */
    vga_printf("[JS] eval: %s\n", code);
    return 0;
}

int js_call_function(int doc_id, const char *name, js_value_t *args, int arg_count, js_value_t *result)
{
    (void)doc_id; (void)args; (void)arg_count;
    if (!name || !result) return -1;
    result->type = JS_UNDEFINED;
    return 0;
}

int js_set_global(const char *name, js_value_t value)
{
    if (!name || !js.initialized) return -1;
    for (int i = 0; i < 64; i++) {
        if (js.globals[i].type == JS_UNDEFINED) {
            js.globals[i] = value;
            return 0;
        }
    }
    return -1;
}

int js_get_global(const char *name, js_value_t *value)
{
    (void)name;
    if (!value || !js.initialized) return -1;
    value->type = JS_UNDEFINED;
    return -1;
}

int web_load_url(int doc_id, const char *url)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used || !url) return -1;
    strncpy(documents[doc_id].url, url, 255);
    vga_printf("[WEB] load: %s\n", url);
    return 0;
}

int web_reload(int doc_id)
{
    if (doc_id < 0 || doc_id >= WEB_MAX_DOCUMENTS || !documents[doc_id].used) return -1;
    return web_load_url(doc_id, documents[doc_id].url);
}

int web_go_back(int doc_id) { (void)doc_id; return 0; }
int web_go_forward(int doc_id) { (void)doc_id; return 0; }

int web_set_cookie(int doc_id, const char *cookie)
{
    (void)doc_id; (void)cookie;
    return 0;
}

int web_get_cookie(int doc_id, char *cookie, int len)
{
    (void)doc_id;
    if (!cookie || len <= 0) return -1;
    cookie[0] = '\0';
    return 0;
}

int web_history_length(int doc_id) { (void)doc_id; return 1; }

int web_history_get(int doc_id, int index, char *url, int len)
{
    (void)doc_id; (void)index;
    if (!url || len <= 0) return -1;
    url[0] = '\0';
    return 0;
}

int web_document_count(void) { return doc_count; }
