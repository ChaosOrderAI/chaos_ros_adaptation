# 同步板核心逻辑的主机端测试
#
#   make        编译并运行
#   make clean

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O2 -I../firmware/core
CORE    := ../firmware/core/sync_core.c
TARGET  := test_sync_core

.PHONY: all clean
all: $(TARGET)
	./$(TARGET)

$(TARGET): test_sync_core.c $(CORE) ../firmware/core/sync_core.h
	$(CC) $(CFLAGS) -o $@ test_sync_core.c $(CORE)

clean:
	rm -f $(TARGET)
