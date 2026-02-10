#include "core/instruction_executor.h"
#include "common/types.h"
#include <cmath>
#include <cfenv>
#include <limits>

namespace riscv {

#pragma STDC FENV_ACCESS ON

namespace {
constexpr uint32_t kCanonicalNaN32 = 0x7FC00000U;
constexpr uint64_t kCanonicalNaN64 = 0x7FF8000000000000ULL;
constexpr uint64_t kNanBoxMask = 0xFFFFFFFF00000000ULL;
constexpr uint64_t kNanBoxPrefix = 0xFFFFFFFF00000000ULL;

float bitsToFloat(uint32_t bits) {
    union {
        uint32_t u;
        float f;
    } conv{bits};
    return conv.f;
}

uint32_t floatToBits(float value) {
    union {
        uint32_t u;
        float f;
    } conv{};
    conv.f = value;
    return conv.u;
}

double bitsToDouble(uint64_t bits) {
    union {
        uint64_t u;
        double d;
    } conv{bits};
    return conv.d;
}

uint64_t doubleToBits(double value) {
    union {
        uint64_t u;
        double d;
    } conv{};
    conv.d = value;
    return conv.u;
}

uint64_t nanBoxSingle(uint32_t bits) {
    return kNanBoxPrefix | static_cast<uint64_t>(bits);
}

uint32_t unpackSingleOperand(uint64_t bits) {
    if ((bits & kNanBoxMask) == kNanBoxPrefix) {
        return static_cast<uint32_t>(bits);
    }
    return kCanonicalNaN32;
}

uint8_t fpFunct5(const DecodedInstruction& inst) {
    return static_cast<uint8_t>(inst.funct7) >> 2U;
}

uint8_t fpFormat(const DecodedInstruction& inst) {
    if (inst.opcode == Opcode::LOAD_FP || inst.opcode == Opcode::STORE_FP) {
        if (inst.funct3 == Funct3::LW) {
            return 0;
        }
        if (inst.funct3 == Funct3::LD) {
            return 1;
        }
        return 0xFF;
    }
    return static_cast<uint8_t>(inst.funct7) & 0x3U;
}

bool isDoubleFormat(const DecodedInstruction& inst) {
    return fpFormat(inst) == 1U;
}

uint32_t classifyFloat32(uint32_t bits) {
    const bool sign = (bits >> 31) != 0;
    const uint32_t exp = (bits >> 23) & 0xFFU;
    const uint32_t frac = bits & 0x7FFFFFU;

    if (exp == 0xFFU) {
        if (frac == 0) {
            return sign ? (1U << 0) : (1U << 7);  // -inf / +inf
        }
        const bool quiet_nan = (frac & (1U << 22)) != 0;
        return quiet_nan ? (1U << 9) : (1U << 8);
    }

    if (exp == 0U) {
        if (frac == 0U) {
            return sign ? (1U << 3) : (1U << 4);  // -0 / +0
        }
        return sign ? (1U << 2) : (1U << 5);  // subnormal
    }

    return sign ? (1U << 1) : (1U << 6);  // normal
}

uint32_t classifyFloat64(uint64_t bits) {
    const bool sign = (bits >> 63) != 0;
    const uint64_t exp = (bits >> 52) & 0x7FFULL;
    const uint64_t frac = bits & 0x000FFFFFFFFFFFFFULL;

    if (exp == 0x7FFULL) {
        if (frac == 0) {
            return sign ? (1U << 0) : (1U << 7);  // -inf / +inf
        }
        const bool quiet_nan = (frac & (1ULL << 51)) != 0;
        return quiet_nan ? (1U << 9) : (1U << 8);
    }

    if (exp == 0ULL) {
        if (frac == 0ULL) {
            return sign ? (1U << 3) : (1U << 4);  // -0 / +0
        }
        return sign ? (1U << 2) : (1U << 5);  // subnormal
    }

    return sign ? (1U << 1) : (1U << 6);  // normal
}

uint64_t signExtend32To64(uint32_t value) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(value)));
}

uint32_t atomicFunct5(const DecodedInstruction& inst) {
    return (static_cast<uint32_t>(inst.funct7) >> 2U) & 0x1FU;
}

constexpr uint8_t kFFlagsNx = 0x01;
constexpr uint8_t kFFlagsUf = 0x02;
constexpr uint8_t kFFlagsOf = 0x04;
constexpr uint8_t kFFlagsDz = 0x08;
constexpr uint8_t kFFlagsNv = 0x10;

bool isNaN32(uint32_t bits) {
    const uint32_t exp = (bits >> 23) & 0xFFU;
    const uint32_t frac = bits & 0x7FFFFFU;
    return exp == 0xFFU && frac != 0;
}

bool isNaN64(uint64_t bits) {
    const uint64_t exp = (bits >> 52) & 0x7FFULL;
    const uint64_t frac = bits & 0x000FFFFFFFFFFFFFULL;
    return exp == 0x7FFULL && frac != 0ULL;
}

bool isSignalingNaN32(uint32_t bits) {
    if (!isNaN32(bits)) {
        return false;
    }
    return (bits & 0x00400000U) == 0;
}

bool isSignalingNaN64(uint64_t bits) {
    if (!isNaN64(bits)) {
        return false;
    }
    return (bits & (1ULL << 51)) == 0;
}

uint8_t mapFEnvToFFlags(int excepts) {
    uint8_t flags = 0;
    if (excepts & FE_INVALID) flags |= kFFlagsNv;
    if (excepts & FE_DIVBYZERO) flags |= kFFlagsDz;
    if (excepts & FE_OVERFLOW) flags |= kFFlagsOf;
    if (excepts & FE_UNDERFLOW) flags |= kFFlagsUf;
    if (excepts & FE_INEXACT) flags |= kFFlagsNx;
    return flags;
}

int toFEnvRound(uint8_t rm) {
    switch (rm) {
        case 0b000: return FE_TONEAREST;   // RNE
        case 0b001: return FE_TOWARDZERO;  // RTZ
        case 0b010: return FE_DOWNWARD;    // RDN
        case 0b011: return FE_UPWARD;      // RUP
        case 0b100: return FE_TONEAREST;   // RMM 近似到最近值
        default: throw IllegalInstructionException("非法舍入模式");
    }
}

uint8_t resolveRoundingMode(const DecodedInstruction& inst, uint8_t current_frm) {
    uint8_t rm = static_cast<uint8_t>(inst.rm) & 0x7U;
    if (rm == 0b111) {
        rm = current_frm & 0x7U;
    }
    if (rm > 0b100) {
        throw IllegalInstructionException("保留的舍入模式");
    }
    return rm;
}

template <typename Fn>
uint8_t withFpEnv(uint8_t rm, Fn&& fn) {
    const int old_round = std::fegetround();
    std::fesetround(toFEnvRound(rm));
    std::feclearexcept(FE_ALL_EXCEPT);
    fn();
    const int excepts = std::fetestexcept(FE_ALL_EXCEPT);
    std::fesetround(old_round);
    return mapFEnvToFFlags(excepts);
}

