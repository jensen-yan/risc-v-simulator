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
    R4_TYPE,    // R4型指令（融合乘加指令，4个源寄存器）
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
    
    // R4型融合乘加指令
    FMADD       = 0b1000011,    // 融合乘加 (FMADD.S/D)
    FMSUB       = 0b1000111,    // 融合乘减 (FMSUB.S/D)
    FNMSUB      = 0b1001011,    // 融合负乘减 (FNMSUB.S/D)
    FNMADD      = 0b1001111,    // 融合负乘加 (FNMADD.S/D)
    
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
    // 浮点加载指令
    FLW     = 0b010,    // 加载单精度浮点数
    FLD     = 0b011,    // 加载双精度浮点数
    
    // 存储指令
    SB      = 0b000,
    SH      = 0b001,
    SW      = 0b010,
    SD      = 0b011,    // RV64I: 存储双字(64位)
    // 浮点存储指令
    FSW     = 0b010,    // 存储单精度浮点数
    FSD     = 0b011,    // 存储双精度浮点数
    
    // 系统调用和特权指令
    ECALL_EBREAK = 0b000,
    
    // 浮点比较指令
    FEQ     = 0b010,
    FLT     = 0b001,
    FLE     = 0b000,
    
    // 浮点符号注入指令
    FSGNJ   = 0b000,    // 符号注入
    FSGNJN  = 0b001,    // 符号注入取反  
    FSGNJX  = 0b010,    // 符号注入异或
    
    // 浮点最小值/最大值指令
    FMIN    = 0b000,    // 最小值
    FMAX    = 0b001,    // 最大值
    
    // 浮点移动和分类指令
    FMV_CLASS = 0b000,  // FMV.X.W, FMV.X.D (rs2=00000)
    FCLASS    = 0b001   // FCLASS.S, FCLASS.D (rs2=00000)
};

enum class Funct7 : uint8_t {
    NORMAL  = 0b0000000,    // 普通运算
    SUB_SRA = 0b0100000,    // SUB, SRA
    M_EXT   = 0b0000001,    // M扩展指令
    
    // RV64F 单精度浮点指令
    FADD_S  = 0b0000000,    // 浮点加法
    FSUB_S  = 0b0000100,    // 浮点减法
    FMUL_S  = 0b0001000,    // 浮点乘法
    FDIV_S  = 0b0001100,    // 浮点除法
    FSQRT_S = 0b0101100,    // 浮点开方
    FSGNJ_S = 0b0010000,    // 浮点符号注入
    FMIN_FMAX_S = 0b0010100, // 浮点最小值/最大值 (funct3区分)
    FCVT_INT_S = 0b1100000, // 浮点转整数 (rs2字段区分W/WU/L/LU)
    FMV_X_W = 0b1110000,    // 浮点寄存器移动到整数寄存器 (funct3=000)
    FCLASS_S = 0b1110000,   // 浮点分类 (funct3=001)
    FCMP_S = 0b1010000,     // 浮点比较 (funct3区分：FEQ=010, FLT=001, FLE=000)
    FCVT_S_INT = 0b1101000, // 整数转浮点 (rs2字段区分W/WU/L/LU)
    FMV_W_X = 0b1111000,    // 整数寄存器移动到浮点寄存器
    
    // RV64D 双精度浮点指令
    FADD_D  = 0b0000001,    // 双精度浮点加法
    FSUB_D  = 0b0000101,    // 双精度浮点减法
    FMUL_D  = 0b0001001,    // 双精度浮点乘法
    FDIV_D  = 0b0001101,    // 双精度浮点除法
    FSQRT_D = 0b0101101,    // 双精度浮点开方
    FSGNJ_D = 0b0010001,    // 双精度浮点符号注入
    FMIN_FMAX_D = 0b0010101, // 双精度浮点最小值/最大值 (funct3区分)
    FCVT_INT_D = 0b1100001, // 双精度转整数 (rs2字段区分W/WU/L/LU)
    FMV_X_D = 0b1110001,    // 双精度寄存器移动到整数寄存器 (funct3=000)
    FCLASS_D = 0b1110001,   // 双精度浮点分类 (funct3=001)
    FCMP_D = 0b1010001,     // 双精度浮点比较 (funct3区分：FEQ=010, FLT=001, FLE=000)
    FCVT_D_INT = 0b1101001, // 整数转双精度 (rs2字段区分W/WU/L/LU)
    FMV_D_X = 0b1111001,    // 整数寄存器移动到双精度寄存器
    FCVT_S_D = 0b0100000,   // 双精度转单精度 (rs2=00001)
    FCVT_D_S = 0b0100001    // 单精度转双精度 (rs2=00000)
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

// CSR地址常量
namespace CSRAddr {
    // 浮点CSR寄存器
    constexpr uint16_t FFLAGS = 0x001;  // 浮点异常标志
    constexpr uint16_t FRM    = 0x002;  // 浮点舍入模式
    constexpr uint16_t FCSR   = 0x003;  // 浮点控制和状态寄存器
    
    // 机器模式CSR（简化支持）
    constexpr uint16_t MSTATUS = 0x300; // 机器状态寄存器
    constexpr uint16_t MISA    = 0x301; // 机器ISA寄存器
    constexpr uint16_t MIE     = 0x304; // 机器中断使能
    constexpr uint16_t MTVEC   = 0x305; // 机器陷阱向量
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