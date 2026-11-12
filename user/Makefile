# ============================================================================
# 赫普OS 64位 - 用户态程序Makefile
# 编译独立ELF64用户程序
# ============================================================================

# 默认目标显式指定为 all。
# 不用靠「文件里第一个目标」：all 定义在文件末尾（变量展开时机所限），
# 若不显式指定，默认目标会变成第一个出现的具体目标（linux_pie_demo.elf），
# `make` 就只构建那一个，看着成功、产物却缺一大半。
.DEFAULT_GOAL := all

USER_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(USER_DIR)build

# 编译器
CC := gcc
LD := ld
OBJCOPY := objcopy

# 编译选项（用户态ELF64）
CFLAGS := -m64 -ffreestanding -fcf-protection=none -fno-pic -fno-pie -fno-stack-protector \
          -fno-builtin -nostdlib -nodefaultlibs -O2 -Wall -Wextra \
          -I$(USER_DIR)../kernel/include
LDFLAGS := -m elf_x86_64 -nostdlib -static

# 链接脚本（用户态地址空间：0x400000）
LDSCRIPT := $(USER_DIR)user.ld

# 源文件
SRCS := hello.c shell.c forktest.c

# 目标
TARGETS := $(BUILD_DIR)/hello.elf $(BUILD_DIR)/shell.elf $(BUILD_DIR)/forktest.elf \
           $(BUILD_DIR)/sec_test.elf $(BUILD_DIR)/comptest.elf \
           $(BUILD_DIR)/ps.elf \
           $(BUILD_DIR)/clone_test.elf

# 全量构建目标：原生用户程序 + Linux 兼容层 PIE 测试程序 + 历史测试程序（SEC-12）
# 注意：真正的 all 目标在**文件末尾**定义 —— GNU Make 在**读取规则时**就展开
#       规则里的变量引用，若这里就写 $(LEGACY_TARGETS)，而它的定义在文件后面，
#       展开结果会是空 ⇒ legacy 那批程序根本不会被构建（`make` 看着成功，
#       产物却缺一大半）。故此处仅留注释，实体见文件末尾。
# all: $(TARGETS) $(PIE_DEMO) $(PIE_PROBE) $(LEGACY_TARGETS)

# ---------------------------------------------------------------------------
# Linux 兼容层测试程序（用宿主 gcc/glibc 编译成 PIE，链接基址=0）
# 验证「加载偏置 + Linux 号表翻译」这条兼容路径。
# 宿主没有 gcc -static-pie 时静默跳过（不阻塞原生构建）。
# ---------------------------------------------------------------------------
PIE_DEMO_DIR := $(USER_DIR)apps/linux_pie_demo
PIE_DEMO := $(PIE_DEMO_DIR)/linux_pie_demo.elf

$(PIE_DEMO): $(PIE_DEMO_DIR)/linux_pie_demo.c
	@if gcc -static-pie -O2 -o $@ $< 2>/dev/null; then \
		echo "Built Linux PIE demo: $@"; \
	else \
		echo "SKIP: host gcc lacks -static-pie (Linux PIE demo not built)"; \
	fi

# ---------------------------------------------------------------------------
# freestanding static-PIE 探针（无 libc，只用裸 syscall）
# 用途：把「内核侧是否正确」与「glibc 自身的 TLS/自重定位问题」隔离开 ——
# 探针只要能跑通，说明加载偏置 + R_X86_64_RELATIVE + 初始栈 auxv 都是对的。
# ---------------------------------------------------------------------------
PIE_PROBE_DIR := $(USER_DIR)apps/pie_probe
PIE_PROBE := $(PIE_PROBE_DIR)/pie_probe.elf

$(PIE_PROBE): $(PIE_PROBE_DIR)/pie_probe.c
	@if gcc -static-pie -nostdlib -fno-stack-protector \
	     -fno-asynchronous-unwind-tables -O2 -Wl,-e,_start \
	     -o $@ $< 2>/dev/null; then \
		echo "Built PIE probe: $@"; \
	else \
		echo "SKIP: host gcc lacks -static-pie (PIE probe not built)"; \
	fi

.PHONY: all clean pie