float roundToMode(float value, uint8_t rm, uint8_t& out_flags) {
    float rounded = value;
    switch (rm) {
        case 0b000:  // RNE
            rounded = std::nearbyint(value);
            break;
        case 0b001:  // RTZ
            rounded = std::trunc(value);
            break;
        case 0b010:  // RDN
            rounded = std::floor(value);
            break;
        case 0b011:  // RUP
            rounded = std::ceil(value);
            break;
        case 0b100: {  // RMM: ties to max magnitude
            const float abs_v = std::fabs(value);
            const float floor_v = std::floor(abs_v);
            const float frac = abs_v - floor_v;
            float mag = floor_v;
            if (frac > 0.5f || frac == 0.5f) {
                mag = floor_v + 1.0f;
            }
            rounded = std::copysign(mag, value);
            break;
        }
        default:
            throw IllegalInstructionException("非法舍入模式");
    }

    if (rounded != value) {
        out_flags |= kFFlagsNx;
    }
    return rounded;
}

long double roundToModeLongDouble(long double value, uint8_t rm, uint8_t& out_flags) {
    long double rounded = value;
    switch (rm) {
        case 0b000:  // RNE
            rounded = std::nearbyint(value);
            break;
        case 0b001:  // RTZ
            rounded = std::trunc(value);
            break;
        case 0b010:  // RDN
            rounded = std::floor(value);
            break;
        case 0b011:  // RUP
            rounded = std::ceil(value);
            break;
        case 0b100: {  // RMM: ties to max magnitude
            const long double abs_v = std::fabs(value);
            const long double floor_v = std::floor(abs_v);
            const long double frac = abs_v - floor_v;
            long double mag = floor_v;
            if (frac > 0.5L || frac == 0.5L) {
                mag = floor_v + 1.0L;
            }
            rounded = std::copysign(mag, value);
            break;
        }
        default:
            throw IllegalInstructionException("非法舍入模式");
    }

    if (rounded != value) {
        out_flags |= kFFlagsNx;
    }
    return rounded;
}
}  // namespace

uint64_t InstructionExecutor::executeImmediateOperation(const DecodedInstruction& inst, uint64_t rs1_val) {
    switch (inst.funct3) {
        case Funct3::ADD_SUB:
            return rs1_val + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
            
        case Funct3::SLT:
            return (static_cast<int64_t>(rs1_val) < static_cast<int64_t>(inst.imm)) ? 1 : 0;
            
        case Funct3::SLTU:
            return (rs1_val < static_cast<uint64_t>(static_cast<int64_t>(inst.imm))) ? 1 : 0;
            
        case Funct3::XOR:
            return rs1_val ^ static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
            
        case Funct3::OR:
            return rs1_val | static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
            
        case Funct3::AND:
            return rs1_val & static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
            
        case Funct3::SLL:
            // RV64I: 使用imm的低6位作为移位量
            return rs1_val << (inst.imm & 0x3F);
            
        case Funct3::SRL_SRA:
            if (inst.funct7 == Funct7::SUB_SRA) {
                // 算术右移
                return static_cast<uint64_t>(static_cast<int64_t>(rs1_val) >> (inst.imm & 0x3F));
            } else {
                // 逻辑右移
                return rs1_val >> (inst.imm & 0x3F);
            }
            
        default:
            throw IllegalInstructionException("未知的立即数指令功能码");
    }
}

uint64_t InstructionExecutor::executeRegisterOperation(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val) {
    switch (inst.funct3) {
        case Funct3::ADD_SUB:
            return performArithmeticOperation(rs1_val, rs2_val, inst.funct3, inst.funct7);
            
        case Funct3::SLT:
        case Funct3::SLTU:
            return performComparisonOperation(rs1_val, rs2_val, inst.funct3);
            
        case Funct3::XOR:
        case Funct3::OR:
        case Funct3::AND:
            return performLogicalOperation(rs1_val, rs2_val, inst.funct3);
            
        case Funct3::SLL:
        case Funct3::SRL_SRA:
            return performShiftOperation(rs1_val, rs2_val, inst.funct3, inst.funct7);
            
        default:
            throw IllegalInstructionException("未知的寄存器指令功能码");
    }
}

bool InstructionExecutor::evaluateBranchCondition(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val) {
    switch (inst.funct3) {
        case Funct3::BEQ:
            return rs1_val == rs2_val;
            
        case Funct3::BNE:
            return rs1_val != rs2_val;
            
        case Funct3::BLT:
            return static_cast<int64_t>(rs1_val) < static_cast<int64_t>(rs2_val);
            
        case Funct3::BGE:
            return static_cast<int64_t>(rs1_val) >= static_cast<int64_t>(rs2_val);
            
        case Funct3::BLTU:
            return rs1_val < rs2_val;
            
        case Funct3::BGEU:
            return rs1_val >= rs2_val;
            
        default:
            throw IllegalInstructionException("未知的分支指令功能码");
    }
}

uint64_t InstructionExecutor::calculateJumpTarget(const DecodedInstruction& inst, uint64_t pc) {
    return pc + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
}

uint64_t InstructionExecutor::calculateJumpAndLinkTarget(const DecodedInstruction& inst, uint64_t pc, uint64_t rs1_val) {
    uint64_t target = rs1_val + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
    return target & 0xFFFFFFFFFFFFFFFE;  // 清除最低位，确保地址对齐
}

uint64_t InstructionExecutor::loadFromMemory(std::shared_ptr<Memory> memory, uint64_t addr, Funct3 funct3) {
    switch (funct3) {
        case Funct3::LB:
            return loadSignExtended(memory, addr, 1);
            
        case Funct3::LH:
            return loadSignExtended(memory, addr, 2);
            
        case Funct3::LW:
            return loadSignExtended(memory, addr, 4);
            
        case Funct3::LD:  // RV64I: 加载双字
            return memory->read64(addr);
            
        case Funct3::LBU:
            return loadZeroExtended(memory, addr, 1);
            
        case Funct3::LHU:
            return loadZeroExtended(memory, addr, 2);
            
        case Funct3::LWU:  // RV64I: 加载字零扩展
            return loadZeroExtended(memory, addr, 4);
            
        default:
            throw IllegalInstructionException("未知的加载指令功能码");
    }
}

void InstructionExecutor::storeToMemory(std::shared_ptr<Memory> memory, uint64_t addr, uint64_t value, Funct3 funct3) {
    switch (funct3) {
        case Funct3::SB:
            memory->writeByte(addr, static_cast<uint8_t>(value));
            break;
            
        case Funct3::SH:
            memory->writeHalfWord(addr, static_cast<uint16_t>(value));
            break;
            
        case Funct3::SW:
            memory->writeWord(addr, static_cast<uint32_t>(value));
            break;
            
        case Funct3::SD:  // RV64I: 存储双字
            memory->write64(addr, value);
            break;
            
        default:
            throw IllegalInstructionException("未知的存储指令功能码");
    }
}

