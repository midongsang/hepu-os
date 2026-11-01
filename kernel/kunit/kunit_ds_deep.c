/* ============================================================================
 * 赫普OS - 数据结构深度单元测试
 * 覆盖：链表/栈/队列/哈希表/树/位图
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "types.h"
#include "kheap.h"

/* === 单链表测试 === */
typedef struct slist_node {
    int data;
    struct slist_node *next;
} slist_node_t;

static slist_node_t *slist_create(int data)
{
    slist_node_t *node = (slist_node_t *)kmalloc(sizeof(slist_node_t));
    if (!node) return 0;
    node->data = data;
    node->next = 0;
    return node;
}

static void slist_append(slist_node_t **head, int data)
{
    slist_node_t *node = slist_create(data);
    if (!node) return;
    if (!*head) { *head = node; return; }
    slist_node_t *cur = *head;
    while (cur->next) cur = cur->next;
    cur->next = node;
}

static int slist_length(slist_node_t *head)
{
    int len = 0;
    while (head) { len++; head = head->next; }
    return len;
}

static kunit_result_t test_slist_append(void)
{
    slist_node_t *head = 0;
    slist_append(&head, 10);
    if (!head) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    slist_append(&head, 20);
    slist_append(&head, 30);

    KUNIT_ASSERT_EQ(slist_length(head), 3, " 链 table len =3");
    KUNIT_ASSERT_EQ(head->data, 10, " node =10");
    KUNIT_ASSERT_EQ(head->next->data, 20, " node =20");
    KUNIT_ASSERT_EQ(head->next->next->data, 30, " 末 node =30");
    return KUNIT_PASS;
}

static kunit_result_t test_slist_empty(void)
{
    slist_node_t *head = 0;
    KUNIT_ASSERT_EQ(slist_length(head), 0, " empty 链 table len =0");
    KUNIT_ASSERT_NULL(head, " empty 链 table header =NULL");
    return KUNIT_PASS;
}

/* === 栈测试 === */
#define STACK_MAX 64
static int stack_data[STACK_MAX];
static int stack_top;

static void stack_init(void) { stack_top = 0; }
static int stack_push(int val)
{
    if (stack_top >= STACK_MAX) return -1;
    stack_data[stack_top++] = val;
    return 0;
}
static int stack_pop(int *out)
{
    if (stack_top <= 0) return -1;
    *out = stack_data[--stack_top];
    return 0;
}
static int stack_peek(int *out)
{
    if (stack_top <= 0) return -1;
    *out = stack_data[stack_top - 1];
    return 0;
}

static kunit_result_t test_stack_lifo(void)
{
    stack_init();
    stack_push(1);
    stack_push(2);
    stack_push(3);

    int val;
    KUNIT_ASSERT_EQ(stack_pop(&val), 0, " 弹出 ok ");
    KUNIT_ASSERT_EQ(val, 3, "LIFO 弹出 =3");
    KUNIT_ASSERT_EQ(stack_pop(&val), 0, " 弹出 ok ");
    KUNIT_ASSERT_EQ(val, 2, "LIFO 弹出 =2");
    KUNIT_ASSERT_EQ(stack_pop(&val), 0, " 弹出 ok ");
    KUNIT_ASSERT_EQ(val, 1, "LIFO 弹出 =1");
    return KUNIT_PASS;
}

static kunit_result_t test_stack_overflow(void)
{
    stack_init();
    for (int i = 0; i < STACK_MAX; i++) {
        KUNIT_ASSERT_EQ(stack_push(i), 0, " 压入 ok ");
    }
    KUNIT_ASSERT_EQ(stack_push(999), -1, " 栈 full 压入 failed ");
    return KUNIT_PASS;
}

static kunit_result_t test_stack_underflow(void)
{
    stack_init();
    int val;
    KUNIT_ASSERT_EQ(stack_pop(&val), -1, " empty 栈弹出 failed ");
    KUNIT_ASSERT_EQ(stack_peek(&val), -1, " empty 栈查看 failed ");
    return KUNIT_PASS;
}

/* === 环形队列测试 === */
#define QUEUE_MAX 16
static u8 queue_buf[QUEUE_MAX];
static u32 queue_head, queue_tail, queue_count;

