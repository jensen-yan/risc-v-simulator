# RISC-V 模拟器 API 文档

## 概述

本文档详细描述了RISC-V模拟器中各个核心类的API接口和实现细节。模拟器采用模块化设计，每个组件职责清晰分离。

## 核心架构

### 命名空间
所有类都位于 `riscv` 命名空间中。

### 主要组件
- **Simulator**: 模拟器主接口
- **CPU**: 处理器核心实现 
- **Memory**: 内存管理器
- **Decoder**: 指令解码器
- **ALU**: 算术逻辑单元
- **ElfLoader**: ELF文件加载器
- **SyscallHandler**: 系统调用处理器

---

## 1. Simulator 类

### 描述
模拟器的主要接口类，协调所有组件的工作。

### 构造函数
```cpp
Simulator(size_t memorySize = Memory::DEFAULT_SIZE)
```
- **参数**: `memorySize` - 内存大小（字节），默认4KB
- **功能**: 初始化模拟器，创建CPU和内存组件

### 程序加载接口

#### loadRiscvProgram()
```cpp
bool loadRiscvProgram(const std::string& filename, Address loadAddr = 0x1000)
```
- **功能**: 加载二进制RISC-V程序
- **参数**: 
  - `filename`: 程序文件路径
  - `loadAddr`: 加载地址（默认0x1000）
- **返回值**: 成功返回true，失败返回false
- **说明**: 自动设置RISC-V ABI环境（栈指针、帧指针）

#### loadElfProgram()
```cpp
bool loadElfProgram(const std::string& filename)
```
- **功能**: 加载ELF格式程序文件
- **参数**: `filename` - ELF文件路径
- **返回值**: 成功返回true，失败返回false
- **说明**: 自动解析ELF头部，设置入口点和内存映射

#### loadProgramFromBytes()
```cpp
bool loadProgramFromBytes(const std::vector<uint8_t>& program, Address startAddr)
```
- **功能**: 从字节数组加载程序
- **参数**: 
  - `program`: 程序字节数据
  - `startAddr`: 起始地址
- **返回值**: 成功返回true，失败返回false

### 执行控制接口

#### step()
```cpp
void step()
```
- **功能**: 执行一条指令
- **异常**: 可能抛出`SimulatorException`

#### run()
```cpp
void run()
```
- **功能**: 运行程序直到停机
- **说明**: 包含指令数限制保护（100,000条指令）

#### reset()
```cpp
void reset()
```
- **功能**: 重置模拟器状态

### 状态查询接口

#### isHalted()
```cpp
bool isHalted() const
```
- **返回值**: 程序是否已停机

#### getInstructionCount()
```cpp
uint64_t getInstructionCount() const
```
- **返回值**: 已执行的指令数

#### getPC() / setPC()
```cpp
uint32_t getPC() const
void setPC(uint32_t pc)
```
- **功能**: 获取/设置程序计数器

#### getRegister() / setRegister()
```cpp
uint32_t getRegister(RegNum reg) const
void setRegister(RegNum reg, uint32_t value)
```
- **功能**: 获取/设置寄存器值
- **参数**: `reg` - 寄存器编号（0-31）

### 内存访问接口

#### readMemoryByte() / writeMemoryByte()
```cpp
uint8_t readMemoryByte(Address addr) const
void writeMemoryByte(Address addr, uint8_t value)
```

#### readMemoryWord() / writeMemoryWord()
```cpp
uint32_t readMemoryWord(Address addr) const
void writeMemoryWord(Address addr, uint32_t value)
```

### 调试接口

#### dumpRegisters()
```cpp
void dumpRegisters() const
```
- **功能**: 打印所有寄存器状态

#### dumpMemory()
```cpp
void dumpMemory(Address startAddr, size_t length) const
```
- **功能**: 打印指定内存区域的十六进制转储

#### dumpState()
```cpp
void dumpState() const
```
- **功能**: 打印完整的CPU状态

#### printStatistics()
```cpp
void printStatistics() const
```
- **功能**: 打印执行统计信息

---

## 2. CPU 类

### 描述
RISC-V处理器核心实现，支持RV32I基础指令集和M、F、C扩展。

### 构造函数
```cpp
CPU(std::shared_ptr<Memory> memory)
```
- **参数**: `memory` - 共享内存对象

