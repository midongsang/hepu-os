/* ============================================================================
 * 赫普OS - ACPI EC（Embedded Controller）最小事务层（M2-full）
 *
 * 只做电池 Field 读取需要的 RD_EC（0x80）。写 EC（WR_EC）不做：
 * 电池 Field 在本系统里只读，任何写路径都应保持 fail-closed。
 *
 * 端口约定（ACPI 5.0 §12.2）：0x62 = EC data，0x66 = EC status/command。
 * 状态位：bit0 OBF（输出缓冲满）、bit1 IBF（输入缓冲满）。
 *
 * 超时：每次等待最多 EC_SPIN 次端口读，失败返回负错误码，不无限等。
 * 是否启用由调用方决定：只有命名空间里存在 PNP0C09 才 set_present(1)，
 * 避免在无 EC 的机器上盲写 0x66。
 * ========================================================================== */

#ifndef EC64_H
#define EC64_H

#include <stdint.h>

void ec64_set_present(int present);
int ec64_present(void);

/* _GLK=1：EC 与 SMI 共享，必须先拿 ACPI Global Lock。当前未实现 FACS
 * 全局锁，调用方应在 glk=1 时锁死 EC 读，不能冒险访问。 */
void ec64_set_glk(int glk);
int ec64_glk(void);

/* 读 EC 地址空间的一个字节。成功 0；未启用 -2；超时/状态异常 -3；
 * _GLK=1 且未实现全局锁时 -4（不碰端口）。 */
int ec64_read(uint8_t offset, uint8_t *out);

#endif
