/* ============================================================================
 * 赫普OS - Hepu Desktop QML (hsqml) 声明式UI语言解析器实现
 * V0.10.162
 * ========================================================================== */

#include "hsqml.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

/* 简单字符串操作 */
static int str_len(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

static int str_cmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static void str_cpy(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_alnum(char c)
{
    return is_alpha(c) || is_digit(c);
}

static int is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* ============================================================================
 * 词法分析器
 * ========================================================================== */

typedef struct {
    const char *src;
    int pos;
    int len;
    int line;
    int col;
} lexer_t;

static void lexer_init(lexer_t *lex, const char *src)
{
    lex->src = src;
    lex->pos = 0;
    lex->len = str_len(src);
    lex->line = 1;
    lex->col = 1;
}

static char lexer_peek(lexer_t *lex)
{
    if (lex->pos >= lex->len) return 0;
    return lex->src[lex->pos];
}

static char lexer_advance(lexer_t *lex)
{
    if (lex->pos >= lex->len) return 0;
    char c = lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else lex->col++;
    return c;
}

static void lexer_skip_space(lexer_t *lex)
{
    while (lex->pos < lex->len && is_space(lexer_peek(lex))) {
        lexer_advance(lex);
    }
    /* 跳过注释 // ... */
    if (lex->pos + 1 < lex->len &&
        lex->src[lex->pos] == '/' && lex->src[lex->pos + 1] == '/') {
        while (lex->pos < lex->len && lexer_peek(lex) != '\n') {
            lexer_advance(lex);
        }
        lexer_skip_space(lex);
    }
    /* 跳过块注释(星号斜杠 形式) */
    if (lex->pos + 1 < lex->len &&
        lex->src[lex->pos] == '/' && lex->src[lex->pos + 1] == '*') {
        lexer_advance(lex); lexer_advance(lex);
        while (lex->pos + 1 < lex->len &&
               !(lex->src[lex->pos] == '*' && lex->src[lex->pos + 1] == '/')) {
            lexer_advance(lex);
        }
        if (lex->pos + 1 < lex->len) {
            lexer_advance(lex); lexer_advance(lex);
        }
        lexer_skip_space(lex);
    }
}

static int lexer_next(lexer_t *lex, hsqml_token_t *tok)
{
    lexer_skip_space(lex);

    tok->line = lex->line;
    tok->col = lex->col;
    tok->str[0] = 0;
    tok->num = 0;
    tok->color = 0;

    if (lex->pos >= lex->len) {
        tok->type = TOK_EOF;
        return 0;
    }

    char c = lexer_peek(lex);

    /* 字符串 */
    if (c == '"') {
        lexer_advance(lex);  /* 跳过 " */
        int i = 0;
        while (lex->pos < lex->len && lexer_peek(lex) != '"' && i < HSQML_MAX_STR_LEN - 1) {
            char ch = lexer_advance(lex);
            if (ch == '\\' && lex->pos < lex->len) {
                char esc = lexer_advance(lex);
                switch (esc) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case '\\': ch = '\\'; break;
                    case '"': ch = '"'; break;
                    default: ch = esc; break;
                }
            }
            tok->str[i++] = ch;
        }
        tok->str[i] = 0;
        if (lex->pos < lex->len) lexer_advance(lex);  /* 跳过 " */
        tok->type = TOK_STRING;
        return 1;
    }

    /* 颜色 #RRGGBB */
    if (c == '#') {
        lexer_advance(lex);  /* 跳过 # */
        uint32_t color = 0;
        int hex_count = 0;
        while (lex->pos < lex->len && is_hex(lexer_peek(lex)) && hex_count < 6) {
            char h = lexer_advance(lex);
            int val;
            if (h >= '0' && h <= '9') val = h - '0';
            else if (h >= 'a' && h <= 'f') val = h - 'a' + 10;
            else val = h - 'A' + 10;
            color = (color << 4) | val;
            hex_count++;
        }
        tok->color = color;
        tok->type = TOK_COLOR;
        return 1;
    }

    /* 数字 */
    if (is_digit(c) || (c == '-' && lex->pos + 1 < lex->len && is_digit(lex->src[lex->pos + 1]))) {
        int neg = 0;
        if (c == '-') { lexer_advance(lex); neg = 1; }
        int val = 0;
        while (lex->pos < lex->len && is_digit(lexer_peek(lex))) {
            val = val * 10 + (lexer_advance(lex) - '0');
        }
        tok->num = neg ? -val : val;
        tok->type = TOK_NUMBER;
        return 1;
    }

    /* 标识符/关键字 */
    if (is_alpha(c)) {
        int i = 0;
        while (lex->pos < lex->len && is_alnum(lexer_peek(lex)) && i < HSQML_MAX_STR_LEN - 1) {
            tok->str[i++] = lexer_advance(lex);
        }
        tok->str[i] = 0;

        /* 关键字 */
        if (str_cmp(tok->str, "true") == 0) {
            tok->type = TOK_TRUE;
            tok->bool_val = 1;
        } else if (str_cmp(tok->str, "false") == 0) {
            tok->type = TOK_FALSE;
            tok->bool_val = 0;
        } else {
            tok->type = TOK_IDENT;
        }
        return 1;
    }

    /* 符号 */
    switch (c) {
        case '{': lexer_advance(lex); tok->type = TOK_LBRACE; return 1;
        case '}': lexer_advance(lex); tok->type = TOK_RBRACE; return 1;
        case ':': lexer_advance(lex); tok->type = TOK_COLON; return 1;
        case ';': lexer_advance(lex); tok->type = TOK_SEMICOLON; return 1;
        case ',': lexer_advance(lex); tok->type = TOK_COMMA; return 1;
        default:
            lexer_advance(lex);
            tok->type = TOK_EOF;
            return 1;
    }
}

/* ============================================================================
 * 语法分析器
 * ========================================================================== */

typedef struct {
    hsqml_token_t tokens[HSQML_MAX_TOKENS];
    int token_count;
    int pos;
    hsqml_result_t *result;
} parser_t;

static hsqml_node_t *parser_alloc_node(parser_t *p)
{
    if (p->result->node_count >= HSQML_MAX_NODES) return NULL;
    hsqml_node_t *node = &p->result->nodes[p->result->node_count++];
    /* 彻底清零节点 */
    for (int i = 0; i < HSQML_MAX_STR_LEN; i++) node->type[i] = 0;
    node->prop_count = 0;
    node->child_count = 0;
    for (int i = 0; i < HSQML_MAX_PROPS_PER_NODE; i++) node->props[i] = NULL;
    for (int i = 0; i < HSQML_MAX_CHILDREN; i++) node->children[i] = NULL;
    return node;
}

static hsqml_token_t *parser_peek(parser_t *p)
{
    if (p->pos >= p->token_count) return &p->tokens[p->token_count - 1];
    return &p->tokens[p->pos];
}

static hsqml_token_t *parser_advance(parser_t *p)
{
    if (p->pos >= p->token_count) return &p->tokens[p->token_count - 1];
    return &p->tokens[p->pos++];
}

static int parser_expect(parser_t *p, hsqml_token_type_t type)
{
    hsqml_token_t *tok = parser_peek(p);
    if (tok->type != type) {
        p->result->error = 1;
        str_cpy(p->result->error_msg, "Unexpected token");
        p->result->error_line = tok->line;
        return 0;
    }
    parser_advance(p);
    return 1;
}

static void parser_parse_property(parser_t *p, hsqml_node_t *node)
{
    if (node->prop_count >= HSQML_MAX_PROPS_PER_NODE) return;

    hsqml_token_t *name_tok = parser_advance(p);
    if (name_tok->type != TOK_IDENT) return;

    if (!parser_expect(p, TOK_COLON)) return;

    hsqml_token_t *val_tok = parser_advance(p);

    int supported = 1;
    hsqml_prop_type_t ptype = PROP_STRING;
    switch (val_tok->type) {
        case TOK_STRING: ptype = PROP_STRING; break;
        case TOK_NUMBER: ptype = PROP_NUMBER; break;
        case TOK_COLOR:  ptype = PROP_COLOR; break;
        case TOK_TRUE:   ptype = PROP_BOOL; break;
        case TOK_FALSE:  ptype = PROP_BOOL; break;
        default: supported = 0; break;
    }

    if (!supported) {
        if (parser_peek(p)->type == TOK_SEMICOLON) parser_advance(p);
        return;
    }

    /* 从属性池分配 */
    if (p->result->prop_pool_count >= HSQML_MAX_PROPS_TOTAL) {
        /* 旧实现静默丢弃, 配置后半段整体失效且无从察觉 */
        p->result->error = 1;
        const char *m = "too many properties";
        int i = 0;
        while (m[i] && i < HSQML_MAX_STR_LEN - 1) { p->result->error_msg[i] = m[i]; i++; }
        p->result->error_msg[i] = 0;
        return;
    }
    hsqml_property_t *prop = &p->result->prop_pool[p->result->prop_pool_count++];

    /* 清零属性 */
    for (int i = 0; i < HSQML_MAX_STR_LEN; i++) prop->name[i] = 0;
    for (int i = 0; i < HSQML_MAX_STR_LEN; i++) prop->str_val[i] = 0;
    prop->num_val = 0;
    prop->color_val = 0;
    prop->bool_val = 0;

    node->props[node->prop_count++] = prop;
    str_cpy(prop->name, name_tok->str);
    prop->type = ptype;

    switch (val_tok->type) {
        case TOK_STRING: str_cpy(prop->str_val, val_tok->str); break;
        case TOK_NUMBER: prop->num_val = val_tok->num; break;
        case TOK_COLOR:  prop->color_val = val_tok->color; break;
        case TOK_TRUE:   prop->bool_val = 1; break;
        case TOK_FALSE:  prop->bool_val = 0; break;
        default: break;
    }

    if (parser_peek(p)->type == TOK_SEMICOLON) parser_advance(p);
}

static void parser_parse_node(parser_t *p, hsqml_node_t *parent)
{
    hsqml_token_t *type_tok = parser_advance(p);  /* 节点类型 */
    if (type_tok->type != TOK_IDENT) return;

    if (!parser_expect(p, TOK_LBRACE)) return;

    hsqml_node_t *node = parser_alloc_node(p);
    if (!node) return;
    str_cpy(node->type, type_tok->str);

    if (parent) {
        if (parent->child_count < HSQML_MAX_CHILDREN) {
            parent->children[parent->child_count++] = node;
        }
    } else {
        if (p->result->root_count < HSQML_MAX_NODES) {
            p->result->roots[p->result->root_count++] = node;
        }
    }

    while (parser_peek(p)->type != TOK_RBRACE &&
           parser_peek(p)->type != TOK_EOF) {
        hsqml_token_t *tok = parser_peek(p);

        if (tok->type == TOK_IDENT) {
            /* 看下一个token是:还是{ */
            int save_pos = p->pos;
            parser_advance(p);  /* 跳过ident */
            if (parser_peek(p)->type == TOK_COLON) {
                p->pos = save_pos;
                parser_parse_property(p, node);
            } else if (parser_peek(p)->type == TOK_LBRACE) {
                p->pos = save_pos;
                parser_parse_node(p, node);
            } else {
                p->pos = save_pos;
                parser_advance(p);  /* 跳过未知 */
            }
        } else {
            parser_advance(p);  /* 跳过未知token */
        }
    }

    if (parser_peek(p)->type == TOK_RBRACE) {
        parser_advance(p);
    }
}

/* ============================================================================
 * 公共API
 * ========================================================================== */

int hsqml_parse(const char *source, hsqml_result_t *result)
{
    if (!source || !result) return -1;

    /* 彻底清零result关键字段，避免旧数据残留 */
    result->node_count = 0;
    result->root_count = 0;
    result->prop_pool_count = 0;
    result->error = 0;
    result->error_msg[0] = 0;
    result->error_line = 0;
    for (int i = 0; i < HSQML_MAX_NODES; i++) {
        result->roots[i] = NULL;
    }

    /* 词法分析 */
    lexer_t lex;
    lexer_init(&lex, source);

    /* parser_t含256个token约22KB，静态分配避免栈溢出 */
    static parser_t parser;
    parser.token_count = 0;
    parser.pos = 0;
    parser.result = result;

    while (parser.token_count < HSQML_MAX_TOKENS - 1) {
        hsqml_token_t *tok = &parser.tokens[parser.token_count];
        if (!lexer_next(&lex, tok)) break;
        parser.token_count++;
        if (tok->type == TOK_EOF) break;
    }
    /* 旧上限只有256个token, 默认配置(约300个)尾部一直被静默截断,
     * 且截断点落在半个节点上会让解析卡死。超限必须显式报错。 */
    if (parser.token_count >= HSQML_MAX_TOKENS - 1) {
        result->error = 2;
        const char *m = "config too large (token limit)";
        int i = 0;
        while (m[i] && i < HSQML_MAX_STR_LEN - 1) { result->error_msg[i] = m[i]; i++; }
        result->error_msg[i] = 0;
    }
    /* 添加EOF token（带边界检查） */
    if (parser.token_count < HSQML_MAX_TOKENS) {
        parser.tokens[parser.token_count].type = TOK_EOF;
        parser.tokens[parser.token_count].line = lex.line;
        parser.token_count++;
    }

    /* 语法分析 */
    while (parser.pos < parser.token_count &&
           parser_peek(&parser)->type != TOK_EOF) {
        if (parser_peek(&parser)->type == TOK_IDENT) {
            parser_parse_node(&parser, NULL);
        } else {
            parser_advance(&parser);
        }
    }

    return result->error ? -1 : result->root_count;
}

/* 属性查询 */
hsqml_property_t *hsqml_get_prop(hsqml_node_t *node, const char *name)
{
    if (!node || !name) return NULL;
    for (int i = 0; i < node->prop_count; i++) {
        if (node->props[i] && str_cmp(node->props[i]->name, name) == 0) {
            return node->props[i];
        }
    }
    return NULL;
}

const char *hsqml_get_string(hsqml_node_t *node, const char *name, const char *def)
{
    hsqml_property_t *p = hsqml_get_prop(node, name);
    if (p && p->type == PROP_STRING) return p->str_val;
    return def;
}

int hsqml_get_number(hsqml_node_t *node, const char *name, int def)
{
    hsqml_property_t *p = hsqml_get_prop(node, name);
    if (p && p->type == PROP_NUMBER) return p->num_val;
    return def;
}

uint32_t hsqml_get_color(hsqml_node_t *node, const char *name, uint32_t def)
{
    hsqml_property_t *p = hsqml_get_prop(node, name);
    if (p && p->type == PROP_COLOR) return p->color_val;
    return def;
}

int hsqml_get_bool(hsqml_node_t *node, const char *name, int def)
{
    hsqml_property_t *p = hsqml_get_prop(node, name);
    if (p && p->type == PROP_BOOL) return p->bool_val;
    return def;
}

/* 子节点查询 */
hsqml_node_t *hsqml_get_child(hsqml_node_t *node, const char *type, int index)
{
    if (!node || !type) return NULL;
    int count = 0;
    for (int i = 0; i < node->child_count; i++) {
        if (str_cmp(node->children[i]->type, type) == 0) {
            if (count == index) return node->children[i];
            count++;
        }
    }
    return NULL;
}

int hsqml_count_children(hsqml_node_t *node, const char *type)
{
    if (!node || !type) return 0;
    int count = 0;
    for (int i = 0; i < node->child_count; i++) {
        if (str_cmp(node->children[i]->type, type) == 0) count++;
    }
    return count;
}

/* 调试输出 */
static void dump_node(hsqml_node_t *node, int indent)
{
    for (int i = 0; i < indent; i++) vga_puts("  ");
    vga_puts(node->type);
    vga_puts(" {\n");

    for (int i = 0; i < node->prop_count; i++) {
        if (!node->props[i]) continue;
        for (int j = 0; j < indent + 1; j++) vga_puts("  ");
        vga_puts(node->props[i]->name);
        vga_puts(": ");
        switch (node->props[i]->type) {
            case PROP_STRING:
                vga_puts("\"");
                vga_puts(node->props[i]->str_val);
                vga_puts("\"");
                break;
            case PROP_NUMBER: {
                char buf[16];
                int n = node->props[i]->num_val, k = 0;
                if (n == 0) buf[k++] = '0';
                while (n > 0) { buf[k++] = '0' + n % 10; n /= 10; }
                buf[k] = 0;
                for (int j = 0; j < k / 2; j++) {
                    char t = buf[j]; buf[j] = buf[k-1-j]; buf[k-1-j] = t;
                }
                vga_puts(buf);
                break;
            }
            case PROP_COLOR:
                vga_puts("#");
                vga_put_hex(node->props[i]->color_val);
                break;
            case PROP_BOOL:
                vga_puts(node->props[i]->bool_val ? "true" : "false");
                break;
        }
        vga_puts("\n");
    }

    for (int i = 0; i < node->child_count; i++) {
        dump_node(node->children[i], indent + 1);
    }

    for (int i = 0; i < indent; i++) vga_puts("  ");
    vga_puts("}\n");
}

void hsqml_dump(hsqml_result_t *result)
{
    if (!result) return;
    vga_puts("[HSQML] Roots: ");
    char buf[8];
    int n = result->root_count, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(", Nodes: ");
    n = result->node_count; i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts("\n");

    for (int i = 0; i < result->root_count; i++) {
        dump_node(result->roots[i], 0);
    }
}
