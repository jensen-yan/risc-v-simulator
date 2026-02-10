#pragma once

#include "common/cpu_interface.h"
#include "core/memory.h"
#include "system/elf_loader.h"
#include <string>
#include <memory>

// 前向声明
namespace riscv {
    class DiffTest;
}

namespace riscv {

/**
 * RISC-V 模拟器主类
 * 统一管理CPU和内存，提供外部接口
 */
class Simulator {
public:
    explicit Simulator(size_t memorySize = Memory::DEFAULT_SIZE, CpuType cpuType = CpuType::IN_ORDER);
    ~Simulator();
    
    // 禁用拷贝构造和赋值
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    
    // 程序管理
    bool loadProgram(const std::string& filename);
    bool loadProgramFromBytes(const std::vector<uint8_t>& program, Address startAddr = 0);
    bool loadRiscvProgram(const std::string& filename, Address loadAddr = 0x1000);
    bool loadElfProgram(const std::string& filename);
    void setHostCommAddresses(Address tohostAddr, Address fromhostAddr);
    
    // 执行控制
    void step();                    // 单步执行
    void run();                     // 运行到结束
    void reset();                   // 重置模拟器
    
    // 状态查询
    bool isHalted() const;
    uint64_t getInstructionCount() const;
    bool hasProgramExit() const;
    int getProgramExitCode() const;
    bool endedOnZeroInstruction() const;
    bool isHaltedByInstructionLimit() const { return halted_by_instruction_limit_; }
    bool isHaltedByCycleLimit() const { return halted_by_cycle_limit_; }
    
    // 调试功能
    void dumpRegisters() const;
    void dumpMemory(Address startAddr, size_t length) const;
    void dumpState() const;
    bool dumpSignature(const std::string& outputPath,
                       Address startAddr,
                       Address endAddr,
                       size_t granularity) const;
    
    // 统计信息
    void printStatistics() const;
    
    // 获取CPU类型
    CpuType getCpuType() const { return cpuType_; }
    
    // 获取底层CPU实例（用于特定功能）
    ICpuInterface* getCpu() { return cpu_.get(); }
    
private:
    // 主CPU内存和CPU
    std::shared_ptr<Memory> memory_;
    std::unique_ptr<ICpuInterface> cpu_;
    CpuType cpuType_;
    uint64_t cycle_count_ = 0;
    
    // 参考CPU内存和CPU（仅乱序CPU模式下使用）
    std::shared_ptr<Memory> reference_memory_;
    std::unique_ptr<ICpuInterface> reference_cpu_;
    
    // DiffTest组件（仅乱序CPU模式下使用）
    std::unique_ptr<DiffTest> difftest_;

    bool halted_by_instruction_limit_ = false;
    bool halted_by_cycle_limit_ = false;
    
    // 辅助方法
    std::vector<uint8_t> loadBinaryFile(const std::string& filename);

    static constexpr uint64_t kMaxInOrderInstructions = 5000000;
    static constexpr uint64_t kMaxOutOfOrderCycles = 10000;
};

} // namespace riscv
