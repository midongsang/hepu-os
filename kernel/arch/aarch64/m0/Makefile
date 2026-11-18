# Hepu OS ARM64 M0 —— EFI 应用构建（用仓库内 gnu-efi 的 aarch64 产物）
CC      = aarch64-linux-gnu-gcc
LD      = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy

EFI_ROOT = ../../../../tools/gnu-efi
EFI_INC  = $(EFI_ROOT)/inc
EFI_ARCH = $(EFI_ROOT)/aarch64
EFI_LDS  = $(EFI_ROOT)/gnuefi/elf_aarch64_efi.lds

CFLAGS = -fno-stack-protector -fpic -fshort-wchar -ffreestanding \
         -DEFI_FUNCTION_WRAPPER -I$(EFI_INC) -I$(EFI_INC)/aarch64 \
         -I$(EFI_INC)/protocol -O2 -Wall
LDFLAGS = -nostdlib -znocombreloc --defsym=EFI_SUBSYSTEM=10 -T $(EFI_LDS) -shared -Bsymbolic \
          -L$(EFI_ARCH)/lib -L$(EFI_ARCH)/gnuefi $(EFI_ARCH)/gnuefi/crt0-efi-aarch64.o

TARGET = BOOTAA64.EFI

all: $(TARGET)

hepu_m0_a64.o: hepu_m0_a64.c
	$(CC) $(CFLAGS) -c $< -o $@

m0.so: hepu_m0_a64.o
	$(LD) $(LDFLAGS) -o $@ $< -lgnuefi -lefi

$(TARGET): m0.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	           -j .rel -j .rela -j .reloc $< $@
	python3 ../../../../tools/pe_flatten_aarch64.py $(TARGET) /tmp/_x.efi && mv /tmp/_x.efi $(TARGET)
	@echo "  [M0] $(TARGET) built"
	@ls -l $(TARGET)

clean:
	rm -f hepu_m0_a64.o m0.so $(TARGET)
