# 赫普OS - 3D图形演示应用 Makefile
# V0.9.0

CC = gcc
CFLAGS = -m32 -ffreestanding -fno-builtin -nostdlib -Wall -Wextra -O2
LDFLAGS = -m32 -nostdlib -Wl,-Ttext=0x1000000 -e _start

TARGET = gfx3d_demo
SOURCES = gfx3d_demo.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
