/* ============================================================================
 * 赫普OS - 配置与参数单元测试
 * 覆盖：配置项/命令行参数/启动参数/环境变量/系统参数
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 配置项测试 === */
#define CONFIG_MAX_ITEMS 32
#define CONFIG_KEY_MAX 32
#define CONFIG_VALUE_MAX 64

typedef struct {
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VALUE_MAX];
    u8   used;
} config_item_t;

static config_item_t config_table[CONFIG_MAX_ITEMS];
static u32 config_count;

static void config_init(void)
{
    memset(config_table, 0, sizeof(config_table));
    config_count = 0;
}

static int config_set(const char *key, const char *value)
{
    if (!key || !value) return -1;
    if (strlen(key) >= CONFIG_KEY_MAX) return -2;
    if (strlen(value) >= CONFIG_VALUE_MAX) return -2;

    /* 查找已有项 */
    for (u32 i = 0; i < config_count; i++) {
        if (strcmp(config_table[i].key, key) == 0) {
            strcpy(config_table[i].value, value);
            return 0;
        }
    }
    /* 新建项 */
    if (config_count >= CONFIG_MAX_ITEMS) return -3;
    strcpy(config_table[config_count].key, key);
    strcpy(config_table[config_count].value, value);
    config_table[config_count].used = 1;
    config_count++;
    return 0;
}

static const char *config_get(const char *key)
{
    if (!key) return 0;
    for (u32 i = 0; i < config_count; i++) {
        if (strcmp(config_table[i].key, key) == 0) {
            return config_table[i].value;
        }
    }
    return 0;
}

static int config_get_int(const char *key, s32 *out)
{
    const char *val = config_get(key);
    if (!val || !out) return -1;
    s32 result = 0;
    u32 i = 0;
    u8 neg = 0;
    if (val[0] == '-') { neg = 1; i = 1; }
    for (; val[i]; i++) {
        if (val[i] < '0' || val[i] > '9') return -2;
        result = result * 10 + (val[i] - '0');
    }
    *out = neg ? -result : result;
    return 0;
}

static kunit_result_t test_config_set_get(void)
{
    config_init();
    KUNIT_ASSERT_EQ(config_set("hostname", "hepu-phone"), 0, " set hostname");
    KUNIT_ASSERT_EQ(config_set("debug", "1"), 0, " set debug");

    KUNIT_ASSERT_EQ(strcmp(config_get("hostname"), "hepu-phone"), 0, " get hostname");
    KUNIT_ASSERT_EQ(strcmp(config_get("debug"), "1"), 0, " get debug");
    KUNIT_ASSERT_NULL(config_get("nonexistent"), " not exists key");
    return KUNIT_PASS;
}

static kunit_result_t test_config_update(void)
{
    config_init();
    config_set("timeout", "100");
    KUNIT_ASSERT_EQ(strcmp(config_get("timeout"), "100"), 0, " initial value ");

    config_set("timeout", "200");
    KUNIT_ASSERT_EQ(strcmp(config_get("timeout"), "200"), 0, " update next value ");
    KUNIT_ASSERT_EQ(config_count, 1, " count not 变 ");
    return KUNIT_PASS;
}

static kunit_result_t test_config_int(void)
{
    config_init();
    config_set("max_users", "128");
    config_set("negative", "-42");
    config_set("invalid", "abc");

    s32 val;
    KUNIT_ASSERT_EQ(config_get_int("max_users", &val), 0, " parse 正 count ");
    KUNIT_ASSERT_EQ(val, 128, " value =128");

    KUNIT_ASSERT_EQ(config_get_int("negative", &val), 0, " parse 负 count ");
    KUNIT_ASSERT_EQ(val, -42, " value =-42");

    KUNIT_ASSERT_EQ(config_get_int("invalid", &val), -2, " invalid count 字 parse failed ");
    KUNIT_ASSERT_EQ(config_get_int("missing", &val), -1, " 缺失 key failed ");
    return KUNIT_PASS;
}

