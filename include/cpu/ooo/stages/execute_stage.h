#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"
#include "cpu/ooo/ooo_types.h"

#include <functional>
#include <vector>

namespace riscv {

/**
 * 执行阶段实现
 * 负责从保留站调度指令到执行单元，管理指令执行过程
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

        std::vector<ReservationStation::DispatchResult> dispatchReadyInstructions(
            size_t limit,
            const std::function<bool(const DynamicInstPtr&)>& can_dispatch) {
            return state_.reservation_station->dispatch_instructions(limit, can_dispatch);
        }

        bool hasInflightMemoryAccess() const;
        size_t reservationStationOccupiedCount() const {
            return state_.reservation_station->get_occupied_entry_count();
        }
        size_t reservationStationReadyCount() const {
            return state_.reservation_station->get_ready_entry_count();
        }

        bool hasEarlierStoreUncommitted(uint64_t instruction_id) const {
            return state_.reorder_buffer->has_earlier_store_uncommitted(instruction_id);
        }
        void releaseExecutionUnit(ExecutionUnitType unit_type, int unit_id) {
            state_.reservation_station->release_execution_unit(unit_type, unit_id);
        }
        ExecutionUnit* executionUnit(ExecutionUnitType unit_type, int unit_id);
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
    enum class LoadExecutionResult {
        Forwarded,
        LoadedFromMemory,
        WaitingForCache,
        BlockedByStore,
        Exception
    };

    // 辅助方法
    void update_execution_units(CPUState& state);
    void update_memory_access_inflight(CPUState& state);
    ExecutionUnit* get_available_unit(ExecutionUnitType type, CPUState& state);
    LoadExecutionResult perform_load_execution(ExecutionUnit& unit, CPUState& state);
    bool start_or_wait_dcache_access(ExecutionUnit& unit,
                                     CPUState& state,
                                     CacheAccessType access_type,
                                     PerfCounterId stall_counter_id);
    bool move_memory_access_to_inflight(ExecutionUnit& unit,
                                        ExecutionUnitType unit_type,
                                        size_t unit_index,
                                        CPUState& state);
    void record_dcache_access_result(CPUState& state,
                                     CacheAccessType access_type,
                                     const CacheAccessResult& cache_result);
    
    // 执行单元完成时的公共处理逻辑
    void complete_execution_unit(ExecutionUnit& unit,
                                 ExecutionUnitType unit_type,
                                 size_t unit_index,
                                 CPUState& state,
                                 bool release_dispatch_unit = true);
    bool try_recover_control_mispredict_early(ExecutionUnit& unit,
                                              ExecutionUnitType unit_type,
                                              size_t unit_index,
                                              CPUState& state);
    size_t flush_younger_cdb_entries(CPUState& state, uint64_t instruction_id);
    bool flush_younger_execution_units(CPUState& state,
                                       uint64_t instruction_id,
                                       ExecutionUnitType current_unit_type,
                                       size_t current_unit_index);
    void erase_younger_rename_checkpoints(CPUState& state, uint64_t instruction_id);
    bool try_recover_memory_order_violation(const DynamicInstPtr& store_instruction,
                                            CPUState& state);

    // 记录load replay分布桶
    void record_load_replay_bucket(const DynamicInstPtr& instruction, CPUState& state);
    void record_load_replay_reason(const DynamicInstPtr& instruction,
                                   CPUState& state,
                                   PerfCounterId reason_counter_id);
};

} // namespace riscv 
