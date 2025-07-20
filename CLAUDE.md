# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

默认用中文回答，代码注释也用中文。

## 构建命令

**标准构建:**
```bash
mkdir build && cd build
cmake ..
make
```

**调试构建:**
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

**运行测试:**
```bash
cd build
make test
# 或者直接运行:
./risc-v-tests
```

**清理构建:**
```bash
rm -rf build
```

## 构建技巧

- 请记住之后编译默认用make -j 来加快编译速度

## 运行模拟器

**基本执行:**
```bash
./risc-v-sim programs/test_simple.bin
```

**调试模式和单步执行:**
```bash
./risc-v-sim -s -d programs/test_simple.bin
```

**自定义内存大小:**
```bash
./risc-v-sim -m 8192 programs/test_simple.bin
```

## RISC-V 程序编译

**使用 CMake 编译测试程序:**
```bash
cd programs
mkdir build && cd build
cmake ..
make
```

**手动编译单个程序:**
```bash
cd programs
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -static -Wl,-Ttext=0x1000 -o test_simple.elf test_simple.c
riscv64-unknown-elf-objcopy -O binary test_simple.elf test_simple.bin
```

## 工作目录提醒

- 请关注当前是否在build 目录下，编译运行需要在build 目录下，查找文件等要返回上级目录！

## 调试记忆

- 你当前可能再build 目录下，所以找不到函数和文件，查找文件请回到主目录！