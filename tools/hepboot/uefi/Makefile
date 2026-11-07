# HepuBoot UEFI 引导器 Makefile
# 基于gnu-efi标准库（成熟、可靠）

# 编译器和工具
CC = gcc
LD = ld
OBJCOPY = objcopy

# gnu-efi路径（本仓库内置版本，相对当前 Makefile 定位）
EFI_ROOT = $(abspath $(CURDIR)/../../gnu-efi)
EFI_INCLUDE = $(EFI_ROOT)/inc
EFI_LIB = $(EFI_ROOT)/x86_64/lib
EFI_GNUEFI_LIB = $(EFI_ROOT)/x86_64/gnuefi
EFI_CRT0 = $(EFI_ROOT)/x86_64/gnuefi/crt0-efi-x86_64.o
EFI_LDS = $(EFI_ROOT)/gnuefi/elf_x86_64_efi.lds

# 编译选项
CFLAGS = -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
         -DEFI_FUNCTION_WRAPPER -I$(EFI_INCLUDE) -I$(EFI_INCLUDE)/x86_64 \
         -I$(EFI_INCLUDE)/protocol -Wall -Wextra -O2

# 链接选项
LDFLAGS = -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic \
          -L$(EFI_LIB) -L$(EFI_GNUEFI_LIB) $(EFI_CRT0)

# 库
LIBS = -lgnuefi -lefi

# 目标文件
TARGET = hepboot_uefi.efi
OBJECT = hepboot_uefi.o hepboot_gop.o jump_kernel.o jump_kernel_64.o
SOURCE = hepboot_uefi.c hepboot_gop.c

# 默认目标
all: $(TARGET)

# 编译C
hepboot_uefi.o: hepboot_uefi.c
	$(CC) $(CFLAGS) -c $< -o $@

hepboot_gop.o: hepboot_gop.c hepboot_gop.h
	$(CC) $(CFLAGS) -c $< -o $@

# 编译汇编
jump_kernel.o: jump_kernel.S
	$(CC) $(CFLAGS) -c $< -o $@

jump_kernel_64.o: jump_kernel_64.S
	$(CC) $(CFLAGS) -c $< -o $@

# 链接为共享库（crt0处理入口）
hepboot_uefi.so: $(OBJECT)
	$(LD) $(LDFLAGS) -o $@ $(OBJECT) $(LIBS)

# 转换为EFI应用（PE/COFF格式）
# 注意: binutils >= 2.45 起 --target 会同时指定输入格式, 导致 ELF 输入被拒
# ("file format not recognized"), 必须用 --output-target (上游 bug 2139340)
$(TARGET): hepboot_uefi.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic \
	           -j .dynsym -j .rel -j .rela -j .reloc \
	           --output-target=efi-app-x86_64 $< $@
	@echo "  [UEFI] Build complete: $(TARGET)"
	@ls -la $(TARGET)

# 清理
clean:
	rm -f $(OBJECT) hepboot_uefi.so $(TARGET) entry.o hepboot_uefi.elf test_*.o test_*.so test_*.efi hepboot_uefi_signed.efi

# Secure Boot 签名
sign: $(TARGET)
	@echo "  [UEFI] Signing bootloader for Secure Boot..."
	@./sign_bootloader.sh

# 生成Secure Boot密钥
genkeys:
	@echo "  [UEFI] Generating Secure Boot keys..."
	@./secureboot/generate_keys.sh

# 编译+签名
signed: all sign

# 测试（需要OVMF和QEMU）
test: $(TARGET)
	@echo "  [UEFI] Creating test image..."
	@rm -rf /tmp/uefi_test
	@mkdir -p /tmp/uefi_test/EFI/BOOT
	@cp $(TARGET) /tmp/uefi_test/EFI/BOOT/BOOTX64.EFI
	@echo "  [UEFI] Creating GPT+ESP image..."
	@rm -f /tmp/uefi_gpt.img
	@dd if=/dev/zero of=/tmp/uefi_gpt.img bs=1M count=128 2>/dev/null
	@parted -s /tmp/uefi_gpt.img mklabel gpt 2>/dev/null
	@parted -s /tmp/uefi_gpt.img mkpart ESP fat32 1MiB 100MiB 2>/dev/null
	@parted -s /tmp/uefi_gpt.img set 1 esp on 2>/dev/null
	@mkfs.vfat -F 32 --offset=2048 /tmp/uefi_gpt.img >/dev/null 2>&1
	@mcopy -i /tmp/uefi_gpt.img@@1M -s /tmp/uefi_test/EFI ::/ 2>/dev/null || true
	@echo "  [UEFI] Starting QEMU UEFI test (15s)..."
	@qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd \
		-drive file=/tmp/uefi_gpt.img,format=raw \
		-vga std -net none -nographic 2>/dev/null || true

.PHONY: all clean test