### 执行引擎

#### step()
```cpp
void step()
```
- **功能**: 执行一条指令的取指-译码-执行循环
- **流程**:
  1. 从PC地址取指令
  2. 检测指令长度（16位压缩或32位标准）
  3. 解码指令
  4. 根据指令类型执行
  5. 更新PC

#### run()
```cpp
void run()
```
- **功能**: 连续执行直到停机条件满足
- **保护**: 100,000条指令限制

### 寄存器管理

#### 通用寄存器接口
```cpp
uint32_t getRegister(RegNum reg) const
void setRegister(RegNum reg, uint32_t value)
```
- **约束**: x0寄存器始终为0（硬连线）

#### 浮点寄存器接口
```cpp
uint32_t getFPRegister(RegNum reg) const
void setFPRegister(RegNum reg, uint32_t value)
float getFPRegisterFloat(RegNum reg) const
void setFPRegisterFloat(RegNum reg, float value)
```
- **功能**: 支持F扩展的浮点寄存器操作

### 指令执行器

#### executeRType()
```cpp
void executeRType(const DecodedInstruction& inst)
```
- **功能**: 执行R类型指令（寄存器-寄存器操作）
- **支持**: ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU
- **扩展**: M扩展乘除法指令，F扩展浮点指令

#### executeIType()
```cpp
void executeIType(const DecodedInstruction& inst)
```
- **功能**: 执行I类型指令（立即数操作）
- **支持**: 立即数运算、加载指令、JALR跳转

#### executeSType()
```cpp
void executeSType(const DecodedInstruction& inst)
```
- **功能**: 执行S类型指令（存储操作）

#### executeBType()
```cpp
void executeBType(const DecodedInstruction& inst)
```
- **功能**: 执行B类型指令（分支操作）
- **支持**: BEQ, BNE, BLT, BGE, BLTU, BGEU

#### executeUType()
```cpp
void executeUType(const DecodedInstruction& inst)
```
- **功能**: 执行U类型指令（上位立即数）
- **支持**: LUI, AUIPC

#### executeJType()
```cpp
void executeJType(const DecodedInstruction& inst)
```
- **功能**: 执行J类型指令（无条件跳转）
- **支持**: JAL

#### executeSystem()
```cpp
void executeSystem(const DecodedInstruction& inst)
```
- **功能**: 执行系统指令
- **支持**: ECALL, EBREAK, CSR指令

### 内存操作

#### loadFromMemory()
```cpp
uint32_t loadFromMemory(Address addr, Funct3 funct3)
```
- **功能**: 根据funct3执行不同宽度的加载操作
- **支持**: LB, LH, LW, LBU, LHU

#### storeToMemory()
```cpp
void storeToMemory(Address addr, uint32_t value, Funct3 funct3)
```
- **功能**: 根据funct3执行不同宽度的存储操作
- **支持**: SB, SH, SW

### 系统调用处理

#### handleEcall()
```cpp
void handleEcall()
```
- **功能**: 处理ECALL系统调用
- **实现**: 委托给SyscallHandler处理

#### handleEbreak()
```cpp
void handleEbreak()
```
- **功能**: 处理EBREAK断点指令

### 扩展指令支持

#### executeMExtension()
```cpp
void executeMExtension(const DecodedInstruction& inst)
```
- **功能**: 执行M扩展指令（乘除法）
- **支持**: MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU

#### executeFPExtension()
```cpp
void executeFPExtension(const DecodedInstruction& inst)
```
- **功能**: 执行F扩展指令（单精度浮点）
- **支持**: FADD.S, FSUB.S, FMUL.S, FDIV.S, 比较和转换指令

---

## 3. Memory 类

### 描述
线性内存管理器，提供字节序正确的内存访问接口。

### 构造函数
```cpp
Memory(size_t size)
```
- **参数**: `size` - 内存大小（字节）
- **约束**: 大小不能为0

### 基本访问接口

#### 字节访问
```cpp
uint8_t readByte(Address addr) const
void writeByte(Address addr, uint8_t value)
```

#### 半字访问（16位）
```cpp
uint16_t readHalfWord(Address addr) const
void writeHalfWord(Address addr, uint16_t value)
```
- **字节序**: 小端序