uint64_t InstructionExecutor::loadFPFromMemory(std::shared_ptr<Memory> memory, uint64_t addr, Funct3 funct3) {
    switch (funct3) {
        case Funct3::LW:  // FLW
            return nanBoxSingle(memory->readWord(addr));
        case Funct3::LD:  // FLD
            return memory->read64(addr);
        default:
            throw IllegalInstructionException("未知的浮点加载指令功能码");
    }
}

void InstructionExecutor::storeFPToMemory(std::shared_ptr<Memory> memory, uint64_t addr, uint64_t value, Funct3 funct3) {
    switch (funct3) {
        case Funct3::SW:  // FSW
            memory->writeWord(addr, static_cast<uint32_t>(value));
            break;
        case Funct3::SD:  // FSD
            memory->write64(addr, value);
            break;
        default:
            throw IllegalInstructionException("未知的浮点存储指令功能码");
    }
}

uint64_t InstructionExecutor::executeUpperImmediate(const DecodedInstruction& inst, uint64_t pc) {
    if (inst.opcode == Opcode::LUI) {
        return static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
    } else if (inst.opcode == Opcode::AUIPC) {
        return pc + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
    } else {
        throw IllegalInstructionException("未知的上位立即数指令");
    }
}

// RV64I 32位立即数运算（W后缀）
uint64_t InstructionExecutor::executeImmediateOperation32(const DecodedInstruction& inst, uint64_t rs1_val) {
    int32_t rs1_32 = static_cast<int32_t>(rs1_val);
    int32_t result;
    
    switch (inst.funct3) {
        case Funct3::ADD_SUB:  // ADDIW
            result = rs1_32 + inst.imm;
            break;
            
        case Funct3::SLL:  // SLLIW - 使用imm的低5位作为移位量
            result = rs1_32 << (inst.imm & 0x1F);
            break;
            
        case Funct3::SRL_SRA:
            if (inst.funct7 == Funct7::SUB_SRA) {  // SRAIW
                result = rs1_32 >> (inst.imm & 0x1F);
            } else {  // SRLIW
                result = static_cast<int32_t>(static_cast<uint32_t>(rs1_32) >> (inst.imm & 0x1F));
            }
            break;
            
        default:
            throw IllegalInstructionException("未知的32位立即数指令功能码");
    }
    
    // 符号扩展到64位
    return static_cast<uint64_t>(static_cast<int64_t>(result));
}

// RV64I 32位寄存器运算（W后缀）
uint64_t InstructionExecutor::executeRegisterOperation32(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val) {
    int32_t rs1_32 = static_cast<int32_t>(rs1_val);
    int32_t rs2_32 = static_cast<int32_t>(rs2_val);
    int32_t result;
    
    switch (inst.funct3) {
        case Funct3::ADD_SUB:
            if (inst.funct7 == Funct7::SUB_SRA) {  // SUBW
                result = rs1_32 - rs2_32;
            } else {  // ADDW
                result = rs1_32 + rs2_32;
            }
            break;
            
        case Funct3::SLL:  // SLLW - 使用rs2的低5位作为移位量
            result = rs1_32 << (rs2_32 & 0x1F);
            break;
            
        case Funct3::SRL_SRA:
            if (inst.funct7 == Funct7::SUB_SRA) {  // SRAW
                result = rs1_32 >> (rs2_32 & 0x1F);
            } else {  // SRLW
                result = static_cast<int32_t>(static_cast<uint32_t>(rs1_32) >> (rs2_32 & 0x1F));
            }
            break;
            
        default:
            throw IllegalInstructionException("未知的32位寄存器指令功能码");
    }
    
    // 符号扩展到64位
    return static_cast<uint64_t>(static_cast<int64_t>(result));
}

uint64_t InstructionExecutor::executeMExtension(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val) {
    switch (inst.funct3) {
        case Funct3::MUL:
            return static_cast<uint64_t>(static_cast<int64_t>(rs1_val) * static_cast<int64_t>(rs2_val));
            
        case Funct3::MULH: {
            // TODO: 需要实现128位乘法以获取高位
            // 暂时使用简化实现
            __int128 result = static_cast<__int128>(static_cast<int64_t>(rs1_val)) * 
                            static_cast<__int128>(static_cast<int64_t>(rs2_val));
            return static_cast<uint64_t>(result >> 64);
        }
        
        case Funct3::MULHSU: {
            __int128 result = static_cast<__int128>(static_cast<int64_t>(rs1_val)) * 
                            static_cast<__int128>(rs2_val);
            return static_cast<uint64_t>(result >> 64);
        }
        
        case Funct3::MULHU: {
            unsigned __int128 result = static_cast<unsigned __int128>(rs1_val) * 
                                     static_cast<unsigned __int128>(rs2_val);
            return static_cast<uint64_t>(result >> 64);
        }
        
        case Funct3::DIV:
            if (rs2_val == 0) {
                return 0xFFFFFFFFFFFFFFFF;  // 除零结果
            }
            {
                const int64_t lhs = static_cast<int64_t>(rs1_val);
                const int64_t rhs = static_cast<int64_t>(rs2_val);
                if (lhs == std::numeric_limits<int64_t>::min() && rhs == -1) {
                    return static_cast<uint64_t>(lhs);  // 溢出场景按RISC-V规范返回被除数
                }
                return static_cast<uint64_t>(lhs / rhs);
            }
            
        case Funct3::DIVU:
            if (rs2_val == 0) {
                return 0xFFFFFFFFFFFFFFFF;  // 除零结果
            }
            return rs1_val / rs2_val;
            
        case Funct3::REM:
            if (rs2_val == 0) {
                return rs1_val;  // 除零情况下返回被除数
            }
            {
                const int64_t lhs = static_cast<int64_t>(rs1_val);
                const int64_t rhs = static_cast<int64_t>(rs2_val);
                if (lhs == std::numeric_limits<int64_t>::min() && rhs == -1) {
                    return 0;  // 溢出场景按RISC-V规范返回0
                }
                return static_cast<uint64_t>(lhs % rhs);
            }
            
        case Funct3::REMU:
            if (rs2_val == 0) {
                return rs1_val;  // 除零情况下返回被除数
            }
            return rs1_val % rs2_val;
            
        default:
            throw IllegalInstructionException("未知的M扩展指令功能码");
    }
}

