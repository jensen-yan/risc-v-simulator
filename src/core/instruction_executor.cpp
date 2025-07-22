#include "core/instruction_executor.h"
#include "common/types.h"
#include <cmath>

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