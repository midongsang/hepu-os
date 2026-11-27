/* ============================================================================
 * 赫普OS - 极简 ACPI AML 解释器（M2）
 *
 * 目标：不做通用 ACPICA，只把“读电池数值”这条链子打通：
 *   建命名空间（DSDT + SSDT）→ 按路径求值 _STA/_BIF/_BIX/_BST。
 *
 * 支持子集（有明确边界，越界一律失败，不猜）：
 *   - 命名空间：Scope/Device/Processor/PowerRes/ThermalZone/Name/Method/Field/
 *     OperationRegion/Mutex（单线程占位）/Alias/External；建库期遇到未识别外层对象，
 *     若带合法 PkgLength 则按长度跳过并记降级：If/Else 递归登记 then/else 两分支声明、
 *     While 跳过 body（建库期不求值条件）；无法定位长度才硬中止，偏移/原因经 aml_ns_diag 透出。
 *   - 方法前缀：Acquire/Release（单线程占位）、Stall/Sleep（有界占位，不做真实墙钟等待）、
 *     _OSI（统一返回 0）
 *   - 值：整数、字符串、Buffer、Package/VarPackage
 *   - 缓冲拆包：CreateDWord/Word/Byte/Bit/QWordField、CreateField 绑定 Buffer 位段引用，
 *     读按位抽取、Store 回写 Buffer 对应位；越界/非 Buffer/位宽>64/负索引明确错误。
 *   - 语句：Store/CopyObject/If/Else/While/Return/Noop，Local0-7、Arg0-6
 *   - 运算：Add/Subtract/Multiply/Divide/Mod/And/Or/Xor/Not/ShiftLeft/ShiftRight/
 *     LAnd/LOr/LNot/LEqual/LGreater/LLess/Increment/Decrement
 *   - 引用：Index/DerefOf/RefOf（缓冲与包的只读元素）
 *   - 寄存器：SystemIO、SystemMemory、EmbeddedControl（经 ec64，只读、带超时）普通 Field 读取；
 *     IndexField / BankField：支持 SystemIO、SystemMemory 上的索引/数据（或 region/bank）通路，
 *     按 bit 偏移/宽度抽取，跨 granule 逐段拼装；总位宽 >64 位拒绝。
 *
 * 不支持：EC 写事务（因此 IndexField/BankField 的 index/data/bank 寄存器落在 EmbeddedControl 上
 *         一律返回 AML_E_UNSUP，不写旧值/0 假装成功）、Wait/Signal 事件同步、Mutex 跨 CPU/SMI
 *         真同步、方法内动态命名、Concatenate/Mid/ObjectType 等部分 P1 表达式、Windows 兼容层。
 * 跳过未知对象只扩大建库覆盖面，不会让电池“看起来成功”：缺字段仍 NOTFOUND → DECLARED/UNKNOWN。
 * 这些路径失败时返回明确错误码，调用方保持“未读到”状态，绝不填假数。
 * ========================================================================== */

#ifndef AML64_H
#define AML64_H

#include <stdint.h>

#define AML_V_INT 0
#define AML_V_STR 1
#define AML_V_BUF 2
#define AML_V_PKG 3
#define AML_V_REF 4   /* 只读元素引用：base + index */

typedef struct aml_value aml_value_t;

struct aml_value {
    uint8_t type;
    int64_t ival;
    uint8_t *buf;
    uint32_t blen;
    aml_value_t **items;
    uint32_t count;
    aml_value_t *base;
    uint32_t index;
};

/* 错误码 */
#define AML_E_OK        0
#define AML_E_NOTFOUND -1   /* 命名空间里没有这个对象/方法 */
#define AML_E_BADCODE  -2   /* AML 字节流不合法或超出支持子集 */
#define AML_E_UNSUP    -3   /* 明确不支持（如 EC 字段、写字段） */
#define AML_E_LIMIT    -4   /* 步数/深度/内存预算用尽 */
#define AML_E_ARG      -5   /* 参数/类型不匹配 */

/* 建命名空间。返回 0 成功；-1 没有任何表；-2 分配失败。 */
#ifdef AML_HOST_TEST
/* 主机测试专用：清空命名空间，允许重新建表。 */
void aml_ns_reset(void);
#endif

int aml_ns_build(const uint8_t *const *tables, const uint32_t *lens, int count);
int aml_ns_ready(void);
int aml_ns_nodes(void);

/* 建库诊断（只读，不改 aml_ns_build 签名与调用方行为）：
 *   degraded         建库期降级计数（跳过/容错处理的对象）；
 *   unknown_skipped  带合法 PkgLength 按长度跳过的未识别外层对象数；
 *   stop_off         首个无法定位长度而硬中止的偏移（相对表体，-1=未中止）。
 * 三个指针均可为空。 */
int aml_ns_diag(int *degraded, int *unknown_skipped, int *stop_off);

/* 找 _HID 等于 eisa_id（压缩 EISA ID）或字符串 hid_str 的设备。
 * 成功返回 0 并写入设备路径（不带前导反斜杠）。
 * aml_find_device_next 从 *cursor 开始继续枚举（cursor 由调用方初始化为 0），
 * 用于多电池/多适配器等同一 HID 的多个实例；未找到时把 cursor 推到末尾。 */
int aml_find_device(uint32_t eisa_id, const char *hid_str, char *out, int cap);
int aml_find_device_next(uint32_t eisa_id, const char *hid_str, char *out, int cap,
                         int *cursor);

/* 求值一个完整路径（可带前导 '\\'）。*out 指向值（生命周期到 sample_end）。 */
int aml_eval_path(const char *path, aml_value_t **out);

/* 以显式整数参数调用方法（用于 _REG 等带参控制方法）。
 * args 可为空仅当 nargs=0；nargs 超过 7 返回 AML_E_ARG。 */
int aml_eval_path_ints(const char *path, const int64_t *args, int nargs,
                       aml_value_t **out);

/* 值访问 */
uint8_t aml_value_type(const aml_value_t *v);
int aml_value_int(const aml_value_t *v, int64_t *out);
int aml_value_count(const aml_value_t *v);
const aml_value_t *aml_value_at(const aml_value_t *v, int i);
int aml_value_str(const aml_value_t *v, const char **s, int *len);

/* 每次采样开始时调用（重置求值内存与写覆盖层），结束时调用。 */
int aml_sample_begin(void);
void aml_sample_end(void);

#endif