uint64_t InstructionExecutor::executeMExtension32(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val) {
    const int32_t lhs = static_cast<int32_t>(rs1_val);
    const int32_t rhs = static_cast<int32_t>(rs2_val);
    const uint32_t lhs_u = static_cast<uint32_t>(rs1_val);
    const uint32_t rhs_u = static_cast<uint32_t>(rs2_val);
    int32_t result = 0;

    switch (inst.funct3) {
        case Funct3::MUL:
            result = static_cast<int32_t>(static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs));
            break;
        case Funct3::DIV:
            if (rhs == 0) {
                result = -1;
            } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                result = std::numeric_limits<int32_t>::min();
            } else {
                result = lhs / rhs;
            }
            break;
        case Funct3::DIVU:
            if (rhs_u == 0) {
                result = static_cast<int32_t>(0xFFFFFFFFU);
            } else {
                result = static_cast<int32_t>(lhs_u / rhs_u);
            }
            break;
        case Funct3::REM:
            if (rhs == 0) {
                result = lhs;
            } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                result = 0;
            } else {
                result = lhs % rhs;
            }
            break;
        case Funct3::REMU:
            if (rhs_u == 0) {
                result = static_cast<int32_t>(lhs_u);
            } else {
                result = static_cast<int32_t>(lhs_u % rhs_u);
            }
            break;
        default:
            throw IllegalInstructionException("未知的M扩展32位指令功能码");
    }

    return static_cast<uint64_t>(static_cast<int64_t>(result));
}

uint32_t InstructionExecutor::executeFPExtension(const DecodedInstruction& inst, float rs1_val, float rs2_val) {
    switch (inst.funct7) {
        case Funct7::FADD_S:
            return floatToUint32(rs1_val + rs2_val);
            
        case Funct7::FSUB_S:
            return floatToUint32(rs1_val - rs2_val);
            
        case Funct7::FMUL_S:
            return floatToUint32(rs1_val * rs2_val);
            
        case Funct7::FDIV_S:
            if (rs2_val == 0.0f) {
                return floatToUint32(rs1_val > 0 ? INFINITY : -INFINITY);
            }
            return floatToUint32(rs1_val / rs2_val);
            
        case Funct7::FEQ_S:
            return (rs1_val == rs2_val) ? 1 : 0;
            
        case Funct7::FLT_S:
            return (rs1_val < rs2_val) ? 1 : 0;
            
        case Funct7::FLE_S:
            return (rs1_val <= rs2_val) ? 1 : 0;
            
        case Funct7::FCVT_W_S:
            return static_cast<uint32_t>(static_cast<int32_t>(rs1_val));
            
        case Funct7::FCVT_WU_S:
            return static_cast<uint32_t>(rs1_val);
            
        case Funct7::FCVT_S_W:
            return floatToUint32(static_cast<float>(static_cast<int32_t>(rs1_val)));
            
        case Funct7::FCVT_S_WU:
            return floatToUint32(static_cast<float>(rs1_val));
            
        default:
            throw IllegalInstructionException("未知的F扩展指令功能码");
    }
}