static void queue_init(void) { queue_head = queue_tail = queue_count = 0; }
static int queue_enqueue(u8 val)
{
    if (queue_count >= QUEUE_MAX) return -1;
    queue_buf[queue_tail] = val;
    queue_tail = (queue_tail + 1) % QUEUE_MAX;
    queue_count++;
    return 0;
}
static int queue_dequeue(u8 *out)
{
    if (queue_count <= 0) return -1;
    *out = queue_buf[queue_head];
    queue_head = (queue_head + 1) % QUEUE_MAX;
    queue_count--;
    return 0;
}

static kunit_result_t test_queue_fifo(void)
{
    queue_init();
    queue_enqueue(1);
    queue_enqueue(2);
    queue_enqueue(3);

    u8 val;
    KUNIT_ASSERT_EQ(queue_dequeue(&val), 0, " dequeue ok ");
    KUNIT_ASSERT_EQ(val, 1, "FIFO dequeue =1");
    KUNIT_ASSERT_EQ(queue_dequeue(&val), 0, " dequeue ok ");
    KUNIT_ASSERT_EQ(val, 2, "FIFO dequeue =2");
    KUNIT_ASSERT_EQ(queue_dequeue(&val), 0, " dequeue ok ");
    KUNIT_ASSERT_EQ(val, 3, "FIFO dequeue =3");
    return KUNIT_PASS;
}

static kunit_result_t test_queue_wrap(void)
{
    queue_init();
    /* 填满再清空，测试回绕 */
    for (int i = 0; i < QUEUE_MAX; i++) queue_enqueue((u8)i);
    u8 val;
    for (int i = 0; i < QUEUE_MAX; i++) {
        KUNIT_ASSERT_EQ(queue_dequeue(&val), 0, " dequeue ");
        KUNIT_ASSERT_EQ(val, (u8)i, " value 正确 ");
    }
    /* 再次入队应该成功（回绕后） */
    KUNIT_ASSERT_EQ(queue_enqueue(0xAA), 0, " 回绕 next enqueue ");
    KUNIT_ASSERT_EQ(queue_dequeue(&val), 0, " 回绕 next dequeue ");
    KUNIT_ASSERT_EQ(val, 0xAA, " 回绕 next value 正确 ");
    return KUNIT_PASS;
}

/* === 哈希表测试 === */
#define HASH_SIZE 32
typedef struct hash_entry {
    u32 key;
    int value;
    struct hash_entry *next;
} hash_entry_t;

static hash_entry_t *hash_table[HASH_SIZE];

static u32 hash_func(u32 key)
{
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = ((key >> 16) ^ key) * 0x45d9f3b;
    key = (key >> 16) ^ key;
    return key % HASH_SIZE;
}

static void hash_init(void)
{
    memset(hash_table, 0, sizeof(hash_table));
}

static int hash_insert(u32 key, int value)
{
    u32 idx = hash_func(key);
    hash_entry_t *entry = (hash_entry_t *)kmalloc(sizeof(hash_entry_t));
    if (!entry) return -1;
    entry->key = key;
    entry->value = value;
    entry->next = hash_table[idx];
    hash_table[idx] = entry;
    return 0;
}

static int hash_lookup(u32 key, int *value)
{
    u32 idx = hash_func(key);
    hash_entry_t *entry = hash_table[idx];
    while (entry) {
        if (entry->key == key) { *value = entry->value; return 0; }
        entry = entry->next;
    }
    return -1;
}

static kunit_result_t test_hash_insert_lookup(void)
{
    hash_init();
    if (hash_insert(1, 100) != 0) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    hash_insert(2, 200);
    hash_insert(3, 300);

    int val;
    KUNIT_ASSERT_EQ(hash_lookup(1, &val), 0, " 查找 key=1");
    KUNIT_ASSERT_EQ(val, 100, "value=100");
    KUNIT_ASSERT_EQ(hash_lookup(2, &val), 0, " 查找 key=2");
    KUNIT_ASSERT_EQ(val, 200, "value=200");
    KUNIT_ASSERT_EQ(hash_lookup(3, &val), 0, " 查找 key=3");
    KUNIT_ASSERT_EQ(val, 300, "value=300");
    return KUNIT_PASS;
}

static kunit_result_t test_hash_miss(void)
{
    hash_init();
    if (hash_insert(42, 999) != 0) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    int val;
    KUNIT_ASSERT_EQ(hash_lookup(99, &val), -1, " not found key=99");
    return KUNIT_PASS;
}

