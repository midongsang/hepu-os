# ============================================================================
# 赫普OS - 64位内核Makefile
# V0.10.6: 初始64位内核构建系统
# ============================================================================

# 工具链
CC = gcc
LD = ld
AS = gcc
OBJCOPY = objcopy

# 目录（使用绝对路径避免混淆）
PROJECT_ROOT = /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os
KERNEL_DIR = $(PROJECT_ROOT)/kernel
ARCH_DIR = $(KERNEL_DIR)/arch/x86_64
BUILD_DIR = $(PROJECT_ROOT)/build_x86_64

# 源文件（核心文件，避免依赖32位内核）
C_SOURCES = \
    $(KERNEL_DIR)/kernel_main_64.c \
    $(ARCH_DIR)/lib64.c \
    $(ARCH_DIR)/kernel_contracts.c \
    $(ARCH_DIR)/pmm64.c \
    $(ARCH_DIR)/vmm64.c \
    $(ARCH_DIR)/gdt64.c \
    $(ARCH_DIR)/irq64_simple.c \
    $(ARCH_DIR)/syscall64_simple.c \
    $(ARCH_DIR)/syscall64_full.c \
    $(ARCH_DIR)/ide64.c \
    $(ARCH_DIR)/keyboard64.c \
    $(ARCH_DIR)/rtc64.c \
    $(ARCH_DIR)/ext2_64.c \
    $(ARCH_DIR)/pci64.c \
    $(ARCH_DIR)/rtl8139_64.c \
    $(ARCH_DIR)/gfx64.c \
    $(ARCH_DIR)/net64.c \
    $(ARCH_DIR)/net64_eth.c \
    $(ARCH_DIR)/net64_arp.c \
    $(ARCH_DIR)/net64_ip.c \
    $(ARCH_DIR)/net64_icmp.c \
    $(ARCH_DIR)/net64_udp.c \
    $(ARCH_DIR)/net64_tcp.c \
    $(ARCH_DIR)/net64_dhcp.c \
    $(ARCH_DIR)/net64_dns.c \
    $(ARCH_DIR)/net64_http.c \
    $(ARCH_DIR)/process64_full.c \
    $(ARCH_DIR)/shell64.c \
    $(ARCH_DIR)/desktop64.c \
    $(ARCH_DIR)/user_process.c \
    $(ARCH_DIR)/user_lib.c \
    $(ARCH_DIR)/user_app_test.c \
    $(ARCH_DIR)/elf64.c \
    $(ARCH_DIR)/drv_register64.c \
    $(ARCH_DIR)/storage_hal64.c \
    $(ARCH_DIR)/network_hal64.c \
    $(ARCH_DIR)/socket64.c \
    $(ARCH_DIR)/ext2_fsck64.c \
    $(KERNEL_DIR)/shared/drivers/driver_framework.c \
    $(KERNEL_DIR)/shared/gfx/gfx_algo.c \
    $(KERNEL_DIR)/shared/fs/ext2_algo.c \
    $(KERNEL_DIR)/shared/lib/lib_shared.c \
    $(KERNEL_DIR)/shared/debug/debug_shared.c \
    $(ARCH_DIR)/ps2mouse64.c \
    $(ARCH_DIR)/ahci64.c \
    $(ARCH_DIR)/font64.c \
    $(ARCH_DIR)/ttf64.c \
    $(ARCH_DIR)/font_data.c \
    $(ARCH_DIR)/window64.c \
    $(ARCH_DIR)/taskmgr64.c \
    $(ARCH_DIR)/filemgr64.c \
    $(ARCH_DIR)/syssettings64.c \
    $(ARCH_DIR)/notes64.c \
    $(ARCH_DIR)/term64.c \
    $(ARCH_DIR)/appstore64.c \
    $(ARCH_DIR)/kheap64.c \
    $(ARCH_DIR)/user_access64.c \
    $(ARCH_DIR)/pit64.c \
    $(ARCH_DIR)/apic64.c \
    $(ARCH_DIR)/x86_64.c \
    $(ARCH_DIR)/spinlock.c \
    $(ARCH_DIR)/smp.c \
    $(ARCH_DIR)/acpi.c \
    $(ARCH_DIR)/uart64.c \
    $(ARCH_DIR)/ac9764.c \
    $(ARCH_DIR)/hepushell.c \
    $(ARCH_DIR)/hsqml.c \
    $(ARCH_DIR)/hsipc.c \
    $(ARCH_DIR)/hsservices.c \
    $(ARCH_DIR)/hsextras.c

