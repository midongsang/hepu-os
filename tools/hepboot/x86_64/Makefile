# 赫普OS 64位引导器 Makefile

AS = as
LD = ld
OBJCOPY = objcopy

# 编译选项
ASFLAGS = --32
LDFLAGS = -m elf_i386 -Ttext 0x8000 --oformat binary

# 目标
TARGET = stage2_64.bin
SOURCE = stage2_64.S

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(AS) $(ASFLAGS) -o stage2_64.o $(SOURCE)
	$(LD) $(LDFLAGS) -o $(TARGET) stage2_64.o
	@echo "  [BUILD] $(TARGET) ($(shell wc -c < $(TARGET)) bytes)"

clean:
	rm -f stage2_64.o $(TARGET)

.PHONY: all clean
