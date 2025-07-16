#include "core/decoder.h"

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
    
    switch (op) {
        case 0x00: // 压缩指令格式 Quadrant 0
            switch (funct3) {
                case 0x00: { // C.ADDI4SPN
                    if (instruction == 0) {
                        throw IllegalInstructionException("非法压缩指令: 全零");
                    }
                    decoded.opcode = Opcode::OP_IMM;
                    decoded.type = InstructionType::I_TYPE;
                    decoded.funct3 = Funct3::ADD_SUB;
                    decoded.rd = 8 + ((instruction >> 2) & 0x07); // x8-x15
                    decoded.rs1 = 2; // x2 (sp)
                    // 立即数：[5:4|9:6|2|3]，左移2位
                    uint32_t imm = ((instruction >> 7) & 0x30) | // [5:4]
                                   ((instruction >> 1) & 0x3C0) | // [9:6]
                                   ((instruction >> 4) & 0x4) |   // [2]
                                   ((instruction >> 2) & 0x8);    // [3]
                    decoded.imm = static_cast<int32_t>(imm);
                    break;
                }
                case 0x02: { // C.LW
                    decoded.opcode = Opcode::LOAD;
                    decoded.type = InstructionType::I_TYPE;
                    decoded.funct3 = Funct3::LW;
                    decoded.rd = 8 + ((instruction >> 2) & 0x07); // x8-x15
                    decoded.rs1 = 8 + ((instruction >> 7) & 0x07); // x8-x15
                    // 立即数计算：uimm[5:3|2|6]，按RISC-V手册
                    uint32_t uimm_5_3 = (instruction >> 10) & 0x7;  // bit[12:10] -> uimm[5:3]
                    uint32_t uimm_6 = (instruction >> 5) & 0x1;     // bit[5] -> uimm[6]
                    uint32_t uimm_2 = (instruction >> 6) & 0x1;     // bit[6] -> uimm[2]
                    uint32_t imm = (uimm_6 << 6) | (uimm_5_3 << 3) | (uimm_2 << 2);
                    decoded.imm = static_cast<int32_t>(imm);
                    break;
                }
                case 0x06: { // C.SW
                    decoded.opcode = Opcode::STORE;
                    decoded.type = InstructionType::S_TYPE;
                    decoded.funct3 = Funct3::SW;
                    decoded.rs2 = 8 + ((instruction >> 2) & 0x07); // x8-x15
                    decoded.rs1 = 8 + ((instruction >> 7) & 0x07); // x8-x15
                    // 立即数计算：uimm[5:3|2|6]，与C.LW相同
                    uint32_t uimm_5_3 = (instruction >> 10) & 0x7;  // bit[12:10] -> uimm[5:3]
                    uint32_t uimm_6 = (instruction >> 5) & 0x1;     // bit[5] -> uimm[6]
                    uint32_t uimm_2 = (instruction >> 6) & 0x1;     // bit[6] -> uimm[2]
                    uint32_t imm = (uimm_6 << 6) | (uimm_5_3 << 3) | (uimm_2 << 2);
                    decoded.imm = static_cast<int32_t>(imm);
                    break;
                }
                default:
                    throw IllegalInstructionException("不支持的压缩指令 Quadrant 0");
            }
            break;
            
        case 0x01: // 压缩指令格式 Quadrant 1
            switch (funct3) {
                case 0x00: { // C.ADDI
                    decoded.opcode = Opcode::OP_IMM;
                    decoded.type = InstructionType::I_TYPE;
                    decoded.funct3 = Funct3::ADD_SUB;
                    decoded.rd = (instruction >> 7) & 0x1F;
                    decoded.rs1 = decoded.rd;
                    // 立即数：[5] | [4:0]，符号扩展
                    int32_t imm = ((instruction >> 7) & 0x20) | ((instruction >> 2) & 0x1F);
                    decoded.imm = (imm & 0x20) ? (imm | 0xFFFFFFC0) : imm; // 符号扩展
                    break;
                }
                case 0x01: { // C.JAL (仅RV32C)
                    decoded.opcode = Opcode::JAL;
                    decoded.type = InstructionType::J_TYPE;
                    decoded.rd = 1; // x1 (ra)
                    // C.JAL立即数计算：[11|4|9:8|10|6|7|3:1|5]
                    // 从指令中提取各个位域
                    int32_t imm = 0;
                    imm |= ((instruction >> 12) & 0x1) << 11;  // [11]
                    imm |= ((instruction >> 8) & 0x1) << 10;   // [10]
                    imm |= ((instruction >> 9) & 0x3) << 8;    // [9:8]
                    imm |= ((instruction >> 6) & 0x1) << 7;    // [7]
                    imm |= ((instruction >> 7) & 0x1) << 6;    // [6]
                    imm |= ((instruction >> 2) & 0x1) << 5;    // [5]
                    imm |= ((instruction >> 11) & 0x1) << 4;   // [4]
                    imm |= ((instruction >> 3) & 0x7) << 1;    // [3:1]
                    // 符号扩展
                    decoded.imm = (imm & 0x800) ? (imm | 0xFFFFF000) : imm;
                    break;
                }
                case 0x02: { // C.LI
                    decoded.opcode = Opcode::OP_IMM;
                    decoded.type = InstructionType::I_TYPE;
                    decoded.funct3 = Funct3::ADD_SUB;
                    decoded.rd = (instruction >> 7) & 0x1F;
                    decoded.rs1 = 0; // x0
                    // 立即数：imm[5][12] | imm[4:0][6:2]，符号扩展
                    int32_t imm = (((instruction >> 12) & 0x1) << 5) | ((instruction >> 2) & 0x1F);
                    decoded.imm = (imm & 0x20) ? (imm | 0xFFFFFFC0) : imm; // 符号扩展
                    break;
                }
                case 0x03: { // C.ADDI16SP or C.LUI
                    uint8_t rd = (instruction >> 7) & 0x1F;
                    if (rd == 2) { // C.ADDI16SP (rd=sp)
                        decoded.opcode = Opcode::OP_IMM;
                        decoded.type = InstructionType::I_TYPE;
                        decoded.funct3 = Funct3::ADD_SUB;
                        decoded.rd = 2; // x2 (sp)
                        decoded.rs1 = 2; // x2 (sp)
                        // 立即数：imm[9|4|6|8:7|5]，按位重构然后符号扩展
                        int32_t imm_9 = (instruction >> 12) & 0x1;    // bit[12] -> imm[9]
                        int32_t imm_4 = (instruction >> 6) & 0x1;     // bit[6] -> imm[4]
                        int32_t imm_6 = (instruction >> 5) & 0x1;     // bit[5] -> imm[6]
                        int32_t imm_8_7 = (instruction >> 3) & 0x3;   // bit[4:3] -> imm[8:7]
                        int32_t imm_5 = (instruction >> 2) & 0x1;     // bit[2] -> imm[5]
                        int32_t imm = (imm_9 << 9) | (imm_8_7 << 7) | (imm_6 << 6) | (imm_5 << 5) | (imm_4 << 4);
                        decoded.imm = (imm & 0x200) ? (imm | 0xFFFFFE00) : imm; // 符号扩展
                    } else { // C.LUI
                        if (rd == 0) {
                            throw IllegalInstructionException("C.LUI with rd=x0 is illegal");
                        }
                        // 检查立即数是否为0（C.LUI不允许立即数为0）
                        int32_t imm_17 = (instruction >> 12) & 0x1;     // bit[12] -> imm[17]
                        int32_t imm_16_12 = (instruction >> 2) & 0x1F;  // bit[6:2] -> imm[16:12]
                        if (imm_17 == 0 && imm_16_12 == 0) {
                            throw IllegalInstructionException("C.LUI with imm=0 is illegal");
                        }
                        decoded.opcode = Opcode::LUI;
                        decoded.type = InstructionType::U_TYPE;
                        decoded.rd = rd;
                        // C.LUI立即数：imm[17][16:12]，左移12位，符号扩展
                        int32_t imm = (imm_17 << 17) | (imm_16_12 << 12);
                        // 符号扩展imm[17:12]到32位
                        decoded.imm = (imm & 0x20000) ? (imm | 0xFFFC0000) : imm;
                    }
                    break;
                }
                case 0x04: { // C.SRLI, C.SRAI, C.ANDI, C.SUB, C.XOR, C.OR, C.AND
                    uint8_t funct2 = (instruction >> 10) & 0x03;
                    uint8_t rs1_rd = 8 + ((instruction >> 7) & 0x07); // x8-x15
                    
                    if (funct2 == 0x00) { // C.SRLI
                        decoded.opcode = Opcode::OP_IMM;
                        decoded.type = InstructionType::I_TYPE;
                        decoded.funct3 = Funct3::SRL_SRA;
                        decoded.funct7 = Funct7::NORMAL;
                        decoded.rd = rs1_rd;
                        decoded.rs1 = rs1_rd;
                        // 移位量：shamt[5][4:0]
                        uint32_t shamt = ((instruction >> 7) & 0x20) | ((instruction >> 2) & 0x1F);
                        decoded.imm = static_cast<int32_t>(shamt);
                    } else if (funct2 == 0x01) { // C.SRAI
                        decoded.opcode = Opcode::OP_IMM;
                        decoded.type = InstructionType::I_TYPE;
                        decoded.funct3 = Funct3::SRL_SRA;
                        decoded.funct7 = Funct7::SUB_SRA;
                        decoded.rd = rs1_rd;
                        decoded.rs1 = rs1_rd;
                        // 移位量：shamt[5][4:0]
                        uint32_t shamt = ((instruction >> 7) & 0x20) | ((instruction >> 2) & 0x1F);
                        decoded.imm = static_cast<int32_t>(shamt);
                    } else if (funct2 == 0x02) { // C.ANDI
                        decoded.opcode = Opcode::OP_IMM;
                        decoded.type = InstructionType::I_TYPE;
                        decoded.funct3 = Funct3::AND;
                        decoded.rd = rs1_rd;
                        decoded.rs1 = rs1_rd;
                        // 立即数：imm[5][4:0]，符号扩展
                        int32_t imm = ((instruction >> 7) & 0x20) | ((instruction >> 2) & 0x1F);
                        decoded.imm = (imm & 0x20) ? (imm | 0xFFFFFFC0) : imm;
                    } else { // funct2 == 0x03: C.SUB, C.XOR, C.OR, C.AND
                        uint8_t rs2 = 8 + ((instruction >> 2) & 0x07); // x8-x15
                        uint8_t funct2_low = (instruction >> 5) & 0x03; // 位[6:5]
                        
                        decoded.opcode = Opcode::OP;
                        decoded.type = InstructionType::R_TYPE;
                        decoded.rd = rs1_rd;
                        decoded.rs1 = rs1_rd;
                        decoded.rs2 = rs2;
                        
                        switch (funct2_low) {
                            case 0x00: // C.SUB (100011 rd' 00 rs2' 01)
                                decoded.funct3 = Funct3::ADD_SUB;
                                decoded.funct7 = Funct7::SUB_SRA;
                                break;
                            case 0x01: // C.XOR (100011 rd' 01 rs2' 01)
                                decoded.funct3 = Funct3::XOR;
                                decoded.funct7 = Funct7::NORMAL;
                                break;
                            case 0x02: // C.OR (100011 rd' 10 rs2' 01)
                                decoded.funct3 = Funct3::OR;
                                decoded.funct7 = Funct7::NORMAL;
                                break;
                            case 0x03: // C.AND (100011 rd' 11 rs2' 01)
                                decoded.funct3 = Funct3::AND;
                                decoded.funct7 = Funct7::NORMAL;
                                break;
                        }
                    }
                    break;
                }
                case 0x05: { // C.J
                    decoded.opcode = Opcode::JAL;
                    decoded.type = InstructionType::J_TYPE;
                    decoded.rd = 0; // x0 (不保存返回地址)
                    // C.J立即数计算与C.JAL相同
                    int32_t imm = 0;
                    imm |= ((instruction >> 12) & 0x1) << 11;  // [11]
                    imm |= ((instruction >> 8) & 0x1) << 10;   // [10]
                    imm |= ((instruction >> 9) & 0x3) << 8;    // [9:8]
                    imm |= ((instruction >> 6) & 0x1) << 7;    // [7]
                    imm |= ((instruction >> 7) & 0x1) << 6;    // [6]
                    imm |= ((instruction >> 2) & 0x1) << 5;    // [5]
                    imm |= ((instruction >> 11) & 0x1) << 4;   // [4]
                    imm |= ((instruction >> 3) & 0x7) << 1;    // [3:1]
                    decoded.imm = (imm & 0x800) ? (imm | 0xFFFFF000) : imm;
                    break;
                }
                case 0x06: { // C.BEQZ
                    decoded.opcode = Opcode::BRANCH;
                    decoded.type = InstructionType::B_TYPE;
                    decoded.funct3 = Funct3::BEQ;
                    decoded.rs1 = 8 + ((instruction >> 7) & 0x07); // x8-x15
                    decoded.rs2 = 0; // x0
                    // 立即数：[8|4:3|7:6|2:1|5]
                    int32_t imm = ((instruction >> 4) & 0x100) | // [8]
                                  ((instruction << 1) & 0xC0) |  // [7:6]
                                  ((instruction << 3) & 0x20) |  // [5]
                                  ((instruction >> 7) & 0x18) |  // [4:3]
                                  ((instruction >> 2) & 0x06);   // [2:1]
                    decoded.imm = (imm & 0x100) ? (imm | 0xFFFFFE00) : imm;
                    break;
                }
                case 0x07: { // C.BNEZ
                    decoded.opcode = Opcode::BRANCH;
                    decoded.type = InstructionType::B_TYPE;
                    decoded.funct3 = Funct3::BNE;
                    decoded.rs1 = 8 + ((instruction >> 7) & 0x07); // x8-x15
                    decoded.rs2 = 0; // x0
                    // 立即数计算与C.BEQZ相同
                    int32_t imm = ((instruction >> 4) & 0x100) |
                                  ((instruction << 1) & 0xC0) |
                                  ((instruction << 3) & 0x20) |
                                  ((instruction >> 7) & 0x18) |
                                  ((instruction >> 2) & 0x06);
                    decoded.imm = (imm & 0x100) ? (imm | 0xFFFFFE00) : imm;
                    break;
                }
                default:
                    throw IllegalInstructionException("不支持的压缩指令 Quadrant 1");
            }
            break;
            
        case 0x02: // 压缩指令格式 Quadrant 2
            switch (funct3) {
                case 0x00: { // C.SLLI
                    decoded.opcode = Opcode::OP_IMM;
                    decoded.type = InstructionType::I_TYPE;
                    decoded.funct3 = Funct3::SLL;
                    decoded.rd = (instruction >> 7) & 0x1F;
                    decoded.rs1 = decoded.rd;
                    decoded.imm = ((instruction >> 7) & 0x20) | ((instruction >> 2) & 0x1F);
                    break;
                }
                case 0x02: { // C.LWSP
                    decoded.opcode = Opcode::LOAD;
                    decoded.type = InstructionType::I_TYPE;
                    decoded.funct3 = Funct3::LW;
                    decoded.rd = (instruction >> 7) & 0x1F;
                    decoded.rs1 = 2; // x2 (sp)
                    // 立即数：[5][4:2|7:6]，左移2位
                    uint32_t imm = ((instruction >> 7) & 0x20) | 
                                   ((instruction >> 2) & 0x1C) | 
                                   ((instruction << 4) & 0xC0);
                    decoded.imm = imm;
                    break;
                }
                case 0x06: { // C.SWSP
                    decoded.opcode = Opcode::STORE;
                    decoded.type = InstructionType::S_TYPE;
                    decoded.funct3 = Funct3::SW;
                    decoded.rs2 = (instruction >> 2) & 0x1F;
                    decoded.rs1 = 2; // x2 (sp)
                    // 立即数：uimm[5:2|7:6]
                    uint32_t uimm_5_2 = (instruction >> 9) & 0xF;  // bit[12:9] -> uimm[5:2]
                    uint32_t uimm_7_6 = (instruction >> 7) & 0x3;  // bit[8:7] -> uimm[7:6]
                    uint32_t imm = (uimm_7_6 << 6) | (uimm_5_2 << 2);
                    decoded.imm = imm;
                    break;
                }
                case 0x04: { // C.JR, C.MV, C.JALR, C.ADD, C.EBREAK
                    uint8_t bit12 = (instruction >> 12) & 0x01;
                    uint8_t rs1 = (instruction >> 7) & 0x1F;
                    uint8_t rs2 = (instruction >> 2) & 0x1F;
                    
                    if (bit12 == 0) {
                        if (rs2 == 0) { // C.JR
                            if (rs1 == 0) {
                                throw IllegalInstructionException("C.JR with rs1=x0 is illegal");
                            }
                            decoded.opcode = Opcode::JALR;
                            decoded.type = InstructionType::I_TYPE;
                            decoded.funct3 = Funct3::ADD_SUB;
                            decoded.rd = 0; // x0 (不保存返回地址)
                            decoded.rs1 = rs1;
                            decoded.imm = 0;
                        } else { // C.MV
                            // C.MV扩展为: ADD rd, x0, rs2
                            decoded.opcode = Opcode::OP;
                            decoded.type = InstructionType::R_TYPE;
                            decoded.funct3 = Funct3::ADD_SUB;
                            decoded.funct7 = Funct7::NORMAL;
                            decoded.rd = rs1;
                            decoded.rs1 = 0; // x0 (always zero)
                            decoded.rs2 = rs2;
                        }
                    } else {
                        if (rs1 == 0 && rs2 == 0) { // C.EBREAK
                            decoded.opcode = Opcode::SYSTEM;
                            decoded.type = InstructionType::SYSTEM_TYPE;
                            decoded.funct3 = Funct3::ADD_SUB;
                            decoded.imm = 1; // EBREAK
                        } else if (rs2 == 0) { // C.JALR
                            if (rs1 == 0) {
                                throw IllegalInstructionException("C.JALR with rs1=x0 is illegal");
                            }
                            decoded.opcode = Opcode::JALR;
                            decoded.type = InstructionType::I_TYPE;
                            decoded.funct3 = Funct3::ADD_SUB;
                            decoded.rd = 1; // x1 (保存返回地址)
                            decoded.rs1 = rs1;
                            decoded.imm = 0;
                        } else { // C.ADD
                            decoded.opcode = Opcode::OP;
                            decoded.type = InstructionType::R_TYPE;
                            decoded.funct3 = Funct3::ADD_SUB;
                            decoded.funct7 = Funct7::NORMAL;
                            decoded.rd = rs1;
                            decoded.rs1 = rs1;
                            decoded.rs2 = rs2;
                        }
                    }
                    break;
                }
                default:
                    throw IllegalInstructionException("不支持的压缩指令 Quadrant 2");
            }
            break;
            
        default:
            throw IllegalInstructionException("无效的压缩指令操作码");
    }
    
    return decoded;
}

bool Decoder::isExtensionEnabled(uint32_t enabled_extensions, Extension ext) {
    return (enabled_extensions & static_cast<uint32_t>(ext)) != 0;
}

} // namespace riscv