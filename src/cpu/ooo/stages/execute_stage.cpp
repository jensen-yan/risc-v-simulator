#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/execute_control_recovery.h"
#include "cpu/ooo/execute_load_completion.h"
#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_semantics.h"
#include "cpu/ooo/execute_store_access.h"
#include "cpu/ooo/issue_ready_select.h"
#include "common/debug_types.h"
#include "common/types.h"
#include "core/instruction_executor.h"

namespace riscv {

ExecuteStage::ExecuteStage() {
    // 构造函数：初始化执行阶段
}

void ExecuteStage::execute(Context& context) {
    CPUState& state = context.stateForLegacyExecuteInternals();

    // 首先更新正在执行的指令的状态
    update_execution_units(state);
    ExecuteMemoryInflight::advance(
        state,
        [this, &state](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            return complete_execution_unit(unit, unit_type, 0, state);
        });

    const auto issue_result = IssueReadySelect::select(state, OOOPipelineConfig::ISSUE_WIDTH);
    for (size_t slot = 0; slot < issue_result.selected.size(); ++slot) {
        const auto& selected = issue_result.selected[slot];
        LOGT(EXECUTE, "issue slot=%zu inst=%" PRId64 " from rs[%d] to execution unit",
             slot, selected.instruction->get_instruction_id(), selected.rs_entry);

        ExecutionUnit* unit = selected.unit;
        if (!unit) {
            LOGT(EXECUTE, "issue selection returned no execution unit");
            continue;
        }

        const char* unit_type_str = "";
        switch (selected.unit_type) {
            case ExecutionUnitType::ALU:
                unit_type_str = "ALU";
                break;
            case ExecutionUnitType::FP:
                unit_type_str = "FP";
                break;
            case ExecutionUnitType::BRANCH:
                unit_type_str = "BRANCH";
                break;
            case ExecutionUnitType::LOAD:
                unit_type_str = "LOAD";
                break;
            case ExecutionUnitType::STORE:
                unit_type_str = "STORE";
                break;
        }

        LOGT(EXECUTE, "inst=%" PRId64 " start on %s%d, cycles=%d",
             selected.instruction->get_instruction_id(),
             unit_type_str,
             static_cast<int>(selected.unit_index),
             unit->remaining_cycles);

        context.incrementCounter(PerfCounterId::ISSUED_INSTRUCTIONS);
        selected.instruction->set_execute_cycle(context.cycleCount());
        OOOExecuteSemantics::executeInstruction(
            *unit, selected.instruction, context.stateForExecuteSemantics());
    }
}

void ExecuteStage::update_execution_units(CPUState& state) {
    // 更新ALU单元
    for (size_t i = 0; i < state.alu_units.size(); ++i) {
        auto& unit = state.alu_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " ALU%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                const auto& inst = unit.instruction->get_decoded_info();
                if (inst.opcode == Opcode::AMO &&
                    state.reorder_buffer->has_earlier_store_uncommitted(unit.instruction->get_instruction_id())) {
                    // 双保险：若AMO执行期间出现顺序约束，延迟完成，等待更老Store/AMO提交。
                    unit.remaining_cycles = 1;
                    LOGT(EXECUTE, "inst=%" PRId64 " AMO waits on earlier uncommitted store-like op, delay completion",
                        unit.instruction->get_instruction_id());
                    continue;
                }

                LOGT(EXECUTE, "inst=%" PRId64 " ALU%zu done, result=0x%" PRIx64 " -> completion fabric",
                    unit.instruction->get_instruction_id(), i, unit.result);
                
                complete_execution_unit(unit, ExecutionUnitType::ALU, i, state);
            }
        }
    }

    for (size_t i = 0; i < state.fp_units.size(); ++i) {
        auto& unit = state.fp_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " FP%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);

            if (unit.remaining_cycles <= 0) {
                LOGT(EXECUTE, "inst=%" PRId64 " FP%zu done, result=0x%" PRIx64 " -> completion fabric",
                    unit.instruction->get_instruction_id(), i, unit.result);
                complete_execution_unit(unit, ExecutionUnitType::FP, i, state);
            }
        }
    }
    
    // 更新分支单元
    for (size_t i = 0; i < state.branch_units.size(); ++i) {
        auto& unit = state.branch_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " BRANCH%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                LOGT(EXECUTE, "inst=%" PRId64 " BRANCH%zu done -> completion fabric",
                    unit.instruction->get_instruction_id(), i);
                complete_execution_unit(unit, ExecutionUnitType::BRANCH, i, state);
            }
        }
    }
    
    // 更新加载单元
    for (size_t i = 0; i < state.load_units.size(); ++i) {
        auto& unit = state.load_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                if (ExecuteLoadCompletion::perform(unit, i, state) ==
                    ExecuteLoadCompletion::Result::Completed) {
                    complete_execution_unit(unit, ExecutionUnitType::LOAD, i, state);
                }
            }
        }
    }
    
    // 更新存储单元
    for (size_t i = 0; i < state.store_units.size(); ++i) {
        auto& unit = state.store_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                const auto store_result = ExecuteStoreAccess::perform(unit, i, state);
                if (store_result == ExecuteStoreAccess::Result::RecoveryTriggered) {
                    return;
                }
                if (store_result == ExecuteStoreAccess::Result::Completed) {
                    complete_execution_unit(unit, ExecutionUnitType::STORE, i, state);
                }
            }
        }
    }
}

bool ExecuteStage::complete_execution_unit(ExecutionUnit& unit,
                                           ExecutionUnitType unit_type,
                                           size_t unit_index,
                                           CPUState& state) {
    if (!unit.instruction) {
        resetExecutionUnitState(unit);
        return true;
    }

    if (!unit.completion_pending) {
        unit.instruction->set_complete_cycle(state.cycle_count);

        if (unit.has_exception) {
            unit.instruction->set_exception(unit.exception_msg);
        } else {
            unit.instruction->clear_exception();
        }

        // 设置执行结果和跳转信息到DynamicInst
        unit.instruction->set_result(unit.result);
        unit.instruction->set_jump_info(unit.is_jump, unit.jump_target);
        ExecuteControlRecovery::tryRecoverEarly(unit, unit_type, unit_index, state);
        unit.completion_pending = true;
    }
    
    CompletionEvent completion_event(unit.instruction);
    if (!state.completion_fabric.trySubmit(completion_event)) {
        state.recordPipelineStall(PerfCounterId::STALL_COMPLETION_PORT_BUSY);
        LOGT(EXECUTE,
             "inst=%" PRId64 " completion fabric full, hold %s%zu result",
             unit.instruction->get_instruction_id(),
             unit_type == ExecutionUnitType::ALU ? "ALU" :
             unit_type == ExecutionUnitType::FP ? "FP" :
             unit_type == ExecutionUnitType::BRANCH ? "BRANCH" :
             unit_type == ExecutionUnitType::LOAD ? "LOAD" : "STORE",
             unit_index);
        return false;
    }

    state.perf_counters.increment(PerfCounterId::COMPLETION_ACCEPTED);
    
    // 清空对应的保留站条目
    RSEntry rs_entry = unit.instruction->get_rs_entry();
    state.reservation_station->release_entry(rs_entry);

    // 释放执行单元
    resetExecutionUnitState(unit);
    return true;
}

} // namespace riscv 
