#pragma once

#include "common/types.h"
#include "core/memory.h"
#include "core/decoder.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/ooo_types.h"
#include "cpu/ooo/cpu_state.h"
#include "cpu/ooo/pipeline_stage.h"
#include "common/cpu_interface.h"
#include <array>
#include <memory>
#include <queue>

namespace riscv {

class SyscallHandler;
class FetchStage;
class DecodeStage;
class IssueStage;
class ExecuteStage;
class WritebackStage;
class CommitStage;

/**
 * 乱序执行RISC-V CPU核心类
 * 
 * 功能：
 * 1. 实现完整的乱序执行流水线
 * 2. 集成寄存器重命名、保留站、重排序缓冲等组件
 * 3. 支持精确异常处理
 * 4. 支持分支预测错误恢复
 * 5. 维护程序语义正确性
 */
class OutOfOrderCPU : public ICpuInterface {
public:
    static constexpr size_t NUM_REGISTERS = 32;
    static constexpr size_t NUM_FP_REGISTERS = 32;
    static constexpr size_t PIPELINE_WIDTH = 4;  // 流水线宽度
    
    explicit OutOfOrderCPU(std::shared_ptr<Memory> memory);
    ~OutOfOrderCPU() override;
    
    // 禁用拷贝构造和赋值
    OutOfOrderCPU(const OutOfOrderCPU&) = delete;
    OutOfOrderCPU& operator=(const OutOfOrderCPU&) = delete;
    
    // 执行控制
    void step() override;                    // 单步执行（执行一个时钟周期）
    void run() override;                     // 连续执行直到结束
    void reset() override;                   // 重置CPU状态
    
    // 寄存器访问（架构寄存器值）
    uint64_t getRegister(RegNum reg) const override;
    void setRegister(RegNum reg, uint64_t value) override;
    
    // 浮点寄存器访问
    uint64_t getFPRegister(RegNum reg) const override;
    void setFPRegister(RegNum reg, uint64_t value) override;
    float getFPRegisterFloat(RegNum reg) const override;
    void setFPRegisterFloat(RegNum reg, float value) override;
    
    // 程序计数器
    uint64_t getPC() const override { return cpu_state_.pc; }
    void setPC(uint64_t pc) override { cpu_state_.pc = pc; }
    
    // 状态查询
    bool isHalted() const override { return cpu_state_.halted; }
    uint64_t getInstructionCount() const override { return cpu_state_.instruction_count; }
    uint64_t getCycleCount() const { return cpu_state_.cycle_count; }
    void requestHalt() override { cpu_state_.halted = true; }
    
    // 扩展支持
    void setEnabledExtensions(uint32_t extensions) override { cpu_state_.enabled_extensions = extensions; }
    uint32_t getEnabledExtensions() const override { return cpu_state_.enabled_extensions; }
    
    // 性能统计
    StatsList getStats() const override;
    
    // 调试功能
    void dumpRegisters() const override;
    void dumpState() const override;
    void dumpPipelineState() const;
    
    // DiffTest控制接口
    void setDiffTest(class DiffTest* difftest) override;  // 由Simulator设置DiffTest引用
    void enableDiffTest(bool enable);
    bool isDiffTestEnabled() const override;
    void performDiffTestWithCommittedPC(uint64_t committed_pc) override;
    void getDiffTestStats(uint64_t& comparisons, uint64_t& mismatches) const;
    
private:
    // 新的流水线设计
    CPUState cpu_state_;                            // CPU共享状态
    std::unique_ptr<FetchStage> fetch_stage_;       // 取指阶段
    std::unique_ptr<DecodeStage> decode_stage_;     // 译码阶段
    std::unique_ptr<IssueStage> issue_stage_;       // 发射阶段
    std::unique_ptr<ExecuteStage> execute_stage_;   // 执行阶段
    std::unique_ptr<WritebackStage> writeback_stage_; // 写回阶段
    std::unique_ptr<CommitStage> commit_stage_;     // 提交阶段
    
    // 向后兼容：保留必要的接口变量  
    std::shared_ptr<Memory> memory_;
    std::unique_ptr<SyscallHandler> syscall_handler_;
    
    // DiffTest组件（由Simulator管理，这里只保存引用）
    class DiffTest* difftest_;
    bool difftest_synced_once_ = false;
    
    // 异常处理
    void handle_exception(const std::string& exception_msg, uint64_t pc);
    void flush_pipeline();
    
    // 分支预测（简化实现）
    bool predict_branch(uint64_t pc);
    void update_branch_predictor(uint64_t pc, bool taken);
    
    // 系统调用处理
    void handleEcall();
    void handleEbreak();
    
    // 辅助方法
    uint64_t get_physical_register_value(PhysRegNum reg) const;
    void set_physical_register_value(PhysRegNum reg, uint64_t value);
    uint32_t get_physical_fp_register_value(PhysRegNum reg) const;
    void set_physical_fp_register_value(PhysRegNum reg, uint32_t value);
    
    // 内存访问辅助方法
    uint64_t loadFromMemory(Address addr, Funct3 funct3);
    void storeToMemory(Address addr, uint64_t value, Funct3 funct3);
    
    // 立即数符号扩展
    int32_t signExtend(uint32_t value, int bits) const;
};

} // namespace riscv
