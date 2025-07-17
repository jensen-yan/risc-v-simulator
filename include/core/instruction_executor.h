#pragma once

#include "core/decoder.h"
#include "core/memory.h"
#include <cstdint>
#include <memory>

namespace riscv {

/**
 * 指令执行引擎
 * 
 * 提供纯函数式的指令执行逻辑，消除顺序CPU和乱序CPU之间的代码重复
 * 所有方法都是静态的，不依赖于特定的CPU状态
 */
class InstructionExecutor {
public:
    // 基本算术运算
    static uint32_t executeImmediateOperation(const DecodedInstruction& inst, uint32_t rs1_val);
    static uint32_t executeRegisterOperation(const DecodedInstruction& inst, uint32_t rs1_val, uint32_t rs2_val);
    
    // 分支条件判断
    static bool evaluateBranchCondition(const DecodedInstruction& inst, uint32_t rs1_val, uint32_t rs2_val);
    
    // 跳转指令
    static uint32_t calculateJumpTarget(const DecodedInstruction& inst, uint32_t pc);
    static uint32_t calculateJumpAndLinkTarget(const DecodedInstruction& inst, uint32_t pc, uint32_t rs1_val);
    
    // 内存操作
    static uint32_t loadFromMemory(std::shared_ptr<Memory> memory, uint32_t addr, Funct3 funct3);
    static void storeToMemory(std::shared_ptr<Memory> memory, uint32_t addr, uint32_t value, Funct3 funct3);
    
    // 上位立即数指令
    static uint32_t executeUpperImmediate(const DecodedInstruction& inst, uint32_t pc);
    
    // M扩展指令（乘除法）
    static uint32_t executeMExtension(const DecodedInstruction& inst, uint32_t rs1_val, uint32_t rs2_val);
    
    // F扩展指令（单精度浮点）
    static uint32_t executeFPExtension(const DecodedInstruction& inst, float rs1_val, float rs2_val);
    
    // 辅助方法
    static int32_t signExtend(uint32_t value, int bits);
    
    // 系统调用辅助
    static bool isSystemCall(const DecodedInstruction& inst);
    static bool isBreakpoint(const DecodedInstruction& inst);
    static bool isMachineReturn(const DecodedInstruction& inst);
    static bool isSupervisorReturn(const DecodedInstruction& inst);
    static bool isUserReturn(const DecodedInstruction& inst);
    
private:
    // 私有辅助方法
    static uint32_t performShiftOperation(uint32_t value, uint32_t shift_amount, Funct3 funct3, Funct7 funct7);
    static uint32_t performArithmeticOperation(uint32_t rs1_val, uint32_t rs2_val, Funct3 funct3, Funct7 funct7);
    static uint32_t performLogicalOperation(uint32_t rs1_val, uint32_t rs2_val, Funct3 funct3);
    static uint32_t performComparisonOperation(uint32_t rs1_val, uint32_t rs2_val, Funct3 funct3);
    
    // 浮点辅助方法
    static float uint32ToFloat(uint32_t value);
    static uint32_t floatToUint32(float value);
    
    // 内存访问辅助
    static uint32_t loadSignExtended(std::shared_ptr<Memory> memory, uint32_t addr, int bytes);
    static uint32_t loadZeroExtended(std::shared_ptr<Memory> memory, uint32_t addr, int bytes);
};

} // namespace riscv