#### 字访问（32位）
```cpp
uint32_t readWord(Address addr) const
void writeWord(Address addr, uint32_t value)
```
- **字节序**: 小端序

### 指令取指

#### fetchInstruction()
```cpp
Instruction fetchInstruction(Address addr) const
```
- **功能**: 取指令，支持C扩展（16位和32位指令）
- **对齐**: 2字节对齐要求
- **返回**: 16位压缩指令或32位标准指令

### 程序加载

#### loadProgram()
```cpp
void loadProgram(const std::vector<uint8_t>& program, Address startAddr = 0)
```
- **功能**: 将程序数据加载到指定内存地址
- **检查**: 自动验证内存边界

### 工具函数

#### clear()
```cpp
void clear()
```
- **功能**: 清零所有内存

#### dump()
```cpp
void dump(Address startAddr, size_t length) const
```
- **功能**: 十六进制转储内存内容
- **格式**: 地址 + 十六进制数据 + ASCII字符

#### getSize()
```cpp
size_t getSize() const
```
- **返回**: 内存总大小

---

## 4. Decoder 类

### 描述
RISC-V指令解码器，支持标准32位指令和C扩展16位压缩指令。

### 主要解码接口

#### decode()
```cpp
DecodedInstruction decode(Instruction instruction, uint32_t enabled_extensions) const
```
- **功能**: 解码32位标准指令
- **参数**: 
  - `instruction`: 32位指令码
  - `enabled_extensions`: 启用的扩展标志位
- **返回**: 结构化的解码结果

#### decodeCompressed()
```cpp
DecodedInstruction decodeCompressed(uint16_t instruction, uint32_t enabled_extensions) const
```
- **功能**: 解码16位压缩指令
- **实现**: 将压缩指令扩展为等价的32位指令格式

### 字段提取函数

#### 基本字段提取
```cpp
Opcode extractOpcode(Instruction inst)
RegNum extractRd(Instruction inst)
RegNum extractRs1(Instruction inst)
RegNum extractRs2(Instruction inst)
RegNum extractRs3(Instruction inst)  // F扩展用
Funct3 extractFunct3(Instruction inst)
Funct7 extractFunct7(Instruction inst)
```

#### 立即数提取
```cpp
int32_t extractImmediateI(Instruction inst)  // I类型：12位符号扩展
int32_t extractImmediateS(Instruction inst)  // S类型：12位符号扩展
int32_t extractImmediateB(Instruction inst)  // B类型：13位符号扩展，左移1位
int32_t extractImmediateU(Instruction inst)  // U类型：20位左移12位
int32_t extractImmediateJ(Instruction inst)  // J类型：21位符号扩展，左移1位
```

### 指令类型判定

#### determineType()
```cpp
InstructionType determineType(Opcode opcode)
```
- **功能**: 根据操作码确定指令类型
- **返回**: R_TYPE, I_TYPE, S_TYPE, B_TYPE, U_TYPE, J_TYPE, SYSTEM_TYPE

### 压缩指令处理

#### isCompressedInstruction()
```cpp
bool isCompressedInstruction(uint16_t instruction)
```
- **检查**: 最低2位不是11表示压缩指令

#### expandCompressedInstruction()
```cpp
DecodedInstruction expandCompressedInstruction(uint16_t instruction)
```
- **功能**: 将C扩展指令转换为标准指令格式
- **支持**: 三个象限的主要压缩指令
  - Quadrant 0: C.ADDI4SPN, C.LW, C.SW
  - Quadrant 1: C.ADDI, C.JAL/C.J, C.LI, C.LUI/C.ADDI16SP, 算术运算, C.BEQZ, C.BNEZ
  - Quadrant 2: C.SLLI, C.LWSP, C.SWSP, C.JR/C.MV/C.JALR/C.ADD/C.EBREAK

### 验证函数

#### validateInstruction()
```cpp
void validateInstruction(const DecodedInstruction& decoded, uint32_t enabled_extensions)
```
- **检查**: 扩展启用状态和寄存器范围
- **异常**: 未启用扩展或无效寄存器时抛出异常

---

## 5. ALU 类

### 描述
算术逻辑单元，提供所有基本运算操作。

