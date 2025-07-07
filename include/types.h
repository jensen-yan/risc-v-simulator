#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

namespace riscv {

// 基本数据类型定义
using uint32_t = std::uint32_t;
using int32_t = std::int32_t;
using uint16_t = std::uint16_t;
using int16_t = std::int16_t;
using uint8_t = std::uint8_t;
using int8_t = std::int8_t;

// 寄存器编号类型
using RegNum = uint8_t;

// 内存地址类型
using Address = uint32_t;

// 指令类型
using Instruction = uint32_t;

// RISC-V 指令格式枚举
enum class InstructionType {
    R_TYPE,     // 寄存器-寄存器运算
    I_TYPE,     // 立即数运算、加载指令
    S_TYPE,     // 存储指令
    B_TYPE,     // 分支指令
    U_TYPE,     // 上位立即数指令
    J_TYPE,     // 跳转指令
    UNKNOWN
};

// 指令操作码枚举
enum class Opcode : uint8_t {
    // R-type
    OP          = 0b0110011,    // ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU
    
    // I-type
    OP_IMM      = 0b0010011,    // ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, SLTI, SLTIU
    LOAD        = 0b0000011,    // LW, LH, LB, LBU, LHU
    JALR        = 0b1100111,    // JALR
    
    // S-type
    STORE       = 0b0100011,    // SW, SH, SB
    
    // B-type
    BRANCH      = 0b1100011,    // BEQ, BNE, BLT, BGE, BLTU, BGEU
    
    // U-type
    LUI         = 0b0110111,    // LUI
    AUIPC       = 0b0010111,    // AUIPC
    
    // J-type
    JAL         = 0b1101111,    // JAL
    
    // 系统指令
    SYSTEM      = 0b1110011     // ECALL, EBREAK
};

// 功能码枚举（用于区分相同操作码的不同指令）
enum class Funct3 : uint8_t {
    // 算术和逻辑运算
    ADD_SUB = 0b000,
    SLL     = 0b001,
    SLT     = 0b010,
    SLTU    = 0b011,
    XOR     = 0b100,
    SRL_SRA = 0b101,
    OR      = 0b110,
    AND     = 0b111,
    
    // 分支指令
    BEQ     = 0b000,
    BNE     = 0b001,
    BLT     = 0b100,
    BGE     = 0b101,
    BLTU    = 0b110,
    BGEU    = 0b111,
    
    // 加载指令
    LB      = 0b000,
    LH      = 0b001,
    LW      = 0b010,
    LBU     = 0b100,
    LHU     = 0b101,
    
    // 存储指令
    SB      = 0b000,
    SH      = 0b001,
    SW      = 0b010
};

enum class Funct7 : uint8_t {
    NORMAL  = 0b0000000,    // 普通运算
    SUB_SRA = 0b0100000     // SUB, SRA
};

// 解码后的指令结构
struct DecodedInstruction {
    InstructionType type;
    Opcode opcode;
    RegNum rd;          // 目标寄存器
    RegNum rs1;         // 源寄存器1
    RegNum rs2;         // 源寄存器2
    int32_t imm;        // 立即数
    Funct3 funct3;
    Funct7 funct7;
    
    DecodedInstruction() : type(InstructionType::UNKNOWN), opcode(static_cast<Opcode>(0)),
                          rd(0), rs1(0), rs2(0), imm(0), 
                          funct3(static_cast<Funct3>(0)), funct7(static_cast<Funct7>(0)) {}
};

// 异常类型
class SimulatorException : public std::runtime_error {
public:
    explicit SimulatorException(const std::string& message) : std::runtime_error(message) {}
};

class IllegalInstructionException : public SimulatorException {
public:
    explicit IllegalInstructionException(const std::string& message) 
        : SimulatorException("非法指令: " + message) {}
};

class MemoryException : public SimulatorException {
public:
    explicit MemoryException(const std::string& message) 
        : SimulatorException("内存错误: " + message) {}
};

} // namespace riscv