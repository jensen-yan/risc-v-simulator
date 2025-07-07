#include "decoder.h"

namespace riscv {

DecodedInstruction Decoder::decode(Instruction instruction) const {
    // 临时实现，返回空的解码结果
    DecodedInstruction decoded;
    decoded.opcode = extractOpcode(instruction);
    decoded.type = determineType(decoded.opcode);
    
    // TODO: 完整实现指令解码
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

Funct3 Decoder::extractFunct3(Instruction inst) {
    return static_cast<Funct3>((inst >> 12) & 0x07);
}

Funct7 Decoder::extractFunct7(Instruction inst) {
    return static_cast<Funct7>((inst >> 25) & 0x7F);
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
            return InstructionType::R_TYPE;
        case Opcode::OP_IMM:
        case Opcode::LOAD:
        case Opcode::JALR:
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
        default:
            return InstructionType::UNKNOWN;
    }
}

void Decoder::validateInstruction(const DecodedInstruction& decoded) {
    // TODO: 实现指令验证
}

} // namespace riscv