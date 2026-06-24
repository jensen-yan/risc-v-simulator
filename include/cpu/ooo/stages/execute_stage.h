#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"
#include "cpu/ooo/ooo_types.h"

#include <vector>

namespace riscv {

/**
 * 执行阶段实现
 * 负责推进执行单元，并把完成结果提交到 Completion Fabric。
 */
class ExecuteStage : public PipelineStage {
public:
    ExecuteStage();
    virtual ~ExecuteStage() = default;

    class Context {
    public:
        explicit Context(CPUState& state) : state_(state) {}

        void incrementCounter(PerfCounterId id, uint64_t amount = 1) {
            state_.perf_counters.increment(id, amount);
        }
        void recordPipelineStall(PerfCounterId reason) {
            state_.recordPipelineStall(reason);
        }

        uint64_t cycleCount() const { return state_.cycle_count; }

        // ExecuteStage still owns several legacy cross-cutting paths (LSU and
        // recovery). Keep the wide access explicit until those paths move into
        // deeper modules.
        CPUState& stateForLegacyExecuteInternals() { return state_; }
        CPUState& stateForExecuteSemantics() { return state_; }

    private:
        CPUState& state_;
    };

    void execute(Context& context);
    const char* get_stage_name() const override { return "EXECUTE"; }

private:
    // 辅助方法
    void update_execution_units(CPUState& state);
    
    // 执行单元完成时的公共处理逻辑
    bool complete_execution_unit(ExecutionUnit& unit,
                                 ExecutionUnitType unit_type,
                                 size_t unit_index,
                                 CPUState& state);
};

} // namespace riscv 
