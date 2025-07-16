# Makefile for RISC-V runtime library and test programs

# 工具链设置
CC = riscv64-unknown-elf-gcc
AS = riscv64-unknown-elf-as
LD = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy

# 编译选项
CFLAGS = -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -static -fno-builtin -O1
ASFLAGS = -march=rv32i -mabi=ilp32
LDFLAGS = -T link.ld -nostdlib -static

# 目标文件
RUNTIME_OBJS = start.o minilib.o

# 测试程序源文件（不带.elf后缀）
# 从programs目录下获取所有.c文件
TEST_NAMES = $(shell ls ../programs/*.c | sed 's/\.\.\/programs\///' | sed 's/\.c//')
TEST_PROGRAMS = $(addsuffix .elf, $(TEST_NAMES))

.PHONY: all clean

all: $(TEST_PROGRAMS)

# 运行时库
start.o: start.s
	$(AS) $(ASFLAGS) -o $@ $<

minilib.o: minilib.c minilib.h
	$(CC) $(CFLAGS) -c -o $@ $<

# 统一规则编译所有测试程序
%.elf: ../programs/%.c $(RUNTIME_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(RUNTIME_OBJS) $(LDFLAGS)

# 清理
clean:
	rm -f *.o *.elf *.bin

# 生成二进制文件（可选）
%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

# 生成所有反汇编文件
all_dis: $(TEST_PROGRAMS)
	for elf in $(TEST_PROGRAMS); do \
		riscv64-unknown-elf-objdump -d $$elf > $$elf.dis; \
	done

# 反汇编（调试用）
%.dis: %.elf
	riscv64-unknown-elf-objdump -d $< > $@