ASM_SOURCES = \
    $(ARCH_DIR)/multiboot_entry.S \
    $(ARCH_DIR)/entry_64.S \
    $(ARCH_DIR)/user_switch.S

# 对象文件（放在build目录下）
C_OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(C_SOURCES:.c=.o)))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(ASM_SOURCES:.S=.o)))

# VPATH用于查找源文件
vpath %.c $(KERNEL_DIR) $(ARCH_DIR) $(KERNEL_DIR)/shared/gfx $(KERNEL_DIR)/shared/drivers $(KERNEL_DIR)/shared/fs $(KERNEL_DIR)/shared/lib $(KERNEL_DIR)/shared/debug
vpath %.S $(ARCH_DIR)

# 编译选项
CFLAGS = \
    -m64 \
    -mcmodel=small \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -ffreestanding \
    -fstack-protector-all \
    -mstack-protector-guard=global \
    -fno-pic \
    -fno-pie \
    -fcf-protection=none \
    -fno-strict-aliasing \
    -fwrapv \
    -O2 \
    -Wall \
    -Wextra \
    -I$(KERNEL_DIR)/include \
    -I$(ARCH_DIR) \
    -I$(ARCH_DIR)/include \
    -I$(KERNEL_DIR)/shared \
    -I$(KERNEL_DIR)/shared/fs \
    -I$(KERNEL_DIR)/shared/lib \
    -I$(KERNEL_DIR)/shared/debug \
    -D__x86_64__ \
    -DHEPU_KERNEL_64

ASFLAGS = \
    -m64 \
    -mno-red-zone \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pic \
    -fno-pie \
    -I$(KERNEL_DIR)/include \
    -I$(ARCH_DIR)

LDFLAGS = \
    -nostdlib \
    -T $(ARCH_DIR)/x86_64.ld \
    -z max-page-size=0x1000

# 目标
TARGET = $(BUILD_DIR)/hepu-kernel64.bin
ELF_TARGET = $(BUILD_DIR)/hepu-kernel64.elf

# 默认目标
all: $(TARGET)

# 创建构建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译C文件
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  [CC] $<"

# 编译shared目录的C文件
$(BUILD_DIR)/driver_framework.o: $(KERNEL_DIR)/shared/drivers/driver_framework.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  [CC] $< (shared)"

# 编译汇编文件
$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@
	@echo "  [AS] $<"

# 链接为ELF
$(ELF_TARGET): $(C_OBJECTS) $(ASM_OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(C_OBJECTS) $(ASM_OBJECTS)
	@echo "  [LD] $@"

# 转换为二进制
$(TARGET): $(ELF_TARGET)
	$(OBJCOPY) -O binary $< $@
	@echo "  [OBJCOPY] $@"
	@ls -lh $@

# 清理
clean:
	rm -rf $(BUILD_DIR)

# 显示信息
info:
	@echo "=== HepuOS 64-bit Kernel Build Info ==="
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "C_SOURCES: $(C_SOURCES)"
	@echo "C_OBJECTS: $(C_OBJECTS)"
	@echo "ASM_OBJECTS: $(ASM_OBJECTS)"
	@echo "Target: $(TARGET)"
	@echo "======================================="

.PHONY: all clean info
