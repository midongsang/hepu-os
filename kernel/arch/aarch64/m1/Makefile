CC      = aarch64-linux-gnu-gcc
LD      = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy
STRIP   = aarch64-linux-gnu-strip

EFI_ROOT = ../../../../tools/gnu-efi
CFLAGS = -fno-stack-protector -fpic -fshort-wchar -ffreestanding \
         -DEFI_FUNCTION_WRAPPER -I$(EFI_ROOT)/inc -I$(EFI_ROOT)/inc/aarch64 \
         -I$(EFI_ROOT)/inc/protocol -O2 -Wall
LDFLAGS = -nostdlib -znocombreloc --defsym=EFI_SUBSYSTEM=10 \
          -T $(EFI_ROOT)/gnuefi/elf_aarch64_efi.lds -shared -Bsymbolic \
          -L$(EFI_ROOT)/aarch64/lib -L$(EFI_ROOT)/aarch64/gnuefi \
          $(EFI_ROOT)/aarch64/gnuefi/crt0-efi-aarch64.o

TARGET = M1.EFI
all: $(TARGET)
m1.o: m1.c
	$(CC) $(CFLAGS) -c $< -o $@
m1.so: m1.o
	$(LD) $(LDFLAGS) -o $@ $< -lgnuefi -lefi
$(TARGET): m1.so
	$(STRIP) --strip-debug $< -o /tmp/_m1s.so
	python3 ../../../../tools/pe_flatten_aarch64.py /tmp/_m1s.so $@
	@ls -l $@
clean:
	rm -f m1.o m1.so $(TARGET)
