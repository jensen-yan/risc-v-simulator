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
    struct CsrExecuteResult {
        uint64_t read_value;
        uint64_t write_value;
    };

    struct FpExecuteResult {
        uint64_t value = 0;
        bool write_int_reg = false;
        bool write_fp_reg = false;
    };

    struct AtomicExecuteResult {
        uint64_t rd_value = 0;
        uint64_t store_value = 0;
        bool do_store = false;
        bool acquire_reservation = false;
        bool release_reservation = false;
    };

    // 基本算术运算
    static uint64_t executeImmediateOperation(const DecodedInstruction& inst, uint64_t rs1_val);
    static uint64_t executeRegisterOperation(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val);
    
    // 分支条件判断
    static bool evaluateBranchCondition(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val);
    
    // 跳转指令
    static uint64_t calculateJumpTarget(const DecodedInstruction& inst, uint64_t pc);
    static uint64_t calculateJumpAndLinkTarget(const DecodedInstruction& inst, uint64_t pc, uint64_t rs1_val);
    
    // 内存操作
    static uint64_t loadFromMemory(std::shared_ptr<Memory> memory, uint64_t addr, Funct3 funct3);
    static void storeToMemory(std::shared_ptr<Memory> memory, uint64_t addr, uint64_t value, Funct3 funct3);
    
    // 上位立即数指令
    static uint64_t executeUpperImmediate(const DecodedInstruction& inst, uint64_t pc);
    
    // M扩展指令（乘除法）
    static uint64_t executeMExtension(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val);
    static uint64_t executeMExtension32(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val);
    
    // F扩展指令（单精度浮点）
    static uint32_t executeFPExtension(const DecodedInstruction& inst, float rs1_val, float rs2_val);
    static FpExecuteResult executeFPOperation(const DecodedInstruction& inst, uint32_t rs1_bits,
                                              uint32_t rs2_bits, uint64_t rs1_int);
    static FpExecuteResult executeFusedFPOperation(const DecodedInstruction& inst, uint32_t rs1_bits,
                                                   uint32_t rs2_bits, uint32_t rs3_bits);
    static bool isFPIntegerDestination(const DecodedInstruction& inst);
    static bool isFloatingPointInstruction(const DecodedInstruction& inst);

    // 浮点访存（按位搬运）
    static uint64_t loadFPFromMemory(std::shared_ptr<Memory> memory, uint64_t addr, Funct3 funct3);
    static void storeFPToMemory(std::shared_ptr<Memory> memory, uint64_t addr, uint64_t value, Funct3 funct3);

    // A扩展
    static AtomicExecuteResult executeAtomicOperation(const DecodedInstruction& inst, uint64_t memory_value,
                                                      uint64_t rs2_value, bool reservation_hit);
    
    // RV64I 32位算术运算
    static uint64_t executeImmediateOperation32(const DecodedInstruction& inst, uint64_t rs1_val);
    static uint64_t executeRegisterOperation32(const DecodedInstruction& inst, uint64_t rs1_val, uint64_t rs2_val);
    
    // 辅助方法
    static int32_t signExtend(uint32_t value, int bits);
    
    // 系统调用辅助
    static bool isSystemCall(const DecodedInstruction& inst);
    static bool isBreakpoint(const DecodedInstruction& inst);
    static bool isMachineReturn(const DecodedInstruction& inst);
    static bool isSupervisorReturn(const DecodedInstruction& inst);
    static bool isUserReturn(const DecodedInstruction& inst);
    static bool isTrapLikeSystemInstruction(const DecodedInstruction& inst);
    static bool isCsrInstruction(const DecodedInstruction& inst);
    static CsrExecuteResult executeCsrInstruction(const DecodedInstruction& inst, uint64_t rs1_value,
                                                  uint64_t current_csr_value);
    
private:
    // 私有辅助方法
    static uint64_t performShiftOperation(uint64_t value, uint64_t shift_amount, Funct3 funct3, Funct7 funct7);
    static uint64_t performArithmeticOperation(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3, Funct7 funct7);
    static uint64_t performLogicalOperation(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3);
    static uint64_t performComparisonOperation(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3);
    
    // 32位运算辅助方法（用于RV64I的W后缀指令）
    static uint64_t performShiftOperation32(uint64_t value, uint64_t shift_amount, Funct3 funct3, Funct7 funct7);
    static uint64_t performArithmeticOperation32(uint64_t rs1_val, uint64_t rs2_val, Funct3 funct3, Funct7 funct7);
    
    // 浮点辅助方法
    static float uint32ToFloat(uint32_t value);
    static uint32_t floatToUint32(float value);
    
    // 内存访问辅助
    static uint64_t loadSignExtended(std::shared_ptr<Memory> memory, uint64_t addr, int bytes);
    static uint64_t loadZeroExtended(std::shared_ptr<Memory> memory, uint64_t addr, int bytes);
};

} // namespace riscv
