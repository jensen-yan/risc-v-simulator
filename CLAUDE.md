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

模拟器采用分层模块化设计，组件职责清晰分离：

### 目录结构

```
include/
├── common/          # 通用类型和接口定义
│   ├── types.h              # 基础数据类型  
│   ├── debug_types.h        # 调试相关类型
│   └── cpu_interface.h      # CPU通用接口
├── core/            # 核心组件
│   ├── memory.h             # 内存管理
│   ├── alu.h               # 算术逻辑单元
│   ├── decoder.h           # 指令解码器
│   └── instruction_executor.h  # 指令执行器
├── cpu/             # CPU实现
│   ├── inorder/            # 顺序执行CPU
│   │   └── cpu.h           # 基础CPU实现
│   └── ooo/                # 乱序执行CPU
│       ├── ooo_cpu.h       # 乱序CPU主控制器
│       ├── ooo_types.h     # 乱序CPU专用类型
│       ├── register_rename.h    # 寄存器重命名
│       ├── reorder_buffer.h     # 重排序缓冲区
│       └── reservation_station.h   # 保留站
├── system/          # 系统级组件
│   ├── simulator.h         # 模拟器控制器
│   ├── elf_loader.h        # ELF文件加载器
│   └── syscall_handler.h   # 系统调用处理
└── utils/           # 工具类（可扩展）
```

### 核心组件

**Simulator 类** (`system/simulator.h`): 主要接口，协调所有组件
- 处理程序加载（二进制文件和ELF）
- 提供执行控制（单步/运行模式）
- 管理调试和状态检查
- 支持不同CPU实现的切换

**CPU 类** (`cpu/inorder/cpu.h`): 顺序执行CPU实现
- 管理32个通用寄存器（x0-x31，其中x0始终为零）
- 处理程序计数器和指令分发
- 实现所有RV32I指令类型（R、I、S、B、U、J）

**OOO_CPU 类** (`cpu/ooo/ooo_cpu.h`): 乱序执行CPU实现
- 支持指令级并行（ILP）
- 实现动态调度和推测执行
- 包含寄存器重命名、重排序缓冲区、保留站等组件

**Memory 类** (`core/memory.h`): 线性内存管理
- 默认4KB内存（可用-m标志配置）
- 小端字节序
- 支持字节、半字、字访问
- 所有内存操作都进行边界检查

**Decoder 类** (`core/decoder.h`): 指令解析和验证
- 将32位机器码解码为结构化格式
- 提取操作码、寄存器、立即数和功能码
- 支持所有RISC-V指令格式

**ALU 类** (`core/alu.h`): 算术和逻辑运算
- 处理所有计算操作
- 实现分支条件评估
- 支持有符号/无符号比较

## 指令集实现

模拟器实现完整的RV32I基础指令集（37条指令）以及多个扩展：

### RV32I 基础指令集
- **算术运算**: ADD, SUB, ADDI
- **逻辑运算**: AND, OR, XOR, ANDI, ORI, XORI
- **移位运算**: SLL, SRL, SRA, SLLI, SRLI, SRAI
- **比较运算**: SLT, SLTU, SLTI, SLTIU
- **内存访问**: LW, LH, LB, LBU, LHU, SW, SH, SB
- **分支跳转**: BEQ, BNE, BLT, BGE, BLTU, BGEU
- **无条件跳转**: JAL, JALR
- **上位立即数**: LUI, AUIPC
- **系统指令**: ECALL, EBREAK

### M扩展（乘除法指令）
- **乘法运算**: MUL, MULH, MULHSU, MULHU
- **除法运算**: DIV, DIVU, REM, REMU
- **特殊处理**: 自动处理除零情况，符合RISC-V标准

### F扩展（单精度浮点）
- **基本运算**: FADD.S, FSUB.S, FMUL.S, FDIV.S
- **比较运算**: FEQ.S, FLT.S, FLE.S
- **类型转换**: FCVT.W.S, FCVT.WU.S, FCVT.S.W, FCVT.S.WU
- **IEEE 754**: 完全兼容IEEE 754单精度浮点标准

### C扩展（压缩指令）
- **16位指令**: 支持三个象限的主要压缩指令
- **自动扩展**: 透明转换为32位指令格式
- **空间优化**: 代码密度提升约25-30%

