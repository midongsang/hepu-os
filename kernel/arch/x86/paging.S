/* ============================================================================
 * 赫普OS - 分页汇编辅助函数
 * ========================================================================== */

.section .text

/* 加载页目录到 CR3 */
.global paging_load_dir
paging_load_dir:
    mov 4(%esp), %eax
    mov %eax, %cr3
    ret

/* 启用分页 (设置 CR0 的 PG 位) */
.global paging_enable_paging
paging_enable_paging:
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    ret

/* 刷新 TLB 中指定页面 */
.global paging_invlpg
paging_invlpg:
    mov 4(%esp), %eax
    invlpg (%eax)
    ret

/* 读取 CR2 (页错误虚拟地址) */
.global paging_read_cr2
paging_read_cr2:
    mov %cr2, %eax
    ret
