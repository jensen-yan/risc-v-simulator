#pragma once

#include "common/cpu_interface.h"
#include "core/memory.h"
#include "system/checkpoint_types.h"
#include "system/elf_loader.h"
#include "system/pipeline_tracer.h"

#include <functional>
#include <memory>
#include <string>

// 前向声明
namespace riscv {
    class DiffTest;
}

namespace riscv {

struct InstructionWindowResult {
    bool success = false;
    bool warmup_completed = false;
    bool measure_completed = false;
    CheckpointFailureReason failure_reason = CheckpointFailureReason::NONE;
    std::string message;
    uint64_t stop_pc = 0;
    uint64_t total_instructions = 0;
    uint64_t total_cycles = 0;
    uint64_t measure_cycles = 0;
    uint64_t warmup_instructions_completed = 0;
    uint64_t measure_instructions_completed = 0;
};

/**
 * RISC-V 模拟器主类
 * 统一管理CPU和内存，提供外部接口
 */
class Simulator {
public:
    explicit Simulator(size_t memorySize = Memory::DEFAULT_SIZE,
                       CpuType cpuType = CpuType::IN_ORDER,
                       Address memoryBaseAddress = 0);
    ~Simulator();
    
    // 禁用拷贝构造和赋值
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    
    // 程序管理
    bool loadProgram(const std::string& filename);
    bool loadProgramFromBytes(const std::vector<uint8_t>& program, Address startAddr = 0);
    bool loadRiscvProgram(const std::string& filename, Address loadAddr = 0x1000);
    bool loadElfProgram(const std::string& filename);
    bool loadSnapshot(const SnapshotBundle& snapshot);
    void setHostCommAddresses(Address tohostAddr, Address fromhostAddr);
    void setEnabledExtensions(uint32_t extensions);
    void setCheckpointDiffTestEnabled(bool enabled);
    
    // 执行控制
    void step();                    // 单步执行
    void run();                     // 运行到结束
    bool runWithWarmup(uint64_t warmupCycles, const std::function<void()>& onWarmup);
    InstructionWindowResult runInstructionWindow(uint64_t warmup_instructions,
                                                 uint64_t measure_instructions);
    void reset();                   // 重置模拟器
    
    // 状态查询
    bool isHalted() const;
    uint64_t getInstructionCount() const;
    uint64_t getCycleCount() const { return cycle_count_; }
    bool hasProgramExit() const;
    int getProgramExitCode() const;
    bool endedOnZeroInstruction() const;
    bool isHaltedByInstructionLimit() const { return halted_by_instruction_limit_; }
    bool isHaltedByCycleLimit() const { return halted_by_cycle_limit_; }
    void setMaxInOrderInstructions(uint64_t limit) { max_in_order_instructions_ = limit; }
    void setMaxOutOfOrderCycles(uint64_t limit) { max_out_of_order_cycles_ = limit; }

    // 流水线可视化
    void enablePipelineTracer(const std::string& output_path,
                              uint64_t start_cycle = 0,
                              uint64_t end_cycle = UINT64_MAX,
                              size_t max_instructions = 2000);
    bool writePipelineView() const;
    uint64_t getMaxInOrderInstructions() const { return max_in_order_instructions_; }
    uint64_t getMaxOutOfOrderCycles() const { return max_out_of_order_cycles_; }
    bool hasReferenceExecutionContext() const {
        return reference_memory_ != nullptr && reference_cpu_ != nullptr;
    }
    bool hasDiffTestInstance() const { return difftest_ != nullptr; }
    
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
    static constexpr uint64_t kDefaultMaxInOrderInstructions = 5000000;
    static constexpr uint64_t kDefaultMaxOutOfOrderCycles = 50000;

    // 主CPU内存和CPU
    std::shared_ptr<Memory> memory_;
    std::unique_ptr<ICpuInterface> cpu_;
    CpuType cpuType_;
    uint64_t cycle_count_ = 0;
    size_t memory_size_ = 0;
    Address memory_base_address_ = 0;
    bool checkpoint_difftest_enabled_ = false;
    
    // 参考CPU内存和CPU（仅乱序CPU模式下使用）
    std::shared_ptr<Memory> reference_memory_;
    std::unique_ptr<ICpuInterface> reference_cpu_;
    
    // DiffTest组件（仅乱序CPU模式下使用）
    std::unique_ptr<DiffTest> difftest_;

    // 流水线可视化
    std::unique_ptr<PipelineTracer> pipeline_tracer_;
    std::string pipeline_view_path_;

    bool halted_by_instruction_limit_ = false;
    bool halted_by_cycle_limit_ = false;
    uint64_t max_in_order_instructions_ = kDefaultMaxInOrderInstructions;
    uint64_t max_out_of_order_cycles_ = kDefaultMaxOutOfOrderCycles;
    
    // 辅助方法
    std::vector<uint8_t> loadBinaryFile(const std::string& filename);
    void ensureReferenceExecutionContext();
    void releaseReferenceExecutionContext();
    void ensureDiffTestInitialized();
    void restoreSnapshotMemory(const SnapshotBundle& snapshot, const std::shared_ptr<Memory>& memory) const;
    void restoreSnapshotCpuState(ICpuInterface* cpu, const SnapshotBundle& snapshot) const;
    void synchronizeSharedTranslationState(ICpuInterface* cpu, const SnapshotBundle& snapshot) const;
};

} // namespace riscv