**支持的压缩指令类型**:
- Quadrant 0: C.ADDI4SPN, C.LW, C.SW
- Quadrant 1: C.ADDI, C.JAL, C.LI, C.LUI, C.ADDI16SP, 算术运算, C.BEQZ, C.BNEZ
- Quadrant 2: C.SLLI, C.LWSP, C.SWSP, C.JR, C.MV, C.JALR, C.ADD, C.EBREAK

## 程序加载和执行

### 程序加载器

**loadRiscvProgram()**: 二进制程序加载器
- 在指定地址加载程序（默认0x1000）
- 初始化栈指针（x2）到内存顶部
- 设置帧指针（x8）为栈指针值
- 遵循RISC-V调用约定

**loadElfProgram()**: ELF文件加载器
- 自动解析ELF文件头和程序头表
- 支持多段程序加载（代码段、数据段等）
- 正确设置段权限（读/写/执行）
- 自动初始化BSS段为零
- 设置程序入口点

### 执行控制流程

**step()执行流程**:
1. 从PC处的内存取指令（支持16位/32位指令自动检测）
2. 将指令解码为结构化格式
3. 根据指令类型分发到相应执行器
4. 更新寄存器、内存和PC
5. 检查停机条件和异常

**run()执行模式**:
- 连续执行直到程序停机
- 100,000条指令安全限制
- 支持ECALL停机
- 自动处理系统调用

### 系统调用支持

**支持的系统调用**:
- **SYS_EXIT (93)**: 程序退出，支持退出码
- **SYS_WRITE (64)**: 标准输出/错误输出
- **SYS_READ (63)**: 标准输入读取  
- **SYS_BRK (214)**: 简化的内存断点管理

**ABI兼容性**:
- 系统调用号在a7寄存器（x17）
- 参数通过a0-a6寄存器传递
- 返回值存放在a0寄存器
- 完全兼容RISC-V Linux ABI

## 内存布局

### 地址空间组织
```
0x00000000  ┌─────────────────────────────────────┐
            │              保留区域                 │ 
0x00001000  ├─────────────────────────────────────┤
            │                                     │
            │            程序代码段                 │ ← 默认加载地址
            │         (.text section)              │
            ├─────────────────────────────────────┤
            │            程序数据段                 │
            │      (.data/.rodata section)         │
            ├─────────────────────────────────────┤
            │             BSS段                    │
            │         (零初始化数据)                │
            ├─────────────────────────────────────┤
            │               堆区                    │
            │          (动态分配)                   │
            ├─────────────────────────────────────┤
            │               ...                    │
            ├─────────────────────────────────────┤
            │               栈区                    │ ← sp初始位置
内存顶部     └─────────────────────────────────────┘
```

### 内存特征
- **线性地址空间**: 32位统一地址空间
- **小端字节序**: 所有多字节访问使用小端序
- **动态大小**: 默认4KB，可通过-m参数调整
- **边界检查**: 所有内存访问都进行越界检查
- **对齐要求**: 指令2字节对齐，数据访问自然对齐

## 测试架构

### 测试层次结构
```
┌─────────────────────────────────────────────────────────────┐
│                     集成测试层                               │
│  • riscv-tests官方测试套件                                   │
│  • 完整程序执行验证                                          │
│  • 多扩展综合测试                                            │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                     组件测试层                               │
│  • CPU指令执行测试                                           │
│  • Memory访问模式测试                                        │  
│  • Decoder解码正确性测试                                     │
│  • 跨组件交互测试                                            │
└─────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────────────────────────────────────┐
│                     单元测试层                               │
│  • 单个函数功能验证                                          │
│  • 边界条件测试                                             │
│  • 异常处理测试                                             │
│  • 性能基准测试                                             │
└─────────────────────────────────────────────────────────────┘
```

### 测试覆盖范围
- **指令测试**: 覆盖RV32I + M/F/C扩展的所有指令
- **边界测试**: 内存边界、寄存器范围、立即数范围
- **异常测试**: 非法指令、内存越界、除零等错误处理
- **兼容性测试**: 与标准RISC-V行为对比验证
- **性能测试**: 执行效率和内存使用量评估

### riscv-tests集成
- **官方测试套件**: 集成RISC-V官方测试程序
- **自动化验证**: 批量运行和结果统计
- **回归测试**: 确保新功能不破坏现有特性
- **覆盖率报告**: 详细的测试覆盖率分析

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

## 调试输出

参考  ./risc-v-sim --help 来了解如何使用调试输出。