### 算术运算
```cpp
uint32_t add(uint32_t a, uint32_t b)     // 加法
uint32_t sub(uint32_t a, uint32_t b)     // 减法
```

### 逻辑运算
```cpp
uint32_t and_(uint32_t a, uint32_t b)    // 按位与
uint32_t or_(uint32_t a, uint32_t b)     // 按位或
uint32_t xor_(uint32_t a, uint32_t b)    // 按位异或
```

### 移位运算
```cpp
uint32_t sll(uint32_t a, uint32_t shamt) // 逻辑左移
uint32_t srl(uint32_t a, uint32_t shamt) // 逻辑右移
uint32_t sra(uint32_t a, uint32_t shamt) // 算术右移
```
- **说明**: 移位量自动取低5位（0-31）

### 比较运算
```cpp
bool slt(int32_t a, int32_t b)          // 有符号小于
bool sltu(uint32_t a, uint32_t b)       // 无符号小于
bool eq(uint32_t a, uint32_t b)         // 相等
bool ne(uint32_t a, uint32_t b)         // 不相等
bool lt(int32_t a, int32_t b)           // 有符号小于
bool ge(int32_t a, int32_t b)           // 有符号大于等于
bool ltu(uint32_t a, uint32_t b)        // 无符号小于
bool geu(uint32_t a, uint32_t b)        // 无符号大于等于
```

### 工具函数
```cpp
uint32_t signExtend(uint32_t value, int bits)
```
- **功能**: 符号扩展指定位数的值

---

## 6. ElfLoader 类

### 描述
ELF文件格式加载器，支持RISC-V 32位ELF可执行文件。

### 主要接口

#### loadElfFile()
```cpp
static ElfInfo loadElfFile(const std::string& filename, std::shared_ptr<Memory> memory)
```
- **功能**: 加载ELF文件到内存
- **返回**: ElfInfo结构包含加载信息
- **处理**: 自动解析程序头表，加载所有LOAD段

### ELF结构体

#### ElfInfo
```cpp
struct ElfInfo {
    bool isValid;                          // 加载是否成功
    Address entryPoint;                    // 程序入口点
    std::vector<ProgramSegment> segments;  // 程序段列表
}
```

#### ProgramSegment
```cpp
struct ProgramSegment {
    Address virtualAddr;        // 虚拟地址
    Address physicalAddr;       // 物理地址
    size_t fileSize;           // 文件中的大小
    size_t memorySize;         // 内存中的大小
    std::vector<uint8_t> data; // 段数据
    bool executable;           // 可执行标志
    bool writable;            // 可写标志
    bool readable;            // 可读标志
}
```

### 验证和解析

#### validateElfHeader()
```cpp
static bool validateElfHeader(const std::vector<uint8_t>& data)
```
- **检查**: ELF魔数、32位、小端、RISC-V架构、可执行文件

#### parseElfHeader() / parseProgramHeader()
```cpp
static ElfHeader parseElfHeader(const std::vector<uint8_t>& data)
static ProgramHeader parseProgramHeader(const std::vector<uint8_t>& data, size_t offset)
```

---

## 7. SyscallHandler 类

### 描述
系统调用处理器，实现基本的POSIX兼容系统调用。

### 构造函数
```cpp
SyscallHandler(std::shared_ptr<Memory> memory)
```

### 主要接口

#### handleSyscall()
```cpp
bool handleSyscall(CPU* cpu)
```
- **功能**: 处理系统调用
- **返回**: 是否需要停机
- **ABI**: a7寄存器为系统调用号，a0-a6为参数

### 支持的系统调用

#### SYS_EXIT (93)
```cpp
void handleExit(CPU* cpu)
```
- **功能**: 程序退出
- **参数**: a0 = 退出码
- **行为**: 打印测试结果，设置停机标志

#### SYS_WRITE (64)
```cpp
void handleWrite(CPU* cpu)
```
- **功能**: 写入数据
- **参数**: a0=文件描述符, a1=缓冲区地址, a2=字节数
- **支持**: stdout(1), stderr(2)
- **返回**: a0 = 写入的字节数

#### SYS_READ (63)
```cpp
void handleRead(CPU* cpu)
```
- **功能**: 读取数据
- **参数**: a0=文件描述符, a1=缓冲区地址, a2=字节数
- **支持**: stdin(0)
- **返回**: a0 = 读取的字节数

