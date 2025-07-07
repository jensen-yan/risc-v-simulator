#include "decoder.h"

namespace riscv {

DecodedInstruction Decoder::decode(Instruction instruction, uint32_t enabled_extensions) const {
    DecodedInstruction decoded;
    decoded.opcode = extractOpcode(instruction);
    decoded.type = determineType(decoded.opcode);
    decoded.funct3 = extractFunct3(instruction);
    decoded.funct7 = extractFunct7(instruction);
    decoded.rd = extractRd(instruction);
    decoded.rs1 = extractRs1(instruction);
    decoded.rs2 = extractRs2(instruction);
    decoded.rs3 = extractRs3(instruction);
    decoded.rm = extractRM(instruction);
    decoded.is_compressed = false;
    
    // 根据指令类型解码立即数
    switch (decoded.type) {
        case InstructionType::I_TYPE:
            decoded.imm = extractImmediateI(instruction);
            break;
        case InstructionType::S_TYPE:
            decoded.imm = extractImmediateS(instruction);
            break;
        case InstructionType::B_TYPE:
            decoded.imm = extractImmediateB(instruction);
            break;
        case InstructionType::U_TYPE:
            decoded.imm = extractImmediateU(instruction);
            break;
        case InstructionType::J_TYPE:
            decoded.imm = extractImmediateJ(instruction);
            break;
        default:
            decoded.imm = 0;
            break;
    }
    
    // 验证指令合法性
    validateInstruction(decoded, enabled_extensions);
    
    return decoded;
}

Opcode Decoder::extractOpcode(Instruction inst) {
    return static_cast<Opcode>(inst & 0x7F);
}

RegNum Decoder::extractRd(Instruction inst) {
    return static_cast<RegNum>((inst >> 7) & 0x1F);
}

RegNum Decoder::extractRs1(Instruction inst) {
    return static_cast<RegNum>((inst >> 15) & 0x1F);
}

RegNum Decoder::extractRs2(Instruction inst) {
    return static_cast<RegNum>((inst >> 20) & 0x1F);
}

RegNum Decoder::extractRs3(Instruction inst) {
    return static_cast<RegNum>((inst >> 27) & 0x1F);
}

Funct3 Decoder::extractFunct3(Instruction inst) {
    return static_cast<Funct3>((inst >> 12) & 0x07);
}

Funct7 Decoder::extractFunct7(Instruction inst) {
    return static_cast<Funct7>((inst >> 25) & 0x7F);
}

FPRoundingMode Decoder::extractRM(Instruction inst) {
    return static_cast<FPRoundingMode>((inst >> 12) & 0x07);
}

int32_t Decoder::extractImmediateI(Instruction inst) {
    int32_t imm = static_cast<int32_t>(inst) >> 20;
    return imm;
}

int32_t Decoder::extractImmediateS(Instruction inst) {
    int32_t imm = ((inst >> 7) & 0x1F) | ((inst >> 25) << 5);
    return (imm << 20) >> 20; // 符号扩展
}

int32_t Decoder::extractImmediateB(Instruction inst) {
    int32_t imm = ((inst >> 8) & 0x0F) << 1;
    imm |= ((inst >> 25) & 0x3F) << 5;
    imm |= ((inst >> 7) & 0x01) << 11;
    imm |= ((inst >> 31) & 0x01) << 12;
    return (imm << 19) >> 19; // 符号扩展
}

int32_t Decoder::extractImmediateU(Instruction inst) {
    return static_cast<int32_t>(inst & 0xFFFFF000);
}

int32_t Decoder::extractImmediateJ(Instruction inst) {
    int32_t imm = ((inst >> 21) & 0x3FF) << 1;
    imm |= ((inst >> 20) & 0x01) << 11;
    imm |= ((inst >> 12) & 0xFF) << 12;
    imm |= ((inst >> 31) & 0x01) << 20;
    return (imm << 11) >> 11; // 符号扩展
}

InstructionType Decoder::determineType(Opcode opcode) {
    switch (opcode) {
        case Opcode::OP:
        case Opcode::OP_FP:
            return InstructionType::R_TYPE;
        case Opcode::OP_IMM:
        case Opcode::LOAD:
        case Opcode::JALR:
        case Opcode::MISC_MEM:
            return InstructionType::I_TYPE;
        case Opcode::STORE:
            return InstructionType::S_TYPE;
        case Opcode::BRANCH:
            return InstructionType::B_TYPE;
        case Opcode::LUI:
        case Opcode::AUIPC:
            return InstructionType::U_TYPE;
        case Opcode::JAL:
            return InstructionType::J_TYPE;
        case Opcode::SYSTEM:
            return InstructionType::SYSTEM_TYPE;
        default:
            return InstructionType::UNKNOWN;
    }
}

void Decoder::validateInstruction(const DecodedInstruction& decoded, uint32_t enabled_extensions) {
    // 检查M扩展指令
    if (decoded.opcode == Opcode::OP && decoded.funct7 == Funct7::M_EXT) {
        if (!isExtensionEnabled(enabled_extensions, Extension::M)) {
            throw IllegalInstructionException("M扩展指令未启用");
        }
    }
    
    // 检查F扩展指令
    if (decoded.opcode == Opcode::OP_FP) {
        if (!isExtensionEnabled(enabled_extensions, Extension::F)) {
            throw IllegalInstructionException("F扩展指令未启用");
        }
    }
    
    // 检查寄存器范围
    if (decoded.rd >= 32 || decoded.rs1 >= 32 || decoded.rs2 >= 32 || decoded.rs3 >= 32) {
        throw IllegalInstructionException("寄存器编号超出范围");
    }
}

DecodedInstruction Decoder::decodeCompressed(uint16_t instruction, uint32_t enabled_extensions) const {
    if (!isExtensionEnabled(enabled_extensions, Extension::C)) {
        throw IllegalInstructionException("C扩展指令未启用");
    }
    
    DecodedInstruction decoded = expandCompressedInstruction(instruction);
    decoded.is_compressed = true;
    validateInstruction(decoded, enabled_extensions);
    return decoded;
}

bool Decoder::isCompressedInstruction(uint16_t instruction) {
    // 压缩指令的最低2位不是11
    return (instruction & 0x03) != 0x03;
}

DecodedInstruction Decoder::expandCompressedInstruction(uint16_t instruction) {
    DecodedInstruction decoded;
    uint8_t op = instruction & 0x03;
    uint8_t funct3 = (instruction >> 13) & 0x07;
    
    // 简化的压缩指令解码 - 仅实现几个常用指令
    switch (op) {
        case 0x01: // C.J, C.JAL等
            if (funct3 == 0x01) { // C.JAL
                decoded.opcode = Opcode::JAL;
                decoded.type = InstructionType::J_TYPE;
                decoded.rd = 1; // x1 (ra)
                // 提取立即数 (简化)
                decoded.imm = ((instruction >> 3) & 0x1F) << 1;
            }
            break;
        case 0x02: // C.LWSP, C.SWSP等
            if (funct3 == 0x02) { // C.LWSP
                decoded.opcode = Opcode::LOAD;
                decoded.type = InstructionType::I_TYPE;
                decoded.funct3 = Funct3::LW;
                decoded.rd = (instruction >> 7) & 0x1F;
                decoded.rs1 = 2; // x2 (sp)
                decoded.imm = ((instruction >> 2) & 0x3F) << 2;
            }
            break;
        default:
            throw IllegalInstructionException("不支持的压缩指令");
    }
    
    return decoded;
}

bool Decoder::isExtensionEnabled(uint32_t enabled_extensions, Extension ext) {
    return (enabled_extensions & static_cast<uint32_t>(ext)) != 0;
}

} // namespace riscv