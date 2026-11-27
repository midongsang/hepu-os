/* ============================================================================
 * 赫普OS - 极简 ACPI AML 解释器（M2）
 *
 * 目标：不做通用 ACPICA，只把“读电池数值”这条链子打通：
 *   建命名空间（DSDT + SSDT）→ 按路径求值 _STA/_BIF/_BIX/_BST。
 *
 * 支持子集（有明确边界，越界一律失败，不猜）：
 *   - 命名空间：Scope/Device/Processor/PowerRes/ThermalZone/Name/Method/Field/
 *     OperationRegion/Mutex（占位）/Alias（忽略）/External（忽略）
 *   - 值：整数、字符串、Buffer、Package/VarPackage
 *   - 语句：Store/CopyObject/If/Else/While/Return/Noop，Local0-7、Arg0-6
 *   - 运算：Add/Subtract/Multiply/Divide/Mod/And/Or/Xor/Not/ShiftLeft/ShiftRight/
 *     LAnd/LOr/LNot/LEqual/LGreater/LLess/Increment/Decrement
 *   - 引用：Index/DerefOf/RefOf（缓冲与包的只读元素）
 *   - 寄存器：SystemIO 与 SystemMemory 的 Field 读取（EmbeddedControl 不支持）
 *
 * 不支持：EC 事务、IndexField/BankField、Mutex 真同步、方法内动态命名、
 *         条件引用 CondRefOf 之外的扩展对象、_OSI 等控制方法。
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
int aml_ns_build(const uint8_t *const *tables, const uint32_t *lens, int count);
int aml_ns_ready(void);
int aml_ns_nodes(void);

/* 找 _HID 等于 eisa_id（压缩 EISA ID）或字符串 hid_str 的设备。
 * 成功返回 0 并写入设备路径（不带前导反斜杠）。 */
int aml_find_device(uint32_t eisa_id, const char *hid_str, char *out, int cap);

/* 求值一个完整路径（可带前导 '\\'）。*out 指向值（生命周期到 sample_end）。 */
int aml_eval_path(const char *path, aml_value_t **out);

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