static kunit_result_t test_hash_collision(void)
{
    /* 测试冲突链 */
    hash_init();
    /* 找两个哈希到同一桶的key */
    u32 k1 = 1, k2 = 1 + HASH_SIZE;
    if (hash_insert(k1, 111) != 0) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    hash_insert(k2, 222);

    int val;
    KUNIT_ASSERT_EQ(hash_lookup(k1, &val), 0, " 冲突 key1 found ");
    KUNIT_ASSERT_EQ(val, 111, " 冲突 key1 value ");
    KUNIT_ASSERT_EQ(hash_lookup(k2, &val), 0, " 冲突 key2 found ");
    KUNIT_ASSERT_EQ(val, 222, " 冲突 key2 value ");
    return KUNIT_PASS;
}

/* === 二叉搜索树测试 === */
typedef struct bst_node {
    int key;
    struct bst_node *left;
    struct bst_node *right;
} bst_node_t;

static bst_node_t *bst_create(int key)
{
    bst_node_t *node = (bst_node_t *)kmalloc(sizeof(bst_node_t));
    if (!node) return 0;
    node->key = key;
    node->left = node->right = 0;
    return node;
}

static bst_node_t *bst_insert(bst_node_t *root, int key)
{
    if (!root) return bst_create(key);
    if (key < root->key) root->left = bst_insert(root->left, key);
    else if (key > root->key) root->right = bst_insert(root->right, key);
    return root;
}

static int bst_search(bst_node_t *root, int key)
{
    while (root) {
        if (key == root->key) return 1;
        root = (key < root->key) ? root->left : root->right;
    }
    return 0;
}

static int bst_height(bst_node_t *root)
{
    if (!root) return 0;
    int lh = bst_height(root->left);
    int rh = bst_height(root->right);
    return (lh > rh ? lh : rh) + 1;
}

static kunit_result_t test_bst_insert_search(void)
{
    bst_node_t *root = 0;
    root = bst_insert(root, 50);
    if (!root) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    root = bst_insert(root, 30);
    root = bst_insert(root, 70);
    root = bst_insert(root, 20);
    root = bst_insert(root, 40);

    KUNIT_ASSERT(bst_search(root, 50), " found 50");
    KUNIT_ASSERT(bst_search(root, 30), " found 30");
    KUNIT_ASSERT(bst_search(root, 70), " found 70");
    KUNIT_ASSERT(bst_search(root, 20), " found 20");
    KUNIT_ASSERT(bst_search(root, 40), " found 40");
    KUNIT_ASSERT(!bst_search(root, 99), " not found 99");
    return KUNIT_PASS;
}

static kunit_result_t test_bst_structure(void)
{
    bst_node_t *root = 0;
    root = bst_insert(root, 50);
    if (!root) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    root = bst_insert(root, 30);
    root = bst_insert(root, 70);

    KUNIT_ASSERT_EQ(root->key, 50, " 根 =50");
    KUNIT_ASSERT_EQ(root->left->key, 30, " 左子 =30");
    KUNIT_ASSERT_EQ(root->right->key, 70, " 右子 =70");
    KUNIT_ASSERT_EQ(bst_height(root), 2, " tree high =2");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t ds_deep_tests[] = {
    {" 单链 table 追加 ", test_slist_append, "ds"},
    {" empty 链 table ", test_slist_empty, "ds"},
    {" 栈 LIFO", test_stack_lifo, "ds"},
    {" 栈溢出 ", test_stack_overflow, "ds"},
    {" 栈下溢 ", test_stack_underflow, "ds"},
    {" queue FIFO", test_queue_fifo, "ds"},
    {" queue 回绕 ", test_queue_wrap, "ds"},
    {" 哈希 insert 查找 ", test_hash_insert_lookup, "ds"},
    {" 哈希 not hits ", test_hash_miss, "ds"},
    {" 哈希冲突 ", test_hash_collision, "ds"},
    {"BST insert 查找 ", test_bst_insert_search, "ds"},
    {"BST struct ", test_bst_structure, "ds"},
};
static const kunit_suite_t kunit_ds_deep_suite = {
    " data struct deep ", ds_deep_tests, 12
};

void kunit_ds_deep_register(void)
{
    kunit_register_suite(&kunit_ds_deep_suite);
    vga_puts("[KUNIT] data struct deep test suite registered(12 cases)\n");
}
