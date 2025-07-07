#pragma once

#include "types.h"

namespace riscv {

/**
 * 指令解码器类
 * 负责将32位机器码解析为结构化的指令信息
 */
class Decoder {
public:
    Decoder() = default;
    ~Decoder() = default;
    
    /**
     * 解码指令
     * @param instruction 32位机器码指令
     * @return 解码后的指令结构
     * @throws IllegalInstructionException 当指令格式非法时
     */
    DecodedInstruction decode(Instruction instruction) const;
    
private:
    // 提取指令各个字段
    static Opcode extractOpcode(Instruction inst);
    static RegNum extractRd(Instruction inst);
    static RegNum extractRs1(Instruction inst);
    static RegNum extractRs2(Instruction inst);
    static Funct3 extractFunct3(Instruction inst);
    static Funct7 extractFunct7(Instruction inst);
    
    // 提取立即数（根据指令类型）
    static int32_t extractImmediateI(Instruction inst);
    static int32_t extractImmediateS(Instruction inst);
    static int32_t extractImmediateB(Instruction inst);
    static int32_t extractImmediateU(Instruction inst);
    static int32_t extractImmediateJ(Instruction inst);
    
    // 确定指令类型
    static InstructionType determineType(Opcode opcode);
    
    // 验证指令合法性
    static void validateInstruction(const DecodedInstruction& decoded);
};

} // namespace riscv