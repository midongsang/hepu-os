#ifndef DTB_H
#define DTB_H

#include <stdint.h>

#define DTB_MAGIC 0xD00DFEED
#define DTB_MAX_NODES 64
#define DTB_MAX_PROPS 128

/* DTB头部 */
typedef struct {
    uint32_t magic;
    uint32_t total_size;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} __attribute__((packed)) dtb_header_t;

/* DTB token */
#define DTB_BEGIN_NODE 0x00000001
#define DTB_END_NODE   0x00000002
#define DTB_PROP       0x00000003
#define DTB_NOP        0x00000004
#define DTB_END        0x00000009

/* 属性 */
typedef struct {
    char name[64];
    uint32_t length;
    uint8_t  *data;
} dtb_prop_t;

/* 设备树节点 */
typedef struct {
    char name[64];
    char path[128];
    dtb_prop_t props[16];
    int prop_count;
    int parent;
    int children[16];
    int child_count;
    int depth;
} dtb_node_t;

/* 初始化设备树解析器 (NULL=内置 Hepu 平台树) */
int dtb_init(void *dtb_data);

/* V4.20-E65: 内置平台树构建 + 树形显示 */
void dtb_build_builtin(void);
void dtb_show(void);
void dtb_self_check(void);
int dtb_node_count(void);
dtb_node_t *dtb_node_at(int i);

/* 验证DTB魔数 */
int dtb_verify(void *dtb_data);

/* 解析DTB */
int dtb_parse(void *dtb_data);

/* 查找节点 */
dtb_node_t *dtb_find_node(const char *path);

/* 查找节点属性 */
dtb_prop_t *dtb_find_prop(dtb_node_t *node, const char *name);

/* 获取属性值 (32位整数) */
uint32_t dtb_get_prop_u32(dtb_node_t *node, const char *name, uint32_t def);

/* 获取属性值 (64位整数) */
uint64_t dtb_get_prop_u64(dtb_node_t *node, const char *name, uint64_t def);

/* 获取属性字符串 */
const char *dtb_get_prop_string(dtb_node_t *node, const char *name);

/* 获取属性寄存器地址 */
uint32_t dtb_get_reg(dtb_node_t *node, int index);

/* 获取中断号 */
int dtb_get_irq(dtb_node_t *node, int index);

/* 获取兼容字符串 */
const char *dtb_get_compatible(dtb_node_t *node);

/* 遍历节点 */
int dtb_for_each_node(int (*callback)(dtb_node_t *node, void *data), void *data);

/* 硬件自动配置 */
int dtb_auto_config(void);

/* 获取节点数量 */
int dtb_node_count(void);

/* 打印设备树 */
void dtb_dump(void);

#endif /* DTB_H */
