# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

## 核心架构

模拟器采用模块化设计，组件职责清晰分离：

**Simulator 类**: 主要接口，协调所有组件
- 处理程序加载（二进制文件和ELF）
- 提供执行控制（单步/运行模式）
- 管理调试和状态检查

**CPU 类**: 实现取指-译码-执行循环
- 管理32个通用寄存器（x0-x31，其中x0始终为零）
- 处理程序计数器和指令分发
- 实现所有RV32I指令类型（R、I、S、B、U、J）

**Memory 类**: 线性内存管理
- 默认4KB内存（可用-m标志配置）
- 小端字节序
- 支持字节、半字、字访问
- 所有内存操作都进行边界检查

**Decoder 类**: 指令解析和验证
- 将32位机器码解码为结构化格式
- 提取操作码、寄存器、立即数和功能码
- 支持所有RISC-V指令格式

**ALU 类**: 算术和逻辑运算
- 处理所有计算操作
- 实现分支条件评估
- 支持有符号/无符号比较

## 指令集实现

模拟器实现完整的RV32I基础指令集（37条指令）：
- **算术运算**: ADD, SUB, ADDI
- **逻辑运算**: AND, OR, XOR, ANDI, ORI, XORI
- **移位运算**: SLL, SRL, SRA, SLLI, SRLI, SRAI
- **比较运算**: SLT, SLTU, SLTI, SLTIU
- **内存访问**: LW, LH, LB, LBU, LHU, SW, SH, SB
- **分支跳转**: BEQ, BNE, BLT, BGE, BLTU, BGEU
- **无条件跳转**: JAL, JALR
- **上位立即数**: LUI, AUIPC
- **系统指令**: ECALL, EBREAK

## 程序加载和执行

**loadRiscvProgram()**: 增强的加载器，设置适当的RISC-V ABI环境
- 在指定地址加载程序（默认0x1000）
- 初始化栈指针（x2）到内存顶部
- 设置帧指针（x8）为栈指针值
- 遵循RISC-V调用约定

**执行流程**:
1. 从PC处的内存取指令
2. 将指令解码为结构化格式
3. 根据指令类型执行
4. 更新寄存器、内存和PC
5. 检查停机条件

## 内存布局

- **程序代码**: 默认加载在0x1000
- **栈**: 从内存顶部向下增长
- **堆**: 未实现（程序仅使用栈）
- **内存大小**: 默认4KB，可用-m标志扩展

## 测试结构

测试按组件组织：
- **单元测试**: 单个类测试
- **集成测试**: 跨组件功能测试
- **指令测试**: 每种指令类型验证
- **内存测试**: 边界和访问模式测试

## 错误处理

模拟器使用自定义异常：
- **SimulatorException**: 基础异常类
- **IllegalInstructionException**: 遇到未知操作码
- **MemoryException**: 无效内存访问

## 调试功能

- **寄存器转储**: 显示所有32个寄存器
- **内存转储**: 十六进制内存内容查看
- **单步模式**: 一次执行一条指令
- **统计信息**: 指令计数和执行指标

## RISC-V ABI 合规性

模拟器遵循标准RISC-V调用约定：
- **x0**: 始终为零（硬连线）
- **x1**: 返回地址（ra）
- **x2**: 栈指针（sp）
- **x8**: 帧指针（s0/fp）
- **x10**: 函数返回值（a0）

## 性能特征

- **执行速度**: 10,000+条指令/秒
- **内存使用**: 总占用<100MB
- **指令限制**: 100,000条指令（安全限制）
- **内存访问**: 针对顺序访问模式优化