# RISC-V CPU 模拟器

一个简单的 RISC-V CPU 模拟器，实现了 RV32I 指令集。

## 功能特性

- 完整的 RV32I 指令集支持（37条指令）
- 32个通用寄存器（x0-x31）
- 4KB 内存空间
- 单步执行和连续执行模式
- 寄存器和内存状态查看
- 基础系统调用支持

## 构建说明

```bash
mkdir build
cd build
cmake ..
make

# 生成compile_commands.json
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
```

## 使用方法

```bash
./risc-v-sim [program.bin]
```

## 测试

```bash
make test
```

## 项目结构

```
├── src/           # 源代码文件
├── include/       # 头文件
├── tests/         # 单元测试
├── examples/      # 示例程序
└── docs/          # 文档
```

## 支持的指令

### 算术运算
- ADD, SUB, AND, OR, XOR, SLL, SRL, SRA
- ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI

### 内存操作
- LW, LH, LB, LBU, LHU
- SW, SH, SB

### 控制流
- BEQ, BNE, BLT, BGE, BLTU, BGEU
- JAL, JALR

### 系统指令
- ECALL, EBREAK

## 开发说明

本项目遵循测试驱动开发（TDD）实践。每条指令都有对应的单元测试。

python3 run_tests.py

来运行测试，会自动运行riscv-tests 中的rv32ui 测试。

./risc-v-sim -e -d -m 2164260864 ../riscv-tests/isa/rv32uc-p-rvc

可以运行C 扩展指令测试