InstructionExecutor::FpExecuteResult InstructionExecutor::executeFPOperation(
    const DecodedInstruction& inst, uint64_t rs1_bits_raw, uint64_t rs2_bits_raw, uint64_t rs1_int,
    uint8_t current_frm) {
    FpExecuteResult result{};
    const uint8_t rm = resolveRoundingMode(inst, current_frm);
    const uint8_t funct5 = fpFunct5(inst);
    const bool is_double = isDoubleFormat(inst);

    const uint32_t rs1_s_bits = unpackSingleOperand(rs1_bits_raw);
    const uint32_t rs2_s_bits = unpackSingleOperand(rs2_bits_raw);
    const float rs1_s = bitsToFloat(rs1_s_bits);
    const float rs2_s = bitsToFloat(rs2_s_bits);

    const uint64_t rs1_d_bits = rs1_bits_raw;
    const uint64_t rs2_d_bits = rs2_bits_raw;
    const double rs1_d = bitsToDouble(rs1_d_bits);
    const double rs2_d = bitsToDouble(rs2_d_bits);

    switch (funct5) {
        case 0x00: {  // FADD.S/FADD.D
            if (is_double) {
                double out = 0.0;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_d + rs2_d; });
                uint64_t bits = doubleToBits(out);
                result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
            } else {
                float out = 0.0f;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_s + rs2_s; });
                uint32_t bits = floatToBits(out);
                result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
            }
            result.write_fp_reg = true;
            return result;
        }
        case 0x01: {  // FSUB.S/FSUB.D
            if (is_double) {
                double out = 0.0;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_d - rs2_d; });
                uint64_t bits = doubleToBits(out);
                result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
            } else {
                float out = 0.0f;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_s - rs2_s; });
                uint32_t bits = floatToBits(out);
                result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
            }
            result.write_fp_reg = true;
            return result;
        }
        case 0x02: {  // FMUL.S/FMUL.D
            if (is_double) {
                double out = 0.0;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_d * rs2_d; });
                uint64_t bits = doubleToBits(out);
                result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
            } else {
                float out = 0.0f;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_s * rs2_s; });
                uint32_t bits = floatToBits(out);
                result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
            }
            result.write_fp_reg = true;
            return result;
        }
        case 0x03: {  // FDIV.S/FDIV.D
            if (is_double) {
                double out = 0.0;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_d / rs2_d; });
                uint64_t bits = doubleToBits(out);
                result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
            } else {
                float out = 0.0f;
                result.fflags = withFpEnv(rm, [&]() { out = rs1_s / rs2_s; });
                uint32_t bits = floatToBits(out);
                result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
            }
            result.write_fp_reg = true;
            return result;
        }
        default:
            break;
    }

    if (funct5 == 0x0B && inst.rs2 == 0) {  // FSQRT.S/FSQRT.D
        if (is_double) {
            double out = 0.0;
            result.fflags = withFpEnv(rm, [&]() { out = std::sqrt(rs1_d); });
            uint64_t bits = doubleToBits(out);
            result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
        } else {
            float out = 0.0f;
            result.fflags = withFpEnv(rm, [&]() { out = std::sqrt(rs1_s); });
            uint32_t bits = floatToBits(out);
            result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
        }
        result.write_fp_reg = true;
        return result;
    }

    if (funct5 == 0x05 &&
        (inst.funct3 == static_cast<Funct3>(0b000) || inst.funct3 == static_cast<Funct3>(0b001))) {  // FMIN/FMAX
        if (is_double) {
            const bool rs1_nan = isNaN64(rs1_d_bits);
            const bool rs2_nan = isNaN64(rs2_d_bits);
            if (isSignalingNaN64(rs1_d_bits) || isSignalingNaN64(rs2_d_bits)) {
                result.fflags |= kFFlagsNv;
            }

            if (rs1_nan && rs2_nan) {
                result.value = kCanonicalNaN64;
            } else if (rs1_nan) {
                result.value = rs2_d_bits;
            } else if (rs2_nan) {
                result.value = rs1_d_bits;
            } else if (inst.funct3 == static_cast<Funct3>(0b000)) {  // FMIN
                if (rs1_d == rs2_d && rs1_d == 0.0) {
                    result.value = (rs1_d_bits | rs2_d_bits);
                } else {
                    result.value = (rs1_d < rs2_d) ? rs1_d_bits : rs2_d_bits;
                }
            } else {
                if (rs1_d == rs2_d && rs1_d == 0.0) {
                    result.value = (rs1_d_bits & rs2_d_bits);
                } else {
                    result.value = (rs1_d > rs2_d) ? rs1_d_bits : rs2_d_bits;
                }
            }
        } else {
            const bool rs1_nan = isNaN32(rs1_s_bits);
            const bool rs2_nan = isNaN32(rs2_s_bits);
            if (isSignalingNaN32(rs1_s_bits) || isSignalingNaN32(rs2_s_bits)) {
                result.fflags |= kFFlagsNv;
            }

            if (rs1_nan && rs2_nan) {
                result.value = nanBoxSingle(kCanonicalNaN32);
            } else if (rs1_nan) {
                result.value = nanBoxSingle(rs2_s_bits);
            } else if (rs2_nan) {
                result.value = nanBoxSingle(rs1_s_bits);
            } else if (inst.funct3 == static_cast<Funct3>(0b000)) {  // FMIN
                if (rs1_s == rs2_s && rs1_s == 0.0f) {
                    result.value = nanBoxSingle(rs1_s_bits | rs2_s_bits);
                } else {
                    result.value = nanBoxSingle((rs1_s < rs2_s) ? rs1_s_bits : rs2_s_bits);
                }
            } else {
                if (rs1_s == rs2_s && rs1_s == 0.0f) {
                    result.value = nanBoxSingle(rs1_s_bits & rs2_s_bits);
                } else {
                    result.value = nanBoxSingle((rs1_s > rs2_s) ? rs1_s_bits : rs2_s_bits);
                }
            }
        }
        result.write_fp_reg = true;
        return result;
    }

    if (funct5 == 0x14) {  // FEQ/FLT/FLE
        const bool rs1_nan = is_double ? isNaN64(rs1_d_bits) : isNaN32(rs1_s_bits);
        const bool rs2_nan = is_double ? isNaN64(rs2_d_bits) : isNaN32(rs2_s_bits);
        switch (inst.funct3) {
            case Funct3::FEQ:
                if ((is_double && (isSignalingNaN64(rs1_d_bits) || isSignalingNaN64(rs2_d_bits))) ||
                    (!is_double && (isSignalingNaN32(rs1_s_bits) || isSignalingNaN32(rs2_s_bits)))) {
                    result.fflags |= kFFlagsNv;
                }
                if (is_double) {
                    result.value = (!rs1_nan && !rs2_nan && rs1_d == rs2_d) ? 1U : 0U;
                } else {
                    result.value = (!rs1_nan && !rs2_nan && rs1_s == rs2_s) ? 1U : 0U;
                }
                result.write_int_reg = true;
                return result;
            case Funct3::FLT:
            case Funct3::FLE:
                if (rs1_nan || rs2_nan) {
                    result.fflags |= kFFlagsNv;
                    result.value = 0;
                } else if (inst.funct3 == Funct3::FLT) {
                    result.value = is_double ? ((rs1_d < rs2_d) ? 1U : 0U) : ((rs1_s < rs2_s) ? 1U : 0U);
                } else {
                    result.value = is_double ? ((rs1_d <= rs2_d) ? 1U : 0U) : ((rs1_s <= rs2_s) ? 1U : 0U);
                }
                result.write_int_reg = true;
                return result;
            default:
                break;
        }
    }

    if (funct5 == 0x04) {  // FSGNJ.*
        if (is_double) {
            const uint64_t sign1 = rs1_d_bits & 0x8000000000000000ULL;
            const uint64_t sign2 = rs2_d_bits & 0x8000000000000000ULL;
            uint64_t sign = sign2;
            if (inst.funct3 == static_cast<Funct3>(0b001)) {
                sign = (~sign2) & 0x8000000000000000ULL;
            } else if (inst.funct3 == static_cast<Funct3>(0b010)) {
                sign = (sign1 ^ sign2) & 0x8000000000000000ULL;
            }
            result.value = (rs1_d_bits & 0x7FFFFFFFFFFFFFFFULL) | sign;
        } else {
            const uint32_t sign1 = rs1_s_bits & 0x80000000U;
            const uint32_t sign2 = rs2_s_bits & 0x80000000U;
            uint32_t sign = sign2;
            if (inst.funct3 == static_cast<Funct3>(0b001)) {
                sign = (~sign2) & 0x80000000U;
            } else if (inst.funct3 == static_cast<Funct3>(0b010)) {
                sign = (sign1 ^ sign2) & 0x80000000U;
            }
            result.value = nanBoxSingle((rs1_s_bits & 0x7FFFFFFFU) | sign);
        }
        result.write_fp_reg = true;
        return result;
    }

    if (funct5 == 0x18) {  // FCVT.*.{S/D} -> integer
        result.write_int_reg = true;

        const bool src_nan = is_double ? isNaN64(rs1_d_bits) : isNaN32(rs1_s_bits);
        const bool src_inf = is_double ? std::isinf(rs1_d) : std::isinf(rs1_s);
        const bool src_neg = is_double ? std::signbit(rs1_d) : std::signbit(rs1_s);
        const long double src_value = is_double ? static_cast<long double>(rs1_d)
                                                : static_cast<long double>(rs1_s);
        uint8_t flags = 0;

        auto convert_with_bounds = [&](long double min_v, long double max_v,
                                       uint64_t nan_value, uint64_t neg_overflow_value,
                                       uint64_t pos_overflow_value, bool signed_target) {
            if (src_nan) {
                flags |= kFFlagsNv;
                return nan_value;
            }
            if (src_inf) {
                flags |= kFFlagsNv;
                return src_neg ? neg_overflow_value : pos_overflow_value;
            }

            const long double rounded_ld = roundToModeLongDouble(src_value, rm, flags);
            if (rounded_ld < min_v) {
                flags |= kFFlagsNv;
                return neg_overflow_value;
            }
            if (rounded_ld > max_v) {
                flags |= kFFlagsNv;
                return pos_overflow_value;
            }

            if (signed_target) {
                return static_cast<uint64_t>(static_cast<int64_t>(rounded_ld));
            }
            return static_cast<uint64_t>(rounded_ld);
        };

        switch (inst.rs2) {
            case 0:  // FCVT.W.S
                result.value = signExtend32To64(static_cast<uint32_t>(convert_with_bounds(
                    static_cast<long double>(std::numeric_limits<int32_t>::min()),
                    static_cast<long double>(std::numeric_limits<int32_t>::max()),
                    static_cast<uint64_t>(std::numeric_limits<int32_t>::max()),
                    static_cast<uint64_t>(static_cast<int64_t>(std::numeric_limits<int32_t>::min())),
                    static_cast<uint64_t>(std::numeric_limits<int32_t>::max()),
                    true)));
                break;
            case 1:  // FCVT.WU.S
                result.value = signExtend32To64(static_cast<uint32_t>(convert_with_bounds(
                    0.0L,
                    static_cast<long double>(std::numeric_limits<uint32_t>::max()),
                    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()),
                    0,
                    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()),
                    false)));
                break;
            case 2:  // FCVT.L.S
                result.value = convert_with_bounds(
                    static_cast<long double>(std::numeric_limits<int64_t>::min()),
                    static_cast<long double>(std::numeric_limits<int64_t>::max()),
                    static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
                    static_cast<uint64_t>(std::numeric_limits<int64_t>::min()),
                    static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
                    true);
                break;
            case 3:  // FCVT.LU.S
                result.value = convert_with_bounds(
                    0.0L,
                    static_cast<long double>(std::numeric_limits<uint64_t>::max()),
                    std::numeric_limits<uint64_t>::max(),
                    0,
                    std::numeric_limits<uint64_t>::max(),
                    false);
                break;
            default:
                throw IllegalInstructionException("未知的FCVT到整数变体");
        }

        result.fflags = flags;
        return result;
    }

    if (funct5 == 0x1A) {  // FCVT.{S/D}.* from integer
        if (is_double) {
            double converted = 0.0;
            result.fflags = withFpEnv(rm, [&]() {
                switch (inst.rs2) {
                    case 0:  // FCVT.D.W
                        converted = static_cast<double>(static_cast<int32_t>(rs1_int));
                        break;
                    case 1:  // FCVT.D.WU
                        converted = static_cast<double>(static_cast<uint32_t>(rs1_int));
                        break;
                    case 2:  // FCVT.D.L
                        converted = static_cast<double>(static_cast<int64_t>(rs1_int));
                        break;
                    case 3:  // FCVT.D.LU
                        converted = static_cast<double>(rs1_int);
                        break;
                    default:
                        throw IllegalInstructionException("未知的FCVT到双精度变体");
                }
            });
            uint64_t bits = doubleToBits(converted);
            result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
        } else {
            float converted = 0.0f;
            result.fflags = withFpEnv(rm, [&]() {
                switch (inst.rs2) {
                    case 0:  // FCVT.S.W
                        converted = static_cast<float>(static_cast<int32_t>(rs1_int));
                        break;
                    case 1:  // FCVT.S.WU
                        converted = static_cast<float>(static_cast<uint32_t>(rs1_int));
                        break;
                    case 2:  // FCVT.S.L
                        converted = static_cast<float>(static_cast<int64_t>(rs1_int));
                        break;
                    case 3:  // FCVT.S.LU
                        converted = static_cast<float>(rs1_int);
                        break;
                    default:
                        throw IllegalInstructionException("未知的FCVT到单精度变体");
                }
            });
            uint32_t bits = floatToBits(converted);
            result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
        }
        result.write_fp_reg = true;
        return result;
    }

    if (funct5 == 0x08) {  // FCVT.{S/D}.{D/S}
        if (!is_double && inst.rs2 == 1) {  // FCVT.S.D
            float converted = 0.0f;
            result.fflags = withFpEnv(rm, [&]() { converted = static_cast<float>(rs1_d); });
            uint32_t bits = floatToBits(converted);
            result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
            result.write_fp_reg = true;
            return result;
        }
        if (is_double && inst.rs2 == 0) {  // FCVT.D.S
            double converted = 0.0;
            result.fflags = withFpEnv(rm, [&]() { converted = static_cast<double>(rs1_s); });
            uint64_t bits = doubleToBits(converted);
            result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
            result.write_fp_reg = true;
            return result;
        }
        throw IllegalInstructionException("未知的FCVT S/D互转变体");
    }

    if (funct5 == 0x1C && inst.rs2 == 0) {  // FMV.X.{W/D} / FCLASS.{S/D}
        if (inst.funct3 == static_cast<Funct3>(0b000)) {
            if (is_double) {
                result.value = rs1_d_bits;  // FMV.X.D
            } else {
                result.value = signExtend32To64(static_cast<uint32_t>(rs1_bits_raw));  // FMV.X.W
            }
            result.write_int_reg = true;
            return result;
        }
        if (inst.funct3 == static_cast<Funct3>(0b001)) {
            result.value = is_double ? classifyFloat64(rs1_d_bits) : classifyFloat32(rs1_s_bits);
            result.write_int_reg = true;
            return result;
        }
    }

    if (funct5 == 0x1E && inst.rs2 == 0 && inst.funct3 == static_cast<Funct3>(0b000)) {  // FMV.{W/D}.X
        result.value = is_double ? rs1_int : nanBoxSingle(static_cast<uint32_t>(rs1_int));
        result.write_fp_reg = true;
        return result;
    }

    throw IllegalInstructionException("未知的F扩展指令功能码");
}

