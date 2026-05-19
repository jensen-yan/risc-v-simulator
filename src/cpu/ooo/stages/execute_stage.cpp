#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/execute_control_recovery.h"
#include "cpu/ooo/execute_load_completion.h"
#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_memory_order.h"
#include "cpu/ooo/execute_semantics.h"
#include "cpu/ooo/execute_store_access.h"
#include "common/debug_types.h"
#include "common/types.h"
#include "core/instruction_executor.h"

namespace riscv {

ExecuteStage::ExecuteStage() {
    // 构造函数：初始化执行阶段
}

bool ExecuteStage::Context::hasInflightMemoryAccess() const {
    return ExecuteMemoryInflight::hasAny(state_);
}

ExecutionUnit* ExecuteStage::Context::executionUnit(ExecutionUnitType unit_type, int unit_id) {
    if (unit_id < 0) {
        return nullptr;
    }

    const auto index = static_cast<size_t>(unit_id);
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            return index < state_.alu_units.size() ? &state_.alu_units[index] : nullptr;
        case ExecutionUnitType::FP:
            return index < state_.fp_units.size() ? &state_.fp_units[index] : nullptr;
        case ExecutionUnitType::BRANCH:
            return index < state_.branch_units.size() ? &state_.branch_units[index] : nullptr;
        case ExecutionUnitType::LOAD:
            return index < state_.load_units.size() ? &state_.load_units[index] : nullptr;
        case ExecutionUnitType::STORE:
            return index < state_.store_units.size() ? &state_.store_units[index] : nullptr;
    }
    return nullptr;
}