pie: $(PIE_DEMO) $(PIE_PROBE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(USER_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hello.elf: $(BUILD_DIR)/hello.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<
	@echo "Built user ELF: $@"
	@ls -lh $@

$(BUILD_DIR)/shell.elf: $(BUILD_DIR)/shell.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<
	@echo "Built user ELF: $@"
	@ls -lh $@

$(BUILD_DIR)/forktest.elf: $(BUILD_DIR)/forktest.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<
	@echo "Built user ELF: $@"
	@ls -lh $@

# sec_test：用户地址访问安全回归（SEC-01）
$(BUILD_DIR)/sec_test.elf: $(BUILD_DIR)/sec_test.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<
	@echo "Built user ELF: $@"
	@ls -lh $@

# comptest：Linux 号表兼容测试（源码在 user/apps/，带 NT_GNU_ABI_TAG 标记）
$(BUILD_DIR)/comptest.o: $(USER_DIR)apps/comptest.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/comptest.elf: $(BUILD_DIR)/comptest.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<
	@echo "Built user ELF: $@"
	@ls -lh $@

# ps: 进程列表
$(BUILD_DIR)/ps.o: $(USER_DIR)apps/ps.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ps.elf: $(BUILD_DIR)/ps.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<

$(BUILD_DIR)/clone_test.o: $(USER_DIR)apps/clone_test.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/clone_test.elf: $(BUILD_DIR)/clone_test.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<
	@echo "Built user ELF: $@"
	@ls -lh $@

# pipe test
$(BUILD_DIR)/pipe_test.o: $(USER_DIR)apps/pipe_test.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/pipe_test.elf: $(BUILD_DIR)/pipe_test.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $<
	@echo "Built user ELF: $@"
	@ls -lh $@

# ===========================================================================
# SEC-12: 历史测试程序补源码 —— 让镜像**完全**可从源码重建
# ----------------------------------------------------------------------------
# 背景：v0.33 之前，user/build/ 下有一批没有源码的历史测试二进制
#       （hello_static / hello_linux / hello2 / hello3 / hello4 / hello_c2 /
#         dynhello / dynhello2 / glibc_test / hello_libc / libtest / libtest2）。
#       它们散在镜像里，构建不可重现 —— 改一个字节都没法重做。
#       v0.33 已通过反汇编 + 寄存器转储把每个程序的语义逐条还原，
#       源码见 user/apps/legacy/。
#
# 两类构建方式：
#   ① freestanding Linux ABI 程序 —— 裸 syscall，不链 libc，
#      链接地址必须落在 Hepu 用户窗口 [0x40000000,0x80000000) 内，
#      否则内核按设计拒绝（ET_EXEC 的绝对地址烧在代码里，搬不动）。
#   ② glibc 静态程序 / 共享库 —— 用宿主 gcc 编译。
# ===========================================================================
LEGACY_DIR   := $(USER_DIR)apps/legacy
LEGACY_BUILD := $(BUILD_DIR)/legacy

# --- ① freestanding Linux ABI 程序（裸 syscall） -------------------------
LEGACY_FREESTANDING := \
	$(LEGACY_BUILD)/hello_static.elf \
	$(LEGACY_BUILD)/hello_linux.elf \
	$(LEGACY_BUILD)/hello2.elf \
	$(LEGACY_BUILD)/hello3.elf \
	$(LEGACY_BUILD)/hello4.elf \
	$(LEGACY_BUILD)/hello_c2.elf \
	$(LEGACY_BUILD)/crash_jump.elf

# 链接地址表：全部在用户窗口内。
# 关键：.note.ABI-tag 段必须**显式**放到窗口内 ——
#   ld 默认把 note 段放在 text 之前的低地址（实测落在 0x400000），
#   而内核 elf64_link_base() 取的是**最低 PT_LOAD 的 p_vaddr**，
#   于是 link_base < UWIN_START(0x40000000) ⇒ ET_EXEC 被判「链接在窗口外」而拒绝。
#   所以这里用 --section-start 把 note 钉在 0x40001000，text 放在 0x40002000，
#   两者都在用户窗口内。
LEGACY_NOTE_ADDR := 0x40001000
LEGACY_ADDR_hello_static := 0x40002000
LEGACY_ADDR_hello_linux  := 0x40002000
LEGACY_ADDR_hello2       := 0x40002000
LEGACY_ADDR_hello3       := 0x40002000
LEGACY_ADDR_hello4       := 0x40002000
LEGACY_ADDR_hello_c2     := 0x40002000
LEGACY_ADDR_crash_jump   := 0x40002000

# $(1)=目标名 $(2)=链接地址
define LEGACY_FS_RULE
$(LEGACY_BUILD)/$(1).elf: $(LEGACY_DIR)/$(1).c $(LEGACY_DIR)/common_linux_abi.h | $(LEGACY_BUILD)
	$(CC) $(CFLAGS) -c $$< -o $(LEGACY_BUILD)/$(1).o
	$(LD) $(LDFLAGS) -Ttext=$(2) --section-start=.note.ABI-tag=$(LEGACY_NOTE_ADDR) \
	      -e _start -o $$@ $(LEGACY_BUILD)/$(1).o
	@echo "Built legacy ELF: $$@"
endef

$(eval $(call LEGACY_FS_RULE,hello_static,$(LEGACY_ADDR_hello_static)))
$(eval $(call LEGACY_FS_RULE,hello_linux,$(LEGACY_ADDR_hello_linux)))
$(eval $(call LEGACY_FS_RULE,hello2,$(LEGACY_ADDR_hello2)))
$(eval $(call LEGACY_FS_RULE,hello3,$(LEGACY_ADDR_hello3)))
$(eval $(call LEGACY_FS_RULE,hello4,$(LEGACY_ADDR_hello4)))
$(eval $(call LEGACY_FS_RULE,hello_c2,$(LEGACY_ADDR_hello_c2)))
$(eval $(call LEGACY_FS_RULE,crash_jump,$(LEGACY_ADDR_crash_jump)))

# --- ② glibc 静态程序 ----------------------------------------------------
# 历史二进制是 ET_EXEC @ 0x400000 —— 落在用户窗口 [0x40000000,0x80000000)
# **之外**，内核按设计直接拒绝（绝对地址烧在代码里，搬不动）。
# 改用 -static-pie（ET_DYN）后，内核会把它加载到兼容程序区
# [0x50000000,0x60000000)，与 linux_pie_demo 走同一条已验证的路径。
LEGACY_GLIBC := \
	$(LEGACY_BUILD)/glibc_test.elf \
	$(LEGACY_BUILD)/hello_libc.elf

$(LEGACY_BUILD)/glibc_test.elf: $(LEGACY_DIR)/glibc_test.c | $(LEGACY_BUILD)
	@if $(CC) -static-pie -O2 -o $@ $< 2>/dev/null; then \
		echo "Built legacy ELF: $@"; \
	else \
		echo "SKIP: host gcc lacks -static-pie ($@)"; \
	fi

$(LEGACY_BUILD)/hello_libc.elf: $(LEGACY_DIR)/hello_libc.c | $(LEGACY_BUILD)
	@if $(CC) -static-pie -O2 -o $@ $< 2>/dev/null; then \
		echo "Built legacy ELF: $@"; \
	else \
		echo "SKIP: host gcc lacks -static-pie ($@)"; \
	fi

# --- ③ 共享库 + 动态链接程序 --------------------------------------------
# 共享库链在 0x60000000（用户窗口的「库区」），与历史二进制一致。
# 动态程序依赖 PT_INTERP —— HepuOS 当前**不支持**动态链接器加载，
# 这两个程序在内核上会被 exec 拒绝；保留它们是为了让历史资产可重建。
LEGACY_SHARED := \
	$(LEGACY_BUILD)/libtest.so \
	$(LEGACY_BUILD)/libtest2.so

$(LEGACY_BUILD)/libtest.so: $(LEGACY_DIR)/libtest.c | $(LEGACY_BUILD)
	$(CC) -shared -fPIC -O2 -Wl,-soname,libtest.so \
	      -Wl,-Ttext-segment=0x60000000 -o $@ $<
	@echo "Built legacy shared lib: $@"

$(LEGACY_BUILD)/libtest2.so: $(LEGACY_DIR)/libtest2.c | $(LEGACY_BUILD)
	$(CC) -shared -fPIC -O2 -Wl,-soname,libtest2.so \
	      -Wl,-Ttext-segment=0x60000000 -o $@ $<
	@echo "Built legacy shared lib: $@"

LEGACY_DYNAMIC := \
	$(LEGACY_BUILD)/dynhello.elf \
	$(LEGACY_BUILD)/dynhello2.elf

$(LEGACY_BUILD)/dynhello.elf: $(LEGACY_DIR)/dynhello.c $(LEGACY_BUILD)/libtest.so | $(LEGACY_BUILD)
	$(CC) -O2 -L$(LEGACY_BUILD) -Wl,-rpath,$(LEGACY_BUILD) -o $@ $< -l:libtest.so
	@echo "Built legacy ELF: $@"

$(LEGACY_BUILD)/dynhello2.elf: $(LEGACY_DIR)/dynhello2.c $(LEGACY_BUILD)/libtest2.so | $(LEGACY_BUILD)
	$(CC) -O2 -nostartfiles -L$(LEGACY_BUILD) -Wl,-rpath,$(LEGACY_BUILD) -o $@ $< -l:libtest2.so
	@echo "Built legacy ELF: $@"

LEGACY_TARGETS := $(LEGACY_FREESTANDING) $(LEGACY_GLIBC) $(LEGACY_SHARED) $(LEGACY_DYNAMIC)

$(LEGACY_BUILD):
	mkdir -p $(LEGACY_BUILD)

legacy: $(LEGACY_TARGETS)

# 全量构建目标（放在文件末尾：见上方关于变量展开时机的说明）
all: $(TARGETS) $(PIE_DEMO) $(PIE_PROBE) $(LEGACY_TARGETS)

clean:
	rm -rf $(BUILD_DIR)
