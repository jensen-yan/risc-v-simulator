#pragma once

#include "types.h"
#include "memory.h"
#include "decoder.h"
#include "alu.h"
#include <array>

namespace riscv {

/**
 * RISC-V CPU 核心类
 * 管理寄存器状态，执行指令的主要逻辑
 */
class CPU {
public:
    static constexpr size_t NUM_REGISTERS = 32;
    
    explicit CPU(std::shared_ptr<Memory> memory);
    ~CPU() = default;
    
    // 禁用拷贝构造和赋值
    CPU(const CPU&) = delete;
    CPU& operator=(const CPU&) = delete;
    
    // 执行控制
    void step();                    // 单步执行
    void run();                     // 连续执行直到结束
    void reset();                   // 重置CPU状态
    
    // 寄存器访问
    uint32_t getRegister(RegNum reg) const;
    void setRegister(RegNum reg, uint32_t value);
    
    // 程序计数器
    uint32_t getPC() const { return pc_; }
    void setPC(uint32_t pc) { pc_ = pc; }
    
    // 状态查询
    bool isHalted() const { return halted_; }
    uint64_t getInstructionCount() const { return instruction_count_; }
    
    // 调试功能
    void dumpRegisters() const;
    void dumpState() const;
    
private:
    std::shared_ptr<Memory> memory_;
    Decoder decoder_;
    
    // CPU 状态
    std::array<uint32_t, NUM_REGISTERS> registers_;
    uint32_t pc_;                   // 程序计数器
    bool halted_;                   // 停机标志
    uint64_t instruction_count_;    // 指令计数器
    
    // 指令执行方法
    void executeRType(const DecodedInstruction& inst);
    void executeIType(const DecodedInstruction& inst);
    void executeSType(const DecodedInstruction& inst);
    void executeBType(const DecodedInstruction& inst);
    void executeUType(const DecodedInstruction& inst);
    void executeJType(const DecodedInstruction& inst);
    void executeSystem(const DecodedInstruction& inst);
    
    // 内存访问辅助方法
    uint32_t loadFromMemory(Address addr, Funct3 funct3);
    void storeToMemory(Address addr, uint32_t value, Funct3 funct3);
    
    // 系统调用处理
    void handleEcall();
    void handleEbreak();
    
    // 辅助方法
    void incrementPC() { pc_ += 4; }
    int32_t signExtend(uint32_t value, int bits) const;
}; 

} // namespace riscv
