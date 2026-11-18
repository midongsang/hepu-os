OUTPUT_ARCH(aarch64)
ENTRY(_start)
SECTIONS
{
  . = 0x40000000;
  .text : { *(.text.head) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  . = ALIGN(16);
  . = . + 0x10000;   /* 64KB 栈 */
  __stack_top = .;
}