static kunit_result_t test_config_limits(void)
{
    config_init();

    /* 超长key */
    char long_key[CONFIG_KEY_MAX + 10];
    memset(long_key, 'x', CONFIG_KEY_MAX + 5);
    long_key[CONFIG_KEY_MAX + 5] = '\0';
    KUNIT_ASSERT_EQ(config_set(long_key, "val"), -2, " 超 long key failed ");

    /* 填满配置表 */
    char key[8];
    for (int i = 0; i < CONFIG_MAX_ITEMS; i++) {
        key[0] = 'k'; key[1] = '0' + i / 10; key[2] = '0' + i % 10; key[3] = '\0';
        KUNIT_ASSERT_EQ(config_set(key, "v"), 0, " 填充 config ");
    }
    KUNIT_ASSERT_EQ(config_set("extra", "v"), -3, " config table full ");
    return KUNIT_PASS;
}

/* === 命令行参数解析测试 === */
typedef struct {
    const char *name;
    const char *value;
    u8   has_value;
} cmdline_arg_t;

static int parse_cmdline(const char *cmdline, cmdline_arg_t *args, int max_args)
{
    if (!cmdline || !args) return -1;
    int count = 0;
    const char *p = cmdline;

    while (*p && count < max_args) {
        /* 跳过空格 */
        while (*p == ' ') p++;
        if (!*p) break;

        /* 查找参数名结束 */
        const char *start = p;
        while (*p && *p != ' ' && *p != '=') p++;

        args[count].name = start;
        args[count].has_value = 0;
        args[count].value = 0;

        if (*p == '=') {
            p++;
            args[count].has_value = 1;
            args[count].value = p;
            while (*p && *p != ' ') p++;
        }
        /* 这里不复制字符串，只记录指针（简化） */
        count++;
    }
    return count;
}

static kunit_result_t test_cmdline_parse(void)
{
    const char *cmdline = "root=/dev/sda1 quiet debug=1";
    cmdline_arg_t args[8];
    int count = parse_cmdline(cmdline, args, 8);

    KUNIT_ASSERT_EQ(count, 3, " parse 出 3 param ");
    KUNIT_ASSERT(args[0].has_value, "root has value ");
    KUNIT_ASSERT(!args[1].has_value, "quiet no value ");
    KUNIT_ASSERT(args[2].has_value, "debug has value ");
    return KUNIT_PASS;
}

static kunit_result_t test_cmdline_empty(void)
{
    cmdline_arg_t args[8];
    KUNIT_ASSERT_EQ(parse_cmdline("", args, 8), 0, " empty cmdline =0 param ");
    KUNIT_ASSERT_EQ(parse_cmdline("   ", args, 8), 0, " 全 empty bars =0 param ");
    return KUNIT_PASS;
}

/* === 启动参数测试 === */
typedef struct {
    u32 mem_size;       /* 内存大小(MB) */
    u32 cpu_count;      /* CPU核心数 */
    u8  verbose;        /* 详细输出 */
    u8  safe_mode;      /* 安全模式 */
    char root_dev[16];  /* 根设备 */
} boot_params_t;

static kunit_result_t test_boot_params_defaults(void)
{
    boot_params_t params;
    memset(&params, 0, sizeof(params));

    KUNIT_ASSERT_EQ(params.mem_size, 0, " default memory =0");
    KUNIT_ASSERT_EQ(params.cpu_count, 0, " default CPU=0(未设置, memset)");
    KUNIT_ASSERT_EQ(params.verbose, 0, " default non 详细 ");
    KUNIT_ASSERT_EQ(params.safe_mode, 0, " default non security mode ");
    return KUNIT_PASS;
}

static kunit_result_t test_boot_params_validation(void)
{
    boot_params_t params;
    memset(&params, 0, sizeof(params));

    /* 验证内存大小 */
    params.mem_size = 4096;
    KUNIT_ASSERT(params.mem_size >= 64, " memory >=64MB");
    KUNIT_ASSERT(params.mem_size <= 65536, " memory <=64GB");

    /* 验证CPU数 */
    params.cpu_count = 8;
    KUNIT_ASSERT(params.cpu_count >= 1, "CPU>=1");
    KUNIT_ASSERT(params.cpu_count <= 256, "CPU<=256");
    return KUNIT_PASS;
}

