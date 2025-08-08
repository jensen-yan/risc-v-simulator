#include "core/instruction_executor.h"
#include "common/types.h"
#include <cmath>
#include <iostream>

namespace {
    // 检查单精度浮点数是否为signaling NaN
    bool isSignalingNaN(float value) {
        uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
        // IEEE 754: signaling NaN的指数部分全为1，尾数最高位为0，但尾数不全为0
        uint32_t exponent = (bits >> 23) & 0xFF;
        uint32_t mantissa = bits & 0x7FFFFF;
        return (exponent == 0xFF) && (mantissa != 0) && ((mantissa & 0x400000) == 0);
    }
}

namespace riscv {

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
            
        case Funct3::LW:  // 也用于 FLW (相同的 funct3 值)
            // 对于整数加载指令 LW：加载32位有符号数，符号扩展到64位
            // 对于浮点加载指令 FLW：加载32位浮点数，零扩展到64位
            // 由于在指令执行时我们无法区分LW和FLW，统一使用符号扩展
            // CPU层会根据指令类型正确处理结果
            return loadSignExtended(memory, addr, 4);
            
        case Funct3::LD:  // 也用于 FLD (相同的 funct3 值)
            // LD: 加载64位整数
            // FLD: 加载64位浮点数，作为位模式返回
            // 两者都可以用相同的加载逻辑
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
            
        case Funct3::SW:  // 也用于 FSW (相同的 funct3 值)
            // SW 和 FSW 使用相同的存储逻辑，都是存储32位数据
            memory->writeWord(addr, static_cast<uint32_t>(value));
            break;
            
        case Funct3::SD:  // 也用于 FSD (相同的 funct3 值)  
            // SD 和 FSD 使用相同的存储逻辑，都是存储64位数据
            memory->write64(addr, value);
            break;
            
        default:
            throw IllegalInstructionException("未知的存储指令功能码");
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
            return static_cast<uint64_t>(static_cast<int64_t>(rs1_val) / static_cast<int64_t>(rs2_val));
            
        case Funct3::DIVU:
            if (rs2_val == 0) {
                return 0xFFFFFFFFFFFFFFFF;  // 除零结果
            }
            return rs1_val / rs2_val;
            
        case Funct3::REM:
            if (rs2_val == 0) {
                return rs1_val;  // 除零情况下返回被除数
            }
            return static_cast<uint64_t>(static_cast<int64_t>(rs1_val) % static_cast<int64_t>(rs2_val));
            
        case Funct3::REMU:
            if (rs2_val == 0) {
                return rs1_val;  // 除零情况下返回被除数
            }
            return rs1_val % rs2_val;
            
        default:
            throw IllegalInstructionException("未知的M扩展指令功能码");
    }
}

