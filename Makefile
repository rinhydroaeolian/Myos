# Makefile for myOS Shell Prototype
# 构建一个小型操作系统原型 Shell，包含内置命令、编辑器和设备驱动
#
# 目标:
#   make        — 编译所有源文件，链接生成 myos 可执行文件
#   make clean  — 删除所有生成的目标文件和可执行文件
#   make run    — 编译并运行 myos

CC       = gcc
CFLAGS   = -Wall -Wextra -g -O2
LDFLAGS  = -lpthread -lm
TARGET   = myos

# 源文件列表
SRCS = myos.c      \
       shell.c     \
       commands.c  \
       editor.c    \
       printer.c   \
       keyboard.c  \
       process.c

# 目标文件 (将 .c 替换为 .o)
OBJS = $(SRCS:.c=.o)

# 默认目标
.PHONY: all
all: $(TARGET)

# 链接生成最终可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: ./$(TARGET)"

# 编译规则: .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理目标文件和可执行文件
.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "Cleaned build artifacts."

# 编译并运行
.PHONY: run
run: $(TARGET)
	./$(TARGET)

# 显示帮助
.PHONY: help
help:
	@echo "myOS Shell — Build Targets:"
	@echo "  make        Build the myOS shell"
	@echo "  make clean  Remove build artifacts"
	@echo "  make run    Build and run the shell"
	@echo "  make help   Show this help message"

# 依赖关系
myos.o:      myos.c      shell.h commands.h process.h keyboard.h printer.h datastruct.h
shell.o:     shell.c     shell.h commands.h process.h datastruct.h
commands.o:  commands.c  commands.h shell.h process.h editor.h printer.h keyboard.h datastruct.h
editor.o:    editor.c    editor.h datastruct.h
printer.o:   printer.c   printer.h datastruct.h
keyboard.o:  keyboard.c  keyboard.h datastruct.h
process.o:   process.c   process.h datastruct.h