#### SYS_BRK (214)
```cpp
void handleBrk(CPU* cpu)
```
- **功能**: 设置程序断点（简化实现）
- **参数**: a0 = 新的断点地址
- **返回**: a0 = 设置的地址

### 工具函数

#### readStringFromMemory()
```cpp
std::string readStringFromMemory(Address addr, size_t maxLen = 256)
```
- **功能**: 从内存读取C风格字符串

#### writeStringToMemory()
```cpp
void writeStringToMemory(Address addr, const std::string& str)
```
- **功能**: 将字符串写入内存

---

## 数据类型和常量

### 基本类型
```cpp
using Address = uint32_t;           // 内存地址
using Instruction = uint32_t;       // 指令码
using RegNum = uint8_t;            // 寄存器编号
```

### 枚举类型

#### Opcode（操作码）
```cpp
enum class Opcode : uint8_t {
    LOAD      = 0x03,    // 加载指令
    MISC_MEM  = 0x0F,    // 内存屏障
    OP_IMM    = 0x13,    // 立即数运算
    AUIPC     = 0x17,    // 上位立即数加PC
    STORE     = 0x23,    // 存储指令
    OP        = 0x33,    // 寄存器运算
    LUI       = 0x37,    // 加载上位立即数
    BRANCH    = 0x63,    // 分支指令
    JALR      = 0x67,    // 寄存器跳转
    JAL       = 0x6F,    // 跳转并链接
    SYSTEM    = 0x73,    // 系统指令
    OP_FP     = 0x53     // 浮点运算
}
```

#### InstructionType（指令类型）
```cpp
enum class InstructionType {
    R_TYPE,      // 寄存器类型
    I_TYPE,      // 立即数类型
    S_TYPE,      // 存储类型
    B_TYPE,      // 分支类型
    U_TYPE,      // 上位立即数类型
    J_TYPE,      // 跳转类型
    SYSTEM_TYPE, // 系统类型
    UNKNOWN      // 未知类型
}
```

### 常量定义
```cpp
constexpr size_t NUM_REGISTERS = 32;      // 通用寄存器数量
constexpr size_t NUM_FP_REGISTERS = 32;   // 浮点寄存器数量
constexpr size_t DEFAULT_SIZE = 4 * 1024; // 默认内存大小（4KB）
```

---

## 异常类型

### SimulatorException
基础异常类，所有模拟器异常的基类。

### IllegalInstructionException
```cpp
class IllegalInstructionException : public SimulatorException
```
- **用途**: 遇到无效或不支持的指令时抛出

### MemoryException
```cpp
class MemoryException : public SimulatorException
```
- **用途**: 内存访问越界或无效访问时抛出

---

## 使用示例

### 基本程序执行
```cpp
#include "simulator.h"

int main() {
    try {
        // 创建4KB内存的模拟器
        riscv::Simulator simulator(4096);
        
        // 加载程序
        if (!simulator.loadRiscvProgram("program.bin")) {
            std::cerr << "程序加载失败" << std::endl;
            return 1;
        }
        
        // 运行程序
        simulator.run();
        
        // 打印统计信息
        simulator.printStatistics();
        
    } catch (const riscv::SimulatorException& e) {
        std::cerr << "模拟器错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### 单步调试
```cpp
riscv::Simulator simulator;
simulator.loadElfProgram("program.elf");

while (!simulator.isHalted()) {
    std::cout << "PC: 0x" << std::hex << simulator.getPC() << std::endl;
    simulator.dumpRegisters();
    simulator.step();
    
    // 用户交互...
}
```

## 扩展支持

### M扩展（乘除法）
- MUL, MULH, MULHSU, MULHU
- DIV, DIVU, REM, REMU
- 自动处理除零情况

### F扩展（单精度浮点）
- FADD.S, FSUB.S, FMUL.S, FDIV.S
- FEQ.S, FLT.S, FLE.S（比较）
- FCVT.W.S, FCVT.WU.S, FCVT.S.W, FCVT.S.WU（转换）

### C扩展（压缩指令）
- 支持三个象限的主要压缩指令
- 自动检测16位/32位指令
- 透明扩展为32位指令格式

---

*最后更新: 2024年12月*