uint32_t InstructionExecutor::executeFPExtension(const DecodedInstruction& inst, float rs1_val, float rs2_val) {
    switch (inst.funct7) {
        case Funct7::FADD_S: {
            // 使用long double进行计算以检测精度损失
            long double extended_result = static_cast<long double>(rs1_val) + static_cast<long double>(rs2_val);
            float result = rs1_val + rs2_val;
            
            return floatToUint32(result);
        }
            
        case Funct7::FSUB_S:
            return floatToUint32(rs1_val - rs2_val);
            
        case Funct7::FMUL_S:
            return floatToUint32(rs1_val * rs2_val);
            
        case Funct7::FDIV_S: {
            // 处理0/0和inf/inf等无效情形，以及除零
            bool rs1_zero = (rs1_val == 0.0f);
            bool rs2_zero = (rs2_val == 0.0f);
            bool rs1_inf = std::isinf(rs1_val);
            bool rs2_inf = std::isinf(rs2_val);
            if ((rs1_zero && rs2_zero) || (rs1_inf && rs2_inf)) {
                float qnan = std::numeric_limits<float>::quiet_NaN();
                return floatToUint32(qnan);
            }
            if (rs2_zero && !std::isnan(rs1_val)) {
                float inf = std::numeric_limits<float>::infinity();
                bool sign = std::signbit(rs1_val) ^ std::signbit(rs2_val);
                return floatToUint32(std::copysign(inf, sign ? -1.0f : 1.0f));
            }
            return floatToUint32(rs1_val / rs2_val);
        }
            
        case Funct7::FCMP_S:
            // 根据funct3来区分比较类型
            switch (inst.funct3) {
                case Funct3::FEQ: {
                    // FEQ.S指令：对于quiet NaN不产生异常，对于signaling NaN产生异常
                    uint32_t result;
                    bool has_snan = isSignalingNaN(rs1_val) || isSignalingNaN(rs2_val);
                    
                    if (std::isnan(rs1_val) || std::isnan(rs2_val)) {
                        result = 0;  // NaN与任何值比较都不相等
                        // 对于signaling NaN，需要在CPU层设置异常标志
                    } else {
                        result = (rs1_val == rs2_val) ? 1 : 0;
                    }
                    
                    
                    
                    return result;
                }
                case Funct3::FLT:
                    // FLT.S指令：NaN参与比较会产生Invalid Operation异常
                    if (std::isnan(rs1_val) || std::isnan(rs2_val)) {
                        // 这里需要在CPU层设置异常标志，暂时返回0
                        return 0;
                    }
                    return (rs1_val < rs2_val) ? 1 : 0;
                case Funct3::FLE:
                    // FLE.S指令：NaN参与比较会产生Invalid Operation异常
                    if (std::isnan(rs1_val) || std::isnan(rs2_val)) {
                        // 这里需要在CPU层设置异常标志，暂时返回0
                        return 0;
                    }
                    return (rs1_val <= rs2_val) ? 1 : 0;
                default:
                    throw IllegalInstructionException("未知的浮点比较指令");
            }
            
        case Funct7::FCVT_INT_S:
            // 根据rs2字段来区分转换类型
            switch (inst.rs2) {
                case 0:  // FCVT.W.S
                    return static_cast<uint32_t>(static_cast<int32_t>(rs1_val));
                case 1:  // FCVT.WU.S
                    return static_cast<uint32_t>(rs1_val);
                case 2:  // FCVT.L.S
                    return static_cast<uint64_t>(static_cast<int64_t>(rs1_val));
                case 3:  // FCVT.LU.S
                    return static_cast<uint64_t>(rs1_val);
                default:
                    throw IllegalInstructionException("未知的浮点转整数指令");
            }
            
        case Funct7::FCVT_S_INT:
            // 根据rs2字段来区分转换类型
            switch (inst.rs2) {
                case 0:  // FCVT.S.W
                    return floatToUint32(static_cast<float>(static_cast<int32_t>(rs1_val)));
                case 1:  // FCVT.S.WU
                    return floatToUint32(static_cast<float>(rs1_val));
                case 2:  // FCVT.S.L
                    return floatToUint32(static_cast<float>(static_cast<int64_t>(rs1_val)));
                case 3:  // FCVT.S.LU
                    return floatToUint32(static_cast<float>(rs1_val));
                default:
                    throw IllegalInstructionException("未知的整数转浮点指令");
            }
            
        case Funct7::FSQRT_S:
            return floatToUint32(std::sqrt(rs1_val));
            
        case Funct7::FMIN_FMAX_S:
            // 遵循RISC-V：任一为NaN，返回NaN；两者都为0且符号不同，FMIN返回负零，FMAX返回正零
            switch (inst.funct3) {
                case Funct3::FMIN: {
                    if (std::isnan(rs1_val) || std::isnan(rs2_val)) {
                        return floatToUint32(std::numeric_limits<float>::quiet_NaN());
                    }
                    if (rs1_val == 0.0f && rs2_val == 0.0f) {
                        // 返回 -0.0
                        return floatToUint32(-0.0f);
                    }
                    return floatToUint32(std::fmin(rs1_val, rs2_val));
                }
                case Funct3::FMAX: {
                    if (std::isnan(rs1_val) || std::isnan(rs2_val)) {
                        return floatToUint32(std::numeric_limits<float>::quiet_NaN());
                    }
                    if (rs1_val == 0.0f && rs2_val == 0.0f) {
                        // 返回 +0.0
                        return floatToUint32(0.0f);
                    }
                    return floatToUint32(std::fmax(rs1_val, rs2_val));
                }
                default:
                    throw IllegalInstructionException("未知的FMIN/FMAX指令");
            }
            
        case Funct7::FSGNJ_S:
            // 符号注入指令
            switch (inst.funct3) {
                case Funct3::FSGNJ: {
                    // 复制rs2的符号到rs1
                    uint32_t rs1_bits = floatToUint32(rs1_val);
                    uint32_t rs2_bits = floatToUint32(rs2_val);
                    uint32_t result = (rs1_bits & 0x7FFFFFFF) | (rs2_bits & 0x80000000);
                    return result;
                }
                case Funct3::FSGNJN: {
                    // 复制rs2的符号取反到rs1
                    uint32_t rs1_bits = floatToUint32(rs1_val);
                    uint32_t rs2_bits = floatToUint32(rs2_val);
                    uint32_t result = (rs1_bits & 0x7FFFFFFF) | ((~rs2_bits) & 0x80000000);
                    return result;
                }
                case Funct3::FSGNJX: {
                    // rs1和rs2的符号异或
                    uint32_t rs1_bits = floatToUint32(rs1_val);
                    uint32_t rs2_bits = floatToUint32(rs2_val);
                    uint32_t result = rs1_bits ^ (rs2_bits & 0x80000000);
                    return result;
                }
                default:
                    throw IllegalInstructionException("未知的FSGNJ指令");
            }
            
        case Funct7::FMV_X_W:  // 这个值与FCLASS_S相同，通过funct3区分
            // 根据funct3来区分FMV.X.W和FCLASS.S
            switch (inst.funct3) {
                case Funct3::FMV_CLASS:
                    // FMV.X.W: 将浮点寄存器的位模式移动到整数寄存器
                    return floatToUint32(rs1_val);
                case Funct3::FCLASS: {
                    // FCLASS.S: 分类浮点数
                    uint32_t bits = floatToUint32(rs1_val);
                    uint32_t result = 0;
                    
                    if (std::isnan(rs1_val)) {
                        if ((bits & 0x00400000) == 0) {
                            result = 1 << 9;  // 信号NaN（payload MSB 为0）
                        } else {
                            result = 1 << 8;  // 安静NaN（payload MSB 为1）
                        }
                    } else if (std::isinf(rs1_val)) {
                        if (rs1_val < 0) {
                            result = 1 << 0;  // 负无穷大
                        } else {
                            result = 1 << 7;  // 正无穷大
                        }
                    } else if (rs1_val == 0.0f) {
                        if (std::signbit(rs1_val)) {
                            result = 1 << 3;  // 负零
                        } else {
                            result = 1 << 4;  // 正零
                        }
                    } else if (std::isnormal(rs1_val)) {
                        if (rs1_val < 0) {
                            result = 1 << 1;  // 负正常数
                        } else {
                            result = 1 << 6;  // 正正常数
                        }
                    } else {
                        // 次正常数
                        if (rs1_val < 0) {
                            result = 1 << 2;  // 负次正常数
                        } else {
                            result = 1 << 5;  // 正次正常数
                        }
                    }
                    return result;
                }
                default:
                    throw IllegalInstructionException("未知的FMV.X.W/FCLASS.S指令");
            }
            
        case Funct7::FMV_W_X:
            // 根据funct3来区分
            switch (inst.funct3) {
                case Funct3::FMV_CLASS:
                    // FMV.W.X: 将整数寄存器的低32位位模式移动到浮点寄存器
                    // rs1_val实际上是从整数寄存器来的uint64_t值，需要取低32位
                    return static_cast<uint32_t>(reinterpret_cast<const uint64_t&>(rs1_val) & 0xFFFFFFFF);
                default:
                    throw IllegalInstructionException("未知的FMV.W.X指令");
            }
            
        default:
            throw IllegalInstructionException("未知的F扩展指令功能码");
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

double InstructionExecutor::uint64ToDouble(uint64_t value) {
    union { uint64_t i; double d; } converter;
    converter.i = value;
    return converter.d;
}

uint64_t InstructionExecutor::doubleToUint64(double value) {
    union { uint64_t i; double d; } converter;
    converter.d = value;
    return converter.i;
}

uint64_t InstructionExecutor::executeFPExtensionDouble(const DecodedInstruction& inst, double rs1_val, double rs2_val) {
    switch (inst.funct7) {
        case Funct7::FADD_D:
            return doubleToUint64(rs1_val + rs2_val);
            
        case Funct7::FSUB_D:
            return doubleToUint64(rs1_val - rs2_val);
            
        case Funct7::FMUL_D:
            return doubleToUint64(rs1_val * rs2_val);
            
        case Funct7::FDIV_D:
            if (rs2_val == 0.0) {
                return doubleToUint64(rs1_val > 0 ? INFINITY : -INFINITY);
            }
            return doubleToUint64(rs1_val / rs2_val);
            
        case Funct7::FCMP_D:
            // 根据funct3来区分比较类型
            switch (inst.funct3) {
                case Funct3::FEQ:
                    return (rs1_val == rs2_val) ? 1 : 0;
                case Funct3::FLT:
                    return (rs1_val < rs2_val) ? 1 : 0;
                case Funct3::FLE:
                    return (rs1_val <= rs2_val) ? 1 : 0;
                default:
                    throw IllegalInstructionException("未知的双精度浮点比较指令");
            }
            
        case Funct7::FCVT_INT_D:
            // 根据rs2字段来区分转换类型
            switch (inst.rs2) {
                case 0:  // FCVT.W.D
                    return static_cast<uint64_t>(static_cast<int32_t>(rs1_val));
                case 1:  // FCVT.WU.D
                    return static_cast<uint64_t>(static_cast<uint32_t>(rs1_val));
                case 2:  // FCVT.L.D
                    return static_cast<uint64_t>(static_cast<int64_t>(rs1_val));
                case 3:  // FCVT.LU.D
                    return static_cast<uint64_t>(rs1_val);
                default:
                    throw IllegalInstructionException("未知的双精度转整数指令");
            }
            
        case Funct7::FCVT_D_INT:
            // 根据rs2字段来区分转换类型
            switch (inst.rs2) {
                case 0:  // FCVT.D.W
                    return doubleToUint64(static_cast<double>(static_cast<int32_t>(rs1_val)));
                case 1:  // FCVT.D.WU
                    return doubleToUint64(static_cast<double>(static_cast<uint32_t>(rs1_val)));
                case 2:  // FCVT.D.L
                    return doubleToUint64(static_cast<double>(static_cast<int64_t>(rs1_val)));
                case 3:  // FCVT.D.LU
                    return doubleToUint64(static_cast<double>(rs1_val));
                default:
                    throw IllegalInstructionException("未知的整数转双精度指令");
            }
            
        case Funct7::FSQRT_D:
            return doubleToUint64(std::sqrt(rs1_val));
            
        case Funct7::FMIN_FMAX_D:
            // 根据funct3来区分
            switch (inst.funct3) {
                case Funct3::FMIN:
                    return doubleToUint64(std::fmin(rs1_val, rs2_val));
                case Funct3::FMAX:
                    return doubleToUint64(std::fmax(rs1_val, rs2_val));
                default:
                    throw IllegalInstructionException("未知的双精度FMIN/FMAX指令");
            }
            
        case Funct7::FSGNJ_D:
            // 符号注入指令
            switch (inst.funct3) {
                case Funct3::FSGNJ: {
                    // 复制rs2的符号到rs1
                    uint64_t rs1_bits = doubleToUint64(rs1_val);
                    uint64_t rs2_bits = doubleToUint64(rs2_val);
                    uint64_t result = (rs1_bits & 0x7FFFFFFFFFFFFFFF) | (rs2_bits & 0x8000000000000000);
                    return result;
                }
                case Funct3::FSGNJN: {
                    // 复制rs2的符号取反到rs1
                    uint64_t rs1_bits = doubleToUint64(rs1_val);
                    uint64_t rs2_bits = doubleToUint64(rs2_val);
                    uint64_t result = (rs1_bits & 0x7FFFFFFFFFFFFFFF) | ((~rs2_bits) & 0x8000000000000000);
                    return result;
                }
                case Funct3::FSGNJX: {
                    // rs1和rs2的符号异或
                    uint64_t rs1_bits = doubleToUint64(rs1_val);
                    uint64_t rs2_bits = doubleToUint64(rs2_val);
                    uint64_t result = rs1_bits ^ (rs2_bits & 0x8000000000000000);
                    return result;
                }
                default:
                    throw IllegalInstructionException("未知的双精度FSGNJ指令");
            }
            
        case Funct7::FMV_X_D:  // 这个值与FCLASS_D相同，通过funct3区分
            // 根据funct3来区分FMV.X.D和FCLASS.D
            switch (inst.funct3) {
                case Funct3::FMV_CLASS:
                    // FMV.X.D: 将双精度浮点寄存器的位模式移动到整数寄存器
                    return doubleToUint64(rs1_val);
                case Funct3::FCLASS: {
                    // FCLASS.D: 分类双精度浮点数
                    uint64_t bits = doubleToUint64(rs1_val);
                    uint64_t result = 0;
                    
                    if (std::isnan(rs1_val)) {
                        if ((bits & 0x0008000000000000) == 0) {
                            result = 1 << 8;  // 安静NaN
                        } else {
                            result = 1 << 9;  // 信号NaN
                        }
                    } else if (std::isinf(rs1_val)) {
                        if (rs1_val < 0) {
                            result = 1 << 0;  // 负无穷大
                        } else {
                            result = 1 << 7;  // 正无穷大
                        }
                    } else if (rs1_val == 0.0) {
                        if (std::signbit(rs1_val)) {
                            result = 1 << 3;  // 负零
                        } else {
                            result = 1 << 4;  // 正零
                        }
                    } else if (std::isnormal(rs1_val)) {
                        if (rs1_val < 0) {
                            result = 1 << 1;  // 负正常数
                        } else {
                            result = 1 << 6;  // 正正常数
                        }
                    } else {
                        // 次正常数
                        if (rs1_val < 0) {
                            result = 1 << 2;  // 负次正常数
                        } else {
                            result = 1 << 5;  // 正次正常数
                        }
                    }
                    return result;
                }
                default:
                    throw IllegalInstructionException("未知的FMV.X.D/FCLASS.D指令");
            }
            
        case Funct7::FMV_D_X:
            // 根据funct3来区分
            switch (inst.funct3) {
                case Funct3::FMV_CLASS:
                    // FMV.D.X: 将整数寄存器的位模式移动到双精度浮点寄存器
                    return static_cast<uint64_t>(reinterpret_cast<const uint64_t&>(rs1_val));
                default:
                    throw IllegalInstructionException("未知的FMV.D.X指令");
            }
            
        case Funct7::FCVT_S_D:
            // FCVT.S.D: 双精度转单精度
            return static_cast<uint64_t>(floatToUint32(static_cast<float>(rs1_val)));
            
        case Funct7::FCVT_D_S:
            // FCVT.D.S: 单精度转双精度
            return doubleToUint64(static_cast<double>(uint32ToFloat(static_cast<uint32_t>(rs1_val))));
            
        default:
            throw IllegalInstructionException("未知的D扩展指令功能码");
    }
}

uint32_t InstructionExecutor::executeFusedMultiplyAddSingle(const DecodedInstruction& inst, float rs1_val, float rs2_val, float rs3_val) {
    switch (inst.opcode) {
        case Opcode::FMADD:
            // FMADD.S: rs1 * rs2 + rs3
            return floatToUint32(std::fma(rs1_val, rs2_val, rs3_val));
            
        case Opcode::FMSUB:
            // FMSUB.S: rs1 * rs2 - rs3
            return floatToUint32(std::fma(rs1_val, rs2_val, -rs3_val));
            
        case Opcode::FNMSUB:
            // FNMSUB.S: -(rs1 * rs2) + rs3 = rs3 - rs1 * rs2
            return floatToUint32(std::fma(-rs1_val, rs2_val, rs3_val));
            
        case Opcode::FNMADD:
            // FNMADD.S: -(rs1 * rs2 + rs3)
            return floatToUint32(-std::fma(rs1_val, rs2_val, rs3_val));
            
        default:
            throw IllegalInstructionException("未知的融合乘加指令");
    }
}

uint64_t InstructionExecutor::executeFusedMultiplyAddDouble(const DecodedInstruction& inst, double rs1_val, double rs2_val, double rs3_val) {
    switch (inst.opcode) {
        case Opcode::FMADD:
            // FMADD.D: rs1 * rs2 + rs3
            return doubleToUint64(std::fma(rs1_val, rs2_val, rs3_val));
            
        case Opcode::FMSUB:
            // FMSUB.D: rs1 * rs2 - rs3
            return doubleToUint64(std::fma(rs1_val, rs2_val, -rs3_val));
            
        case Opcode::FNMSUB:
            // FNMSUB.D: -(rs1 * rs2) + rs3 = rs3 - rs1 * rs2
            return doubleToUint64(std::fma(-rs1_val, rs2_val, rs3_val));
            
        case Opcode::FNMADD:
            // FNMADD.D: -(rs1 * rs2 + rs3)
            return doubleToUint64(-std::fma(rs1_val, rs2_val, rs3_val));
            
        default:
            throw IllegalInstructionException("未知的双精度融合乘加指令");
    }
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

uint32_t InstructionExecutor::loadFloatFromMemory(std::shared_ptr<Memory> memory, uint64_t addr) {
    // FLW: 加载32位单精度浮点数，使用32位边界对齐
    if (addr % 4 != 0) {
        throw IllegalInstructionException("FLW地址未对齐到4字节边界");
    }
    return memory->readWord(addr);
}

uint64_t InstructionExecutor::loadDoubleFromMemory(std::shared_ptr<Memory> memory, uint64_t addr) {
    // FLD: 加载64位双精度浮点数，使用64位边界对齐
    if (addr % 8 != 0) {
        throw IllegalInstructionException("FLD地址未对齐到8字节边界");
    }
    return memory->read64(addr);
}

void InstructionExecutor::storeFloatToMemory(std::shared_ptr<Memory> memory, uint64_t addr, uint32_t value) {
    // FSW: 存储32位单精度浮点数，使用32位边界对齐
    if (addr % 4 != 0) {
        throw IllegalInstructionException("FSW地址未对齐到4字节边界");
    }
    memory->writeWord(addr, value);
}

void InstructionExecutor::storeDoubleToMemory(std::shared_ptr<Memory> memory, uint64_t addr, uint64_t value) {
    // FSD: 存储64位双精度浮点数，使用64位边界对齐
    if (addr % 8 != 0) {
        throw IllegalInstructionException("FSD地址未对齐到8字节边界");
    }
    memory->write64(addr, value);
}

} // namespace riscv