InstructionExecutor::FpExecuteResult InstructionExecutor::executeFusedFPOperation(
    const DecodedInstruction& inst, uint64_t rs1_bits, uint64_t rs2_bits, uint64_t rs3_bits, uint8_t current_frm) {
    FpExecuteResult result{};
    const uint8_t rm = resolveRoundingMode(inst, current_frm);
    const bool is_double = isDoubleFormat(inst);

    if (is_double) {
        const double rs1 = bitsToDouble(rs1_bits);
        const double rs2 = bitsToDouble(rs2_bits);
        const double rs3 = bitsToDouble(rs3_bits);
        double out = 0.0;
        switch (inst.opcode) {
            case Opcode::FMADD:
                result.fflags = withFpEnv(rm, [&]() { out = std::fma(rs1, rs2, rs3); });
                break;
            case Opcode::FMSUB:
                result.fflags = withFpEnv(rm, [&]() { out = std::fma(rs1, rs2, -rs3); });
                break;
            case Opcode::FNMSUB:
                result.fflags = withFpEnv(rm, [&]() { out = std::fma(-rs1, rs2, rs3); });
                break;
            case Opcode::FNMADD:
                result.fflags = withFpEnv(rm, [&]() { out = -std::fma(rs1, rs2, rs3); });
                break;
            default:
                throw IllegalInstructionException("未知的浮点融合乘加指令");
        }
        uint64_t bits = doubleToBits(out);
        result.value = isNaN64(bits) ? kCanonicalNaN64 : bits;
    } else {
        const float rs1 = bitsToFloat(unpackSingleOperand(rs1_bits));
        const float rs2 = bitsToFloat(unpackSingleOperand(rs2_bits));
        const float rs3 = bitsToFloat(unpackSingleOperand(rs3_bits));
        float out = 0.0f;
        switch (inst.opcode) {
            case Opcode::FMADD:
                result.fflags = withFpEnv(rm, [&]() { out = std::fmaf(rs1, rs2, rs3); });
                break;
            case Opcode::FMSUB:
                result.fflags = withFpEnv(rm, [&]() { out = std::fmaf(rs1, rs2, -rs3); });
                break;
            case Opcode::FNMSUB:
                result.fflags = withFpEnv(rm, [&]() { out = std::fmaf(-rs1, rs2, rs3); });
                break;
            case Opcode::FNMADD:
                result.fflags = withFpEnv(rm, [&]() { out = -std::fmaf(rs1, rs2, rs3); });
                break;
            default:
                throw IllegalInstructionException("未知的浮点融合乘加指令");
        }
        uint32_t bits = floatToBits(out);
        result.value = nanBoxSingle(isNaN32(bits) ? kCanonicalNaN32 : bits);
    }

    result.write_fp_reg = true;
    return result;
}

InstructionExecutor::AtomicExecuteResult InstructionExecutor::executeAtomicOperation(
    const DecodedInstruction& inst, uint64_t memory_value, uint64_t rs2_value, bool reservation_hit) {
    AtomicExecuteResult result{};
    if (inst.opcode != Opcode::AMO) {
        throw IllegalInstructionException("非A扩展指令");
    }

    const uint32_t funct5 = atomicFunct5(inst);
    if (inst.funct3 != Funct3::LW && inst.funct3 != Funct3::LD) {
        throw IllegalInstructionException("A扩展仅支持W/D宽度");
    }

    const bool is_word = (inst.funct3 == Funct3::LW);
    const uint64_t old_value = is_word ? signExtend32To64(static_cast<uint32_t>(memory_value)) : memory_value;
    result.rd_value = old_value;

    const uint64_t rs2_width_value = is_word ? static_cast<uint64_t>(static_cast<uint32_t>(rs2_value)) : rs2_value;
    const uint64_t old_width_value = is_word ? static_cast<uint64_t>(static_cast<uint32_t>(memory_value)) : memory_value;

    const int64_t old_signed = is_word
        ? static_cast<int64_t>(static_cast<int32_t>(old_width_value))
        : static_cast<int64_t>(old_width_value);
    const int64_t rs2_signed = is_word
        ? static_cast<int64_t>(static_cast<int32_t>(rs2_width_value))
        : static_cast<int64_t>(rs2_width_value);

    switch (funct5) {
        case 0x02:  // LR.W/LR.D
            result.acquire_reservation = true;
            return result;
        case 0x03:  // SC.W/SC.D
            result.release_reservation = true;
            if (reservation_hit) {
                result.rd_value = 0;
                result.store_value = rs2_width_value;
                result.do_store = true;
            } else {
                result.rd_value = 1;
                result.do_store = false;
            }
            return result;
        case 0x01:  // AMOSWAP
            result.store_value = rs2_width_value;
            break;
        case 0x00:  // AMOADD
            result.store_value = old_width_value + rs2_width_value;
            break;
        case 0x04:  // AMOXOR
            result.store_value = old_width_value ^ rs2_width_value;
            break;
        case 0x0C:  // AMOAND
            result.store_value = old_width_value & rs2_width_value;
            break;
        case 0x08:  // AMOOR
            result.store_value = old_width_value | rs2_width_value;
            break;
        case 0x10:  // AMOMIN
            result.store_value = (old_signed < rs2_signed) ? old_width_value : rs2_width_value;
            break;
        case 0x14:  // AMOMAX
            result.store_value = (old_signed > rs2_signed) ? old_width_value : rs2_width_value;
            break;
        case 0x18:  // AMOMINU
            result.store_value = (old_width_value < rs2_width_value) ? old_width_value : rs2_width_value;
            break;
        case 0x1C:  // AMOMAXU
            result.store_value = (old_width_value > rs2_width_value) ? old_width_value : rs2_width_value;
            break;
        default:
            throw IllegalInstructionException("未知的A扩展funct5");
    }

    if (is_word) {
        result.store_value = static_cast<uint64_t>(static_cast<uint32_t>(result.store_value));
    }

    result.do_store = true;
    result.release_reservation = true;
    return result;
}

bool InstructionExecutor::isFPIntegerDestination(const DecodedInstruction& inst) {
    const uint8_t funct5 = fpFunct5(inst);
    if (funct5 == 0x14 &&
        (inst.funct3 == Funct3::FEQ || inst.funct3 == Funct3::FLT || inst.funct3 == Funct3::FLE)) {
        return true;  // FEQ/FLT/FLE
    }
    if (funct5 == 0x18) {
        return true;  // FCVT to integer
    }
    if (funct5 == 0x1C && inst.rs2 == 0 &&
        (inst.funct3 == static_cast<Funct3>(0b000) || inst.funct3 == static_cast<Funct3>(0b001))) {
        return true;  // FMV.X.{W/D} / FCLASS.{S/D}
    }
    return false;
}

bool InstructionExecutor::isFloatingPointInstruction(const DecodedInstruction& inst) {
    switch (inst.opcode) {
        case Opcode::OP_FP:
        case Opcode::LOAD_FP:
        case Opcode::STORE_FP:
        case Opcode::FMADD:
        case Opcode::FMSUB:
        case Opcode::FNMSUB:
        case Opcode::FNMADD:
            return true;
        default:
            return false;
    }
}

int32_t InstructionExecutor::signExtend(uint32_t value, int bits) {
    if (bits <= 0 || bits > 32) {
        return static_cast<int32_t>(value);
    }
    
    uint32_t mask = (1U << bits) - 1;
    uint32_t sign_bit = 1U << (bits - 1);
    
    value &= mask;
    if (value & sign_bit) {
        value |= ~mask;
    }
    
    return static_cast<int32_t>(value);
}

bool InstructionExecutor::isSystemCall(const DecodedInstruction& inst) {
    return inst.opcode == Opcode::SYSTEM && 
           inst.funct3 == Funct3::ECALL_EBREAK && 
           inst.imm == SystemInst::ECALL;
}

bool InstructionExecutor::isBreakpoint(const DecodedInstruction& inst) {
    return inst.opcode == Opcode::SYSTEM && 
           inst.funct3 == Funct3::ECALL_EBREAK && 
           inst.imm == SystemInst::EBREAK;
}

bool InstructionExecutor::isMachineReturn(const DecodedInstruction& inst) {
    return inst.opcode == Opcode::SYSTEM && 
           inst.funct3 == Funct3::ECALL_EBREAK && 
           inst.imm == SystemInst::MRET;
}

bool InstructionExecutor::isSupervisorReturn(const DecodedInstruction& inst) {
    return inst.opcode == Opcode::SYSTEM && 
           inst.funct3 == Funct3::ECALL_EBREAK && 
           inst.imm == SystemInst::SRET;
}

bool InstructionExecutor::isUserReturn(const DecodedInstruction& inst) {
    return inst.opcode == Opcode::SYSTEM && 
           inst.funct3 == Funct3::ECALL_EBREAK && 
           inst.imm == SystemInst::URET;
}

bool InstructionExecutor::isSfenceVma(const DecodedInstruction& inst) {
    return inst.opcode == Opcode::SYSTEM &&
           inst.funct3 == Funct3::ECALL_EBREAK &&
           inst.imm == 0x120;
}

bool InstructionExecutor::isTrapLikeSystemInstruction(const DecodedInstruction& inst) {
    return isSystemCall(inst) ||
           isBreakpoint(inst) ||
           isMachineReturn(inst) ||
           isSupervisorReturn(inst) ||
           isUserReturn(inst) ||
           isSfenceVma(inst);
}

