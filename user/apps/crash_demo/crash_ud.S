/* V3.88-E18: 用户态异常隔离测试——ud2 非法指令触发 #UD */
.section .text
.global _start
_start:
    ud2
    hlt
