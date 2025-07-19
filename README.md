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
make -j

# 生成debug 版本
cmake -DCMAKE_BUILD_TYPE=debug ..
make -j
```

## 使用方法

```bash
./risc-v-sim [program.bin]
```

## 项目结构

```
├── src/           # 源代码文件
├── include/       # 头文件
├── tests/         # 单元测试
├── riscv-tests/    # riscv-tests 测试用例
├── runtime/        # 运行时库，主要包好minilibc库
├── programs/       # 测试程序，结合runtime库生成elf 文件，用于测试
└── docs/          # 文档
```

## 支持的指令

目前支持RISCV 的RV32I 指令集，以及C 扩展指令集，还有乘除法指令。

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

## 构建riscv-tests

```bash
git submodule update --init --recursive

# only for ubuntu 22.04
source setup_riscv_env.sh

cd riscv-tests

# see README.md in riscv-tests
make -j 

```


## 开发说明

需要安装gtest 库，用于运行单元测试。

```bash
sudo apt-get install libgtest-dev
```

本项目遵循测试驱动开发（TDD）实践。每条指令都有对应的单元测试。

python3 run_tests.py

来运行测试，会自动运行riscv-tests 中的rv32ui 测试。

通过-p 参数可以指定运行测试的文件，例如：

python3 run_tests.py -p "rv32uc-p-*"

可以运行所有C 扩展指令测试，对应如下命令：

./risc-v-sim -e -d -m 2164260864 ../riscv-tests/isa/rv32uc-p-rvc

可以运行C 扩展指令测试