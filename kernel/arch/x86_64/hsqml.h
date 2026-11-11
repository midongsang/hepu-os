/* ============================================================================
 * 赫普OS - Hepu Desktop QML (hsqml) 声明式UI语言解析器
 * 简化版QML，用于定义桌面界面
 * V0.10.162
 * ========================================================================== */

#ifndef HSQML_H
#define HSQML_H

#include <stdint.h>
#include <stddef.h>

/* 最大配置：指针数组+属性池，大幅减小体积 */
#define HSQML_MAX_NODES         32
#define HSQML_MAX_PROPS_PER_NODE 96
#define HSQML_MAX_PROPS_TOTAL    640
#define HSQML_MAX_CHILDREN       8
#define HSQML_MAX_STR_LEN        64
#define HSQML_MAX_TOKENS         1024

/* Token类型 */
typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,       /* 标识符 */
    TOK_STRING,      /* 字符串 */
    TOK_NUMBER,      /* 数字 */
    TOK_COLOR,       /* 颜色 #RRGGBB */
    TOK_LBRACE,      /* { */
    TOK_RBRACE,      /* } */
    TOK_COLON,       /* : */
    TOK_SEMICOLON,   /* ; */
    TOK_COMMA,       /* , */
    TOK_TRUE,        /* true */
    TOK_FALSE,       /* false */
} hsqml_token_type_t;

/* Token */
typedef struct {
    hsqml_token_type_t type;
    char str[HSQML_MAX_STR_LEN];
    int num;
    uint32_t color;
    int bool_val;
    int line;
    int col;
} hsqml_token_t;

/* 属性值类型 */
typedef enum {
    PROP_STRING = 0,
    PROP_NUMBER,
    PROP_COLOR,
    PROP_BOOL,
} hsqml_prop_type_t;

/* 属性 */
typedef struct {
    char name[HSQML_MAX_STR_LEN];
    hsqml_prop_type_t type;
    char str_val[HSQML_MAX_STR_LEN];
    int num_val;
    uint32_t color_val;
    int bool_val;
} hsqml_property_t;

/* AST节点 */
typedef struct hsqml_node {
    char type[HSQML_MAX_STR_LEN];
    int prop_count;
    hsqml_property_t *props[HSQML_MAX_PROPS_PER_NODE];  /* 指针数组 */
    int child_count;
    struct hsqml_node *children[HSQML_MAX_CHILDREN];
} hsqml_node_t;

/* 解析结果 */
typedef struct {
    int node_count;
    hsqml_node_t nodes[HSQML_MAX_NODES];
    int root_count;
    hsqml_node_t *roots[HSQML_MAX_NODES];
    int prop_pool_count;
    hsqml_property_t prop_pool[HSQML_MAX_PROPS_TOTAL];  /* 属性池 */
    int error;
    char error_msg[256];
    int error_line;
} hsqml_result_t;

/* API */
int hsqml_parse(const char *source, hsqml_result_t *result);
int hsqml_parse_file(const char *path, hsqml_result_t *result);

/* 属性查询 */
hsqml_property_t *hsqml_get_prop(hsqml_node_t *node, const char *name);
const char *hsqml_get_string(hsqml_node_t *node, const char *name, const char *def);
int hsqml_get_number(hsqml_node_t *node, const char *name, int def);
uint32_t hsqml_get_color(hsqml_node_t *node, const char *name, uint32_t def);
int hsqml_get_bool(hsqml_node_t *node, const char *name, int def);

/* 子节点查询 */
hsqml_node_t *hsqml_get_child(hsqml_node_t *node, const char *type, int index);
int hsqml_count_children(hsqml_node_t *node, const char *type);

/* 调试 */
void hsqml_dump(hsqml_result_t *result);

#endif /* HSQML_H */
