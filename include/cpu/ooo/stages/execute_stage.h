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
    // 辅助方法
    void execute_instruction(ExecutionUnit& unit, DynamicInstPtr instruction, CPUState& state);
    void update_execution_units(CPUState& state);
    ExecutionUnit* get_available_unit(ExecutionUnitType type, CPUState& state);
    bool execute_branch_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2, CPUState& state);
    void execute_store_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2, CPUState& state);
    bool perform_load_execution(ExecutionUnit& unit, CPUState& state);
    
    // 分支预测相关
    bool predict_branch(uint32_t pc);
    void update_branch_predictor(uint32_t pc, bool taken);
    void flush_pipeline(CPUState& state);
    void reset_execution_units(CPUState& state);
    
    // 执行单元重置的辅助函数
    void reset_single_unit(ExecutionUnit& unit);
    template<typename UnitContainer>
    void reset_unit_container(UnitContainer& units);
    
    // 执行单元完成时的公共处理逻辑
    void complete_execution_unit(ExecutionUnit& unit, ExecutionUnitType unit_type, size_t unit_index, CPUState& state);
    
    // 调试辅助方法
    void print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc);
};

} // namespace riscv 