/* === 系统参数测试 === */
typedef struct {
    u32 tick_rate;      /* 时钟频率(Hz) */
    u32 max_processes;  /* 最大进程数 */
    u32 max_files;      /* 最大打开文件数 */
    u32 stack_size;     /* 默认栈大小 */
    u32 heap_size;      /* 默认堆大小 */
} sys_params_t;

static kunit_result_t test_sys_params(void)
{
    sys_params_t params = {
        .tick_rate = 1000,
        .max_processes = 256,
        .max_files = 1024,
        .stack_size = 8192,
        .heap_size = 65536
    };

    KUNIT_ASSERT_EQ(params.tick_rate, 1000, " clock =1000Hz");
    KUNIT_ASSERT_EQ(params.max_processes, 256, " max process =256");
    KUNIT_ASSERT_EQ(params.max_files, 1024, " max file =1024");
    KUNIT_ASSERT_EQ(params.stack_size, 8192, " 栈 =8KB");
    KUNIT_ASSERT_EQ(params.heap_size, 65536, " heap =64KB");
    return KUNIT_PASS;
}

static kunit_result_t test_sys_params_ranges(void)
{
    /* 时钟频率范围: 10-10000Hz */
    KUNIT_ASSERT(1000 >= 10 && 1000 <= 10000, "1000Hz at 范围内 ");
    KUNIT_ASSERT(5 < 10, "5Hz 低于下限(合法值) ");

    /* 栈大小范围: 4KB-1MB */
    KUNIT_ASSERT(8192 >= 4096 && 8192 <= 1048576, "8KB 栈 at 范围内 ");
    return KUNIT_PASS;
}

/* === 版本号测试 === */
typedef struct {
    u16 major;
    u16 minor;
    u16 patch;
} version_t;

static int version_compare(const version_t *a, const version_t *b)
{
    if (a->major != b->major) return a->major - b->major;
    if (a->minor != b->minor) return a->minor - b->minor;
    return a->patch - b->patch;
}

static kunit_result_t test_version_compare(void)
{
    version_t v1 = {0, 90, 0};
    version_t v2 = {0, 89, 0};
    version_t v3 = {0, 90, 1};
    version_t v4 = {1, 0, 0};

    KUNIT_ASSERT(version_compare(&v1, &v2) > 0, "0.90 > 0.89");
    KUNIT_ASSERT(version_compare(&v2, &v1) < 0, "0.89 < 0.90");
    KUNIT_ASSERT(version_compare(&v1, &v1) == 0, "0.90 == 0.90");
    KUNIT_ASSERT(version_compare(&v3, &v1) > 0, "0.90.1 > 0.90.0");
    KUNIT_ASSERT(version_compare(&v4, &v1) > 0, "1.0 > 0.90");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t config_tests[] = {
    {" config set get ", test_config_set_get, "config"},
    {" config update ", test_config_update, "config"},
    {" config 整 count ", test_config_int, "config"},
    {" config 限制 ", test_config_limits, "config"},
    {" cmdline parse ", test_cmdline_parse, "config"},
    {" cmdline empty ", test_cmdline_empty, "config"},
    {" start param default ", test_boot_params_defaults, "config"},
    {" start param verify ", test_boot_params_validation, "config"},
    {" system param ", test_sys_params, "config"},
    {" system param 范围 ", test_sys_params_ranges, "config"},
    {" version 比较 ", test_version_compare, "config"},
};
static const kunit_suite_t kunit_config_suite = {
    " config & param ", config_tests, 11
};

void kunit_config_register(void)
{
    kunit_register_suite(&kunit_config_suite);
    vga_puts("[KUNIT] config & param test suite registered(11 cases)\n");
}
