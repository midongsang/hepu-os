# ============================================================================
# 赫普OS 64位 - 用户态程序Makefile
# 编译独立ELF64用户程序
# ============================================================================

USER_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(USER_DIR)build

# 编译器
CC := gcc
LD := ld
OBJCOPY := objcopy

# 编译选项（用户态ELF64）
CFLAGS := -m64 -ffreestanding -fcf-protection=none -fno-pic -fno-pie -fno-stack-protector \
          -fno-builtin -nostdlib -nodefaultlibs -O2 -Wall -Wextra
LDFLAGS := -m elf_x86_64 -nostdlib -static

# 链接脚本（用户态地址空间：0x400000）
LDSCRIPT := $(USER_DIR)user.ld

# 源文件
SRCS := hello.c shell.c

# 目标
TARGETS := $(BUILD_DIR)/hello.elf $(BUILD_DIR)/shell.elf

.PHONY: all clean

all: $(TARGETS)

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

clean:
	rm -rf $(BUILD_DIR)
