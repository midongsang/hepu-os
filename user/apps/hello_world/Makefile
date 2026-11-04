# 赫普OS 示例应用 Makefile
# Hello World 应用

APP_NAME = hello_world
APP_VERSION = 1.0.0

# 编译器
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
CFLAGS += -I../../lib/appfw
CFLAGS += -DAPP_NAME=\"$(APP_NAME)\" -DAPP_VERSION=\"$(APP_VERSION)\"

# 源文件
SRCS = hello_world.c ../../lib/appfw/app_framework.c
OBJS = $(SRCS:.c=.o)

# 目标
TARGET = $(APP_NAME)

# 默认目标
all: $(TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 编译
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理
clean:
	rm -f $(OBJS) $(TARGET)

# 运行
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
