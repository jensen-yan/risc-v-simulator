#pragma once

#include "common/types.h"
#include "core/memory.h"
#include "core/decoder.h"
#include "common/cpu_interface.h"
#include <array>
#include <memory>

namespace riscv {

class SyscallHandler;

/**
 * RISC-V CPU 核心类
 * 管理寄存器状态，执行指令的主要逻辑
 */
class CPU : public ICpuInterface {
public:
    static constexpr size_t NUM_REGISTERS = 32;
    static constexpr size_t NUM_FP_REGISTERS = 32;
    static constexpr size_t NUM_CSR_REGISTERS = 4096;
    
    explicit CPU(std::shared_ptr<Memory> memory);
    ~CPU() override;
    
    // 禁用拷贝构造和赋值
    CPU(const CPU&) = delete;
    CPU& operator=(const CPU&) = delete;
    
    // 执行控制
    void step() override;                    // 单步执行
    void run() override;                     // 连续执行直到结束
    void reset() override;                   // 重置CPU状态
    
    // 寄存器访问
    uint64_t getRegister(RegNum reg) const override;
    void setRegister(RegNum reg, uint64_t value) override;
    
    // 浮点寄存器访问
    uint64_t getFPRegister(RegNum reg) const override;
    void setFPRegister(RegNum reg, uint64_t value) override;
    float getFPRegisterFloat(RegNum reg) const override;
    void setFPRegisterFloat(RegNum reg, float value) override;

    // CSR寄存器访问
    uint64_t getCSR(uint32_t addr) const override;
    void setCSR(uint32_t addr, uint64_t value) override;
    
    // 程序计数器
    uint64_t getPC() const override { return pc_; }
    void setPC(uint64_t pc) override { pc_ = pc; }
    
    // 状态查询
    bool isHalted() const override { return halted_; }
    uint64_t getInstructionCount() const override { return instruction_count_; }
    void requestHalt() override { halted_ = true; }
    
    // 扩展支持
    void setEnabledExtensions(uint32_t extensions) override { enabled_extensions_ = extensions; }
    uint32_t getEnabledExtensions() const override { return enabled_extensions_; }
    
    // 调试功能
    void dumpRegisters() const override;
    void dumpState() const override;
    
private:
    std::shared_ptr<Memory> memory_;
    Decoder decoder_;
    std::unique_ptr<SyscallHandler> syscall_handler_;
    
    // CPU 状态
    std::array<uint64_t, NUM_REGISTERS> registers_;
    std::array<uint32_t, NUM_FP_REGISTERS> fp_registers_;
    std::array<uint64_t, NUM_CSR_REGISTERS> csr_registers_;
    uint64_t pc_;                   // 程序计数器
    bool halted_;                   // 停机标志
    uint64_t instruction_count_;    // 指令计数器
    uint32_t enabled_extensions_;   // 启用的扩展
    bool last_instruction_compressed_; // 上一条指令是否为压缩指令
    bool reservation_valid_;        // LR/SC 预留标志
    uint64_t reservation_addr_;     // LR 预留地址
    
    // 指令执行方法
    void executeRType(const DecodedInstruction& inst);
    void executeIType(const DecodedInstruction& inst);
    void executeSType(const DecodedInstruction& inst);
    void executeBType(const DecodedInstruction& inst);
    void executeUType(const DecodedInstruction& inst);
    void executeJType(const DecodedInstruction& inst);
    void executeSystem(const DecodedInstruction& inst);
    
    // 扩展指令执行方法
    void executeMExtension(const DecodedInstruction& inst);
    void executeFPExtension(const DecodedInstruction& inst);
    void executeAtomicExtension(const DecodedInstruction& inst);
    
    // I-Type指令子方法
    void executeImmediateOperations(const DecodedInstruction& inst);
    void executeImmediateOperations32(const DecodedInstruction& inst);  // RV64I
    void executeLoadOperations(const DecodedInstruction& inst);
    void executeJALR(const DecodedInstruction& inst);
    
    // 系统调用处理
    void handleEcall();
    void handleEbreak();
    
    // 辅助方法
    void incrementPC() { 
        pc_ += last_instruction_compressed_ ? 2 : 4; 
    }
    int32_t signExtend(uint32_t value, int bits) const;
}; 

} // namespace riscv
