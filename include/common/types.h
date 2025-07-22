#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

namespace riscv {

// 基本数据类型定义
using uint64_t = std::uint64_t;
using int64_t = std::int64_t;
using uint32_t = std::uint32_t;
using int32_t = std::int32_t;
using uint16_t = std::uint16_t;
using int16_t = std::int16_t;
using uint8_t = std::uint8_t;
using int8_t = std::int8_t;

// 寄存器编号类型
using RegNum = uint8_t;

// 内存地址类型
using Address = uint64_t;

// 寄存器值类型
using RegValue = uint64_t;

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
    SYSTEM_TYPE, // 系统指令
    UNKNOWN
};

// 指令操作码枚举
enum class Opcode : uint8_t {
    // R-type
    OP          = 0b0110011,    // ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU, M扩展
    
    // I-type
    OP_IMM      = 0b0010011,    // ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, SLTI, SLTIU
    LOAD        = 0b0000011,    // LW, LH, LB, LBU, LHU, FLW
    JALR        = 0b1100111,    // JALR
    
    // S-type
    STORE       = 0b0100011,    // SW, SH, SB, FSW
    
    // B-type
    BRANCH      = 0b1100011,    // BEQ, BNE, BLT, BGE, BLTU, BGEU
    
    // U-type
    LUI         = 0b0110111,    // LUI
    AUIPC       = 0b0010111,    // AUIPC
    
    // J-type
    JAL         = 0b1101111,    // JAL
    
    // RV64I新增操作码
    OP_IMM_32   = 0b0011011,    // ADDIW, SLLIW, SRLIW, SRAIW
    OP_32       = 0b0111011,    // ADDW, SUBW, SLLW, SRLW, SRAW
    
    // 浮点运算指令 (F/D扩展)
    OP_FP       = 0b1010011,    // 浮点运算指令
    
    // 同步指令
    MISC_MEM    = 0b0001111,    // FENCE指令
    
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
    
    // M扩展 - 乘除法运算
    MUL     = 0b000,
    MULH    = 0b001,
    MULHSU  = 0b010,
    MULHU   = 0b011,
    DIV     = 0b100,
    DIVU    = 0b101,
    REM     = 0b110,
    REMU    = 0b111,
    
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
    LD      = 0b011,    // RV64I: 加载双字(64位)
    LBU     = 0b100,
    LHU     = 0b101,
    LWU     = 0b110,    // RV64I: 加载字(32位)零扩展
    
    // 存储指令
    SB      = 0b000,
    SH      = 0b001,
    SW      = 0b010,
    SD      = 0b011,    // RV64I: 存储双字(64位)
    
    // 系统调用和特权指令
    ECALL_EBREAK = 0b000,
    
    // 浮点比较指令
    FEQ     = 0b010,
    FLT     = 0b001,
    FLE     = 0b000
};

enum class Funct7 : uint8_t {
    NORMAL  = 0b0000000,    // 普通运算
    SUB_SRA = 0b0100000,    // SUB, SRA
    M_EXT   = 0b0000001,    // M扩展指令
    
    // 浮点运算指令
    FADD_S  = 0b0000000,    // 浮点加法
    FSUB_S  = 0b0000100,    // 浮点减法
    FMUL_S  = 0b0001000,    // 浮点乘法
    FDIV_S  = 0b0001100,    // 浮点除法
    FSQRT_S = 0b0101100,    // 浮点开方
    FMIN_S  = 0b0010100,    // 浮点最小值
    FMAX_S  = 0b0010101,    // 浮点最大值
    FEQ_S   = 0b1010000,    // 浮点相等比较
    FLT_S   = 0b1010001,    // 浮点小于比较
    FLE_S   = 0b1010010,    // 浮点小于等于比较
    FCVT_W_S = 0b1100000,   // 浮点转整数
    FCVT_WU_S = 0b1100001,  // 浮点转无符号整数
    FCVT_S_W = 0b1101000,   // 整数转浮点
    FCVT_S_WU = 0b1101001   // 无符号整数转浮点
};

// 扩展支持标志
enum class Extension : uint32_t {
    I    = 0x1,     // 基本整数指令集
    M    = 0x2,     // 乘除法扩展
    A    = 0x4,     // 原子操作扩展
    F    = 0x8,     // 单精度浮点扩展
    D    = 0x10,    // 双精度浮点扩展
    C    = 0x20     // 压缩指令扩展
};

// 系统指令立即数常量
namespace SystemInst {
    constexpr uint32_t ECALL  = 0x000;  // 环境调用
    constexpr uint32_t EBREAK = 0x001;  // 环境断点
    constexpr uint32_t MRET   = 0x302;  // 机器模式返回
    constexpr uint32_t SRET   = 0x102;  // 监管模式返回 (可选)
    constexpr uint32_t URET   = 0x002;  // 用户模式返回 (可选)
    constexpr uint32_t WFI    = 0x105;  // 等待中断 (可选)
}

// 浮点舍入模式
enum class FPRoundingMode : uint8_t {
    RNE = 0b000,    // Round to Nearest, ties to Even
    RTZ = 0b001,    // Round towards Zero
    RDN = 0b010,    // Round Down (towards -∞)
    RUP = 0b011,    // Round Up (towards +∞)
    RMM = 0b100,    // Round to Nearest, ties to Max Magnitude
    DYN = 0b111     // Dynamic rounding mode
};

// 解码后的指令结构
struct DecodedInstruction {
    InstructionType type;
    Opcode opcode;
    RegNum rd;          // 目标寄存器
    RegNum rs1;         // 源寄存器1
    RegNum rs2;         // 源寄存器2
    RegNum rs3;         // 源寄存器3（用于浮点融合乘加）
    int32_t imm;        // 立即数
    Funct3 funct3;
    Funct7 funct7;
    FPRoundingMode rm;  // 浮点舍入模式
    bool is_compressed; // 是否为压缩指令
    
    // 静态执行属性 - 由解码器一次性计算，避免运行时重复解析
    uint8_t memory_access_size; // 内存访问大小（字节），0表示非内存指令
    bool is_signed_load;        // 是否为符号扩展的加载指令
    uint8_t execution_cycles;   // 预期执行周期数
    bool has_decode_exception;  // 解码时发现的异常（如非法funct3）
    std::string decode_exception_msg; // 解码异常消息
    
    DecodedInstruction() : type(InstructionType::UNKNOWN), opcode(static_cast<Opcode>(0)),
                          rd(0), rs1(0), rs2(0), rs3(0), imm(0), 
                          funct3(static_cast<Funct3>(0)), funct7(static_cast<Funct7>(0)),
                          rm(FPRoundingMode::RNE), is_compressed(false),
                          memory_access_size(0), is_signed_load(false), execution_cycles(1),
                          has_decode_exception(false) {}
    
    // 初始化静态执行属性 - 在解码器中调用
    void initialize_execution_properties();
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