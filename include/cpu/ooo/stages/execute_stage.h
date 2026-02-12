#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"
#include "cpu/ooo/ooo_types.h"

namespace riscv {

/**
 * 执行阶段实现
 * 负责从保留站调度指令到执行单元，管理指令执行过程
 */
class ExecuteStage : public PipelineStage {
public:
    ExecuteStage();
    virtual ~ExecuteStage() = default;
    
    // 实现PipelineStage接口
    void execute(CPUState& state) override;
    void flush() override;
    void reset() override;
    const char* get_stage_name() const override { return "EXECUTE"; }

private:
    enum class LoadExecutionResult {
        Forwarded,
        LoadedFromMemory,
        WaitingForCache,
        BlockedByStore,
        Exception
    };

    // 辅助方法
    void update_execution_units(CPUState& state);
    ExecutionUnit* get_available_unit(ExecutionUnitType type, CPUState& state);
    LoadExecutionResult perform_load_execution(ExecutionUnit& unit, CPUState& state);
    bool start_or_wait_dcache_access(ExecutionUnit& unit,
                                     CPUState& state,
                                     CacheAccessType access_type,
                                     PerfCounterId stall_counter_id);
    void reset_execution_units(CPUState& state);
    
    // 执行单元重置的辅助函数
    void reset_single_unit(ExecutionUnit& unit);
    template<typename UnitContainer>
    void reset_unit_container(UnitContainer& units);
    
    // 执行单元完成时的公共处理逻辑
    void complete_execution_unit(ExecutionUnit& unit, ExecutionUnitType unit_type, size_t unit_index, CPUState& state);

    // 记录load replay分布桶
    void record_load_replay_bucket(const DynamicInstPtr& instruction, CPUState& state);
};

} // namespace riscv 
