#include "core/instruction_executor.h"
#include "common/types.h"
#include <cmath>

namespace riscv {

uint32_t InstructionExecutor::executeImmediateOperation(const DecodedInstruction& inst, uint32_t rs1_val) {
    switch (inst.funct3) {
        case Funct3::ADD_SUB:
            return rs1_val + static_cast<uint32_t>(inst.imm);
            
        case Funct3::SLT:
            return (static_cast<int32_t>(rs1_val) < inst.imm) ? 1 : 0;
            
        case Funct3::SLTU:
            return (rs1_val < static_cast<uint32_t>(inst.imm)) ? 1 : 0;
            
        case Funct3::XOR:
            return rs1_val ^ static_cast<uint32_t>(inst.imm);
            
        case Funct3::OR:
            return rs1_val | static_cast<uint32_t>(inst.imm);
            
        case Funct3::AND:
            return rs1_val & static_cast<uint32_t>(inst.imm);
            
        case Funct3::SLL:
            return rs1_val << (inst.imm & 0x1F);
            
        case Funct3::SRL_SRA:
            if (inst.funct7 == Funct7::SUB_SRA) {
                // 算术右移
                return static_cast<uint32_t>(static_cast<int32_t>(rs1_val) >> (inst.imm & 0x1F));
            } else {
                // 逻辑右移
                return rs1_val >> (inst.imm & 0x1F);
            }
            
        default:
            throw IllegalInstructionException("未知的立即数指令功能码");
    }
}

uint32_t InstructionExecutor::executeRegisterOperation(const DecodedInstruction& inst, uint32_t rs1_val, uint32_t rs2_val) {
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

bool InstructionExecutor::evaluateBranchCondition(const DecodedInstruction& inst, uint32_t rs1_val, uint32_t rs2_val) {
    switch (inst.funct3) {
        case Funct3::BEQ:
            return rs1_val == rs2_val;
            
        case Funct3::BNE:
            return rs1_val != rs2_val;
            
        case Funct3::BLT:
            return static_cast<int32_t>(rs1_val) < static_cast<int32_t>(rs2_val);
            
        case Funct3::BGE:
            return static_cast<int32_t>(rs1_val) >= static_cast<int32_t>(rs2_val);
            
        case Funct3::BLTU:
            return rs1_val < rs2_val;
            
        case Funct3::BGEU:
            return rs1_val >= rs2_val;
            
        default:
            throw IllegalInstructionException("未知的分支指令功能码");
    }
}

uint32_t InstructionExecutor::calculateJumpTarget(const DecodedInstruction& inst, uint32_t pc) {
    return pc + static_cast<uint32_t>(inst.imm);
}

uint32_t InstructionExecutor::calculateJumpAndLinkTarget(const DecodedInstruction& inst, uint32_t pc, uint32_t rs1_val) {
    uint32_t target = rs1_val + static_cast<uint32_t>(inst.imm);
    return target & 0xFFFFFFFE;  // 清除最低位，确保地址对齐
}

uint32_t InstructionExecutor::loadFromMemory(std::shared_ptr<Memory> memory, uint32_t addr, Funct3 funct3) {
    switch (funct3) {
        case Funct3::LB:
            return loadSignExtended(memory, addr, 1);
            
        case Funct3::LH:
            return loadSignExtended(memory, addr, 2);
            
        case Funct3::LW:
            return memory->readWord(addr);
            
        case Funct3::LBU:
            return loadZeroExtended(memory, addr, 1);
            
        case Funct3::LHU:
            return loadZeroExtended(memory, addr, 2);
            
        default:
            throw IllegalInstructionException("未知的加载指令功能码");
    }
}

void InstructionExecutor::storeToMemory(std::shared_ptr<Memory> memory, uint32_t addr, uint32_t value, Funct3 funct3) {
    switch (funct3) {
        case Funct3::SB:
            memory->writeByte(addr, static_cast<uint8_t>(value));
            break;
            
        case Funct3::SH:
            memory->writeHalfWord(addr, static_cast<uint16_t>(value));
            break;
            
        case Funct3::SW:
            memory->writeWord(addr, value);
            break;
            
        default:
            throw IllegalInstructionException("未知的存储指令功能码");
    }
}

uint32_t InstructionExecutor::executeUpperImmediate(const DecodedInstruction& inst, uint32_t pc) {
    if (inst.opcode == Opcode::LUI) {
        return static_cast<uint32_t>(inst.imm);
    } else if (inst.opcode == Opcode::AUIPC) {
        return pc + static_cast<uint32_t>(inst.imm);
    } else {
        throw IllegalInstructionException("未知的上位立即数指令");
    }
}

uint32_t InstructionExecutor::executeMExtension(const DecodedInstruction& inst, uint32_t rs1_val, uint32_t rs2_val) {
    switch (inst.funct3) {
        case Funct3::MUL:
            return static_cast<uint32_t>(static_cast<int32_t>(rs1_val) * static_cast<int32_t>(rs2_val));
            
        case Funct3::MULH: {
            int64_t result = static_cast<int64_t>(static_cast<int32_t>(rs1_val)) * 
                           static_cast<int64_t>(static_cast<int32_t>(rs2_val));
            return static_cast<uint32_t>(result >> 32);
        }
        
        case Funct3::MULHSU: {
            int64_t result = static_cast<int64_t>(static_cast<int32_t>(rs1_val)) * 
                           static_cast<int64_t>(rs2_val);
            return static_cast<uint32_t>(result >> 32);
        }
        
        case Funct3::MULHU: {
            uint64_t result = static_cast<uint64_t>(rs1_val) * static_cast<uint64_t>(rs2_val);
            return static_cast<uint32_t>(result >> 32);
        }
        
        case Funct3::DIV:
            if (rs2_val == 0) {
                return 0xFFFFFFFF;  // 除零结果
            }
            return static_cast<uint32_t>(static_cast<int32_t>(rs1_val) / static_cast<int32_t>(rs2_val));
            
        case Funct3::DIVU:
            if (rs2_val == 0) {
                return 0xFFFFFFFF;  // 除零结果
            }
            return rs1_val / rs2_val;
            
        case Funct3::REM:
            if (rs2_val == 0) {
                return rs1_val;  // 除零情况下返回被除数
            }
            return static_cast<uint32_t>(static_cast<int32_t>(rs1_val) % static_cast<int32_t>(rs2_val));
            
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

uint32_t InstructionExecutor::performShiftOperation(uint32_t value, uint32_t shift_amount, Funct3 funct3, Funct7 funct7) {
    uint32_t shamt = shift_amount & 0x1F;  // 只使用低5位
    
    switch (funct3) {
        case Funct3::SLL:
            return value << shamt;
            
        case Funct3::SRL_SRA:
            if (funct7 == Funct7::SUB_SRA) {
                return static_cast<uint32_t>(static_cast<int32_t>(value) >> shamt);
            } else {
                return value >> shamt;
            }
            
        default:
            throw IllegalInstructionException("未知的移位操作");
    }
}

uint32_t InstructionExecutor::performArithmeticOperation(uint32_t rs1_val, uint32_t rs2_val, Funct3 funct3, Funct7 funct7) {
    if (funct3 == Funct3::ADD_SUB) {
        if (funct7 == Funct7::SUB_SRA) {
            return rs1_val - rs2_val;
        } else {
            return rs1_val + rs2_val;
        }
    }
    throw IllegalInstructionException("未知的算术操作");
}

uint32_t InstructionExecutor::performLogicalOperation(uint32_t rs1_val, uint32_t rs2_val, Funct3 funct3) {
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

uint32_t InstructionExecutor::performComparisonOperation(uint32_t rs1_val, uint32_t rs2_val, Funct3 funct3) {
    switch (funct3) {
        case Funct3::SLT:
            return (static_cast<int32_t>(rs1_val) < static_cast<int32_t>(rs2_val)) ? 1 : 0;
            
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

uint32_t InstructionExecutor::loadSignExtended(std::shared_ptr<Memory> memory, uint32_t addr, int bytes) {
    switch (bytes) {
        case 1:
            return static_cast<uint32_t>(static_cast<int8_t>(memory->readByte(addr)));
        case 2:
            return static_cast<uint32_t>(static_cast<int16_t>(memory->readHalfWord(addr)));
        case 4:
            return memory->readWord(addr);
        default:
            throw IllegalInstructionException("不支持的加载字节数");
    }
}

uint32_t InstructionExecutor::loadZeroExtended(std::shared_ptr<Memory> memory, uint32_t addr, int bytes) {
    switch (bytes) {
        case 1:
            return static_cast<uint32_t>(memory->readByte(addr));
        case 2:
            return static_cast<uint32_t>(memory->readHalfWord(addr));
        case 4:
            return memory->readWord(addr);
        default:
            throw IllegalInstructionException("不支持的加载字节数");
    }
}

} // namespace riscv