void ExecuteStage::execute(Context& context) {
    CPUState& state = context.stateForLegacyExecuteInternals();

    // 首先更新正在执行的指令的状态
    update_execution_units(state);
    ExecuteMemoryInflight::advance(
        state,
        [this, &state](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            complete_execution_unit(
                unit, unit_type, 0, state, /*release_dispatch_unit=*/false);
        });

    context.incrementCounter(PerfCounterId::DISPATCH_SLOTS, OOOPipelineConfig::DISPATCH_WIDTH);
    const auto addr_unknown_store_snapshot =
        ExecuteMemoryOrder::captureAddrUnknownStoreSnapshot(state);

    const auto dispatch_results = context.dispatchReadyInstructions(
        OOOPipelineConfig::DISPATCH_WIDTH,
        [&](const DynamicInstPtr& instruction) {
            return !ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(
                state, instruction, addr_unknown_store_snapshot);
        });
    if (dispatch_results.empty()) {
        LOGT(EXECUTE, "no ready instruction in reservation station");
        context.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_READY);

        if (context.hasInflightMemoryAccess()) {
            context.incrementCounter(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED);
            return;
        }

        const size_t rs_occupied = context.reservationStationOccupiedCount();
        if (rs_occupied == 0) {
            context.incrementCounter(PerfCounterId::STALL_EXECUTE_FRONTEND_STARVED);
        } else {
            const size_t rs_ready = context.reservationStationReadyCount();
            if (rs_ready == 0) {
                context.incrementCounter(PerfCounterId::STALL_EXECUTE_DEPENDENCY_BLOCKED);
            } else {
                context.incrementCounter(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED);
            }
        }
    }

    size_t dispatched_this_cycle = 0;
    for (size_t slot = 0; slot < dispatch_results.size(); ++slot) {
        auto dispatch_result = dispatch_results[slot];
        LOGT(EXECUTE, "dispatch slot=%zu inst=%" PRId64 " from rs[%d] to execution unit",
             slot, dispatch_result.instruction->get_instruction_id(), dispatch_result.rs_entry);

        const auto& decoded_info = dispatch_result.instruction->get_decoded_info();
        if (decoded_info.opcode == Opcode::AMO &&
            context.hasEarlierStoreUncommitted(dispatch_result.instruction->get_instruction_id())) {
            dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
            context.releaseExecutionUnit(dispatch_result.unit_type, dispatch_result.unit_id);
            context.recordPipelineStall(PerfCounterId::STALL_EXECUTE_AMO_WAIT);
            LOGT(EXECUTE, "inst=%" PRId64 " AMO waits on earlier uncommitted store-like op, delay dispatch",
                 dispatch_result.instruction->get_instruction_id());
            continue;
        }

        ExecutionUnit* unit = context.executionUnit(dispatch_result.unit_type, dispatch_result.unit_id);

        if (!unit || unit->busy) {
            dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
            context.releaseExecutionUnit(dispatch_result.unit_type, dispatch_result.unit_id);
            LOGT(EXECUTE, "no available execution unit");
            context.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_UNIT);
            continue;
        }

        unit->busy = true;
        unit->instruction = dispatch_result.instruction;
        unit->has_exception = false;
        unit->dcache.reset();
        unit->remaining_cycles = decoded_info.execution_cycles;

        if (dispatch_result.unit_type == ExecutionUnitType::LOAD) {
            auto& memory_info = dispatch_result.instruction->get_memory_info();
            if (ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(
                    state, dispatch_result.instruction, addr_unknown_store_snapshot)) {
                dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
                context.releaseExecutionUnit(
                    ExecutionUnitType::LOAD, dispatch_result.unit_id);
                continue;
            }
            const auto older_unknown_store_pc = ExecuteMemoryOrder::findFirstOlderAddrUnknownStorePc(
                addr_unknown_store_snapshot, dispatch_result.instruction->get_instruction_id());
            if (older_unknown_store_pc.has_value()) {
                const uint64_t load_pc = dispatch_result.instruction->get_pc();
                if (!memory_info.speculated_past_addr_unknown_store &&
                    state.shouldSpeculatePastAddrUnknownStore(load_pc, *older_unknown_store_pc)) {
                    const uint64_t store_pc = *older_unknown_store_pc;
                    memory_info.speculated_past_addr_unknown_store = true;
                    memory_info.has_speculated_addr_unknown_source = true;
                    memory_info.speculated_addr_unknown_store_pc = store_pc;
                    context.incrementCounter(PerfCounterId::LOADS_SPECULATED_ADDR_UNKNOWN);
                    LOGT(EXECUTE,
                         "inst=%" PRId64
                         " load dispatch speculates past unresolved STORE pc=0x%" PRIx64,
                         dispatch_result.instruction->get_instruction_id(),
                         store_pc);
                }
            }
        }

        const char* unit_type_str = "";
        switch (dispatch_result.unit_type) {
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
             dispatch_result.instruction->get_instruction_id(),
             unit_type_str,
             dispatch_result.unit_id,
             unit->remaining_cycles);

        context.incrementCounter(PerfCounterId::DISPATCHED_INSTRUCTIONS);
        dispatch_result.instruction->set_execute_cycle(context.cycleCount());
        OOOExecuteSemantics::executeInstruction(
            *unit, dispatch_result.instruction, context.stateForExecuteSemantics());
        dispatched_this_cycle++;
    }

    context.incrementCounter(PerfCounterId::DISPATCH_UTILIZED_SLOTS, dispatched_this_cycle);
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

                LOGT(EXECUTE, "inst=%" PRId64 " ALU%zu done, result=0x%" PRIx64 " -> CDB",
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
                LOGT(EXECUTE, "inst=%" PRId64 " FP%zu done, result=0x%" PRIx64 " -> CDB",
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
                LOGT(EXECUTE, "inst=%" PRId64 " BRANCH%zu done -> CDB",
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

void ExecuteStage::complete_execution_unit(ExecutionUnit& unit,
                                           ExecutionUnitType unit_type,
                                           size_t unit_index,
                                           CPUState& state,
                                           bool release_dispatch_unit) {
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
    
    // 执行完成，发送到CDB
    CommonDataBusEntry cdb_entry(unit.instruction);
    state.cdb_queue.push(cdb_entry);
    state.perf_counters.increment(PerfCounterId::CDB_ENQUEUED);
    
    // 清空对应的保留站条目
    RSEntry rs_entry = unit.instruction->get_rs_entry();
    state.reservation_station->release_entry(rs_entry);

    if (release_dispatch_unit) {
        state.reservation_station->release_execution_unit(unit_type, unit_index);
    }

    // 释放执行单元
    resetExecutionUnitState(unit);
}

} // namespace riscv 