bool InstructionExecutor::isCsrInstruction(const DecodedInstruction& inst) {
    if (inst.opcode != Opcode::SYSTEM) {
        return false;
    }

    switch (inst.funct3) {
        case static_cast<Funct3>(0b001):  // CSRRW
        case static_cast<Funct3>(0b010):  // CSRRS
        case static_cast<Funct3>(0b011):  // CSRRC
        case static_cast<Funct3>(0b101):  // CSRRWI
        case static_cast<Funct3>(0b110):  // CSRRSI
        case static_cast<Funct3>(0b111):  // CSRRCI
            return true;
        default:
            return false;
    }
}

InstructionExecutor::CsrExecuteResult InstructionExecutor::executeCsrInstruction(
    const DecodedInstruction& inst, uint64_t rs1_value, uint64_t current_csr_value) {
    if (!isCsrInstruction(inst)) {
        throw IllegalInstructionException("非CSR系统指令");
    }

    CsrExecuteResult result{current_csr_value, current_csr_value};
    const uint64_t zimm = static_cast<uint64_t>(inst.rs1 & 0x1F);

    switch (inst.funct3) {
        case static_cast<Funct3>(0b001):  // CSRRW
            result.write_value = rs1_value;
            break;
        case static_cast<Funct3>(0b010):  // CSRRS
            if (inst.rs1 != 0) {
                result.write_value = current_csr_value | rs1_value;
            }
            break;
        case static_cast<Funct3>(0b011):  // CSRRC
            if (inst.rs1 != 0) {
                result.write_value = current_csr_value & ~rs1_value;
            }
            break;
        case static_cast<Funct3>(0b101):  // CSRRWI
            result.write_value = zimm;
            break;
        case static_cast<Funct3>(0b110):  // CSRRSI
            if (inst.rs1 != 0) {
                result.write_value = current_csr_value | zimm;
            }
            break;
        case static_cast<Funct3>(0b111):  // CSRRCI
            if (inst.rs1 != 0) {
                result.write_value = current_csr_value & ~zimm;
            }
            break;
        default:
            throw IllegalInstructionException("未知CSR指令功能码");
    }

    return result;
}

// 私有辅助方法实现

uint64_t InstructionExecutor::performShiftOperation(uint64_t value, uint64_t shift_amount, Funct3 funct3, Funct7 funct7) {
    uint64_t shamt = shift_amount & 0x3F;  // RV64使用低6位
    
    switch (funct3) {
        case Funct3::SLL:
            return value << shamt;
            
        case Funct3::SRL_SRA:
            if (funct7 == Funct7::SUB_SRA) {
                return static_cast<uint64_t>(static_cast<int64_t>(value) >> shamt);
            } else {
                return value >> shamt;
            }
            
        default:
            throw IllegalInstructionException("未知的移位操作");
    }
}

// 32位移位操作（用于W后缀指令）
uint64_t InstructionExecutor::performShiftOperation32(uint64_t value, uint64_t shift_amount, Funct3 funct3, Funct7 funct7) {
    uint32_t shamt = shift_amount & 0x1F;  // W后缀指令使用低5位
    int32_t val32 = static_cast<int32_t>(value);
    int32_t result;
    
    switch (funct3) {
        case Funct3::SLL:
            result = val32 << shamt;
            break;
            
        case Funct3::SRL_SRA:
            if (funct7 == Funct7::SUB_SRA) {
                result = val32 >> shamt;
            } else {
                result = static_cast<int32_t>(static_cast<uint32_t>(val32) >> shamt);
            }
            break;
            
        default:
            throw IllegalInstructionException("未知的32位移位操作");
    }
    
    return static_cast<uint64_t>(static_cast<int64_t>(result));
}

uint64_t InstructionExecutor::performArithmeticOperation(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3, Funct7 funct7) {
    if (funct3 == Funct3::ADD_SUB) {
        if (funct7 == Funct7::SUB_SRA) {
            return rs1_val - rs2_val;
        } else {
            return rs1_val + rs2_val;
        }
    }
    throw IllegalInstructionException("未知的算术操作");
}

// 32位算术操作（用于W后缀指令）
uint64_t InstructionExecutor::performArithmeticOperation32(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3, Funct7 funct7) {
    int32_t rs1_32 = static_cast<int32_t>(rs1_val);
    int32_t rs2_32 = static_cast<int32_t>(rs2_val);
    int32_t result;
    
    if (funct3 == Funct3::ADD_SUB) {
        if (funct7 == Funct7::SUB_SRA) {
            result = rs1_32 - rs2_32;
        } else {
            result = rs1_32 + rs2_32;
        }
    } else {
        throw IllegalInstructionException("未知的32位算术操作");
    }
    
    return static_cast<uint64_t>(static_cast<int64_t>(result));
}

uint64_t InstructionExecutor::performLogicalOperation(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3) {
    switch (funct3) {
        case Funct3::XOR:
            return rs1_val ^ rs2_val;
            
        case Funct3::OR:
            return rs1_val | rs2_val;
            
        case Funct3::AND:
            return rs1_val & rs2_val;
            
        default:
            throw IllegalInstructionException("未知的逻辑操作");
    }
}

uint64_t InstructionExecutor::performComparisonOperation(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3) {
    switch (funct3) {
        case Funct3::SLT:
            return (static_cast<int64_t>(rs1_val) < static_cast<int64_t>(rs2_val)) ? 1 : 0;
            
        case Funct3::SLTU:
            return (rs1_val < rs2_val) ? 1 : 0;
            
        default:
            throw IllegalInstructionException("未知的比较操作");
    }
}

float InstructionExecutor::uint32ToFloat(uint32_t value) {
    union { uint32_t i; float f; } converter;
    converter.i = value;
    return converter.f;
}

uint32_t InstructionExecutor::floatToUint32(float value) {
    union { uint32_t i; float f; } converter;
    converter.f = value;
    return converter.i;
}

uint64_t InstructionExecutor::loadSignExtended(std::shared_ptr<Memory> memory, uint64_t addr, int bytes) {
    switch (bytes) {
        case 1:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(memory->readByte(addr))));
        case 2:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(memory->readHalfWord(addr))));
        case 4:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(memory->readWord(addr))));
        case 8:
            return memory->read64(addr);
        default:
            throw IllegalInstructionException("不支持的加载字节数");
    }
}

uint64_t InstructionExecutor::loadZeroExtended(std::shared_ptr<Memory> memory, uint64_t addr, int bytes) {
    switch (bytes) {
        case 1:
            return static_cast<uint64_t>(memory->readByte(addr));
        case 2:
            return static_cast<uint64_t>(memory->readHalfWord(addr));
        case 4:
            return static_cast<uint64_t>(memory->readWord(addr));
        case 8:
            return memory->read64(addr);
        default:
            throw IllegalInstructionException("不支持的加载字节数");
    }
}

} // namespace riscv
