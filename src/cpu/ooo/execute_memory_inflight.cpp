#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_memory_order.h"
#include "common/debug_types.h"

#include <algorithm>
#include <limits>

namespace riscv {

bool ExecuteMemoryInflight::hasAny(const CPUState& state) {
    return std::any_of(
        state.memory_access_inflight.begin(),
        state.memory_access_inflight.end(),
        [](const MemoryAccessInFlight& entry) { return entry.valid; });
}

bool ExecuteMemoryInflight::tryMove(ExecutionUnit& unit,
                                    ExecutionUnitType unit_type,
                                    size_t unit_index,
                                    CPUState& state) {
    for (auto& entry : state.memory_access_inflight) {
        if (entry.valid) {
            continue;
        }

        entry.valid = true;
        entry.unit_type = unit_type;
        entry.state = unit;
        entry.wait_latency_cycles = unit.remaining_cycles > 0
                                        ? static_cast<uint64_t>(unit.remaining_cycles)
                                        : 0;
        const RSEntry rs_entry = unit.instruction->get_rs_entry();
        state.reservation_station->release_entry(rs_entry);
        unit.instruction->set_rs_entry(std::numeric_limits<RSEntry>::max());
        state.reservation_station->release_execution_unit(unit_type, static_cast<int>(unit_index));
        LOGT(EXECUTE,
             "inst=%" PRId64 " move %s%zu request to inflight queue, remaining=%d",
             unit.instruction->get_instruction_id(),
             unit_type == ExecutionUnitType::LOAD ? "LOAD" : "STORE",
             unit_index,
             unit.remaining_cycles);
        resetExecutionUnitState(unit);
        return true;
    }

    return false;
}

void ExecuteMemoryInflight::advance(CPUState& state, const CompletionCallback& complete) {
    size_t replay_ports_used = 0;
    for (auto& entry : state.memory_access_inflight) {
        if (!entry.valid || !entry.state.instruction) {
            continue;
        }

        auto& inflight = entry.state;
        if (inflight.remaining_cycles > 0) {
            --inflight.remaining_cycles;
        }

        if (inflight.remaining_cycles > 0) {
            LOGT(EXECUTE,
                 "inst=%" PRId64 " %s inflight waiting, remaining=%d",
                 inflight.instruction->get_instruction_id(),
                 entry.unit_type == ExecutionUnitType::LOAD ? "LOAD" : "STORE",
                 inflight.remaining_cycles);
            continue;
        }

        if (replay_ports_used >= OOOPipelineConfig::MEMORY_REPLAY_WIDTH) {
            state.recordPipelineStall(PerfCounterId::STALL_MEMORY_REPLAY_PORT_BUSY);
            LOGT(EXECUTE,
                 "inst=%" PRId64 " %s inflight ready but replay ports exhausted %zu/%zu",
                 inflight.instruction->get_instruction_id(),
                 entry.unit_type == ExecutionUnitType::LOAD ? "LOAD" : "STORE",
                 replay_ports_used,
                 OOOPipelineConfig::MEMORY_REPLAY_WIDTH);
            continue;
        }
        ++replay_ports_used;

        if (entry.unit_type == ExecutionUnitType::LOAD) {
            LOGT(EXECUTE,
                 "inst=%" PRId64 " LOAD inflight done, result=0x%" PRIx64 " -> CDB",
                 inflight.instruction->get_instruction_id(),
                 inflight.result);
            state.perf_counters.increment(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_COUNT);
            state.perf_counters.increment(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_TOTAL,
                                          entry.wait_latency_cycles);
            const uint64_t max_latency =
                state.perf_counters.value(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_MAX);
            if (entry.wait_latency_cycles > max_latency) {
                state.perf_counters.increment(PerfCounterId::MEMORY_INFLIGHT_LOAD_MISS_LATENCY_MAX,
                                              entry.wait_latency_cycles - max_latency);
            }
            ExecuteMemoryOrder::recordLoadReplayBucket(inflight.instruction, state);
            complete(inflight, ExecutionUnitType::LOAD);
        } else {
            inflight.result = 0;
            LOGT(EXECUTE,
                 "inst=%" PRId64 " STORE inflight done, notify ROB",
                 inflight.instruction->get_instruction_id());
            if (ExecuteMemoryOrder::tryRecoverViolation(inflight.instruction, state)) {
                resetMemoryAccessInFlightState(entry);
                return;
            }
            complete(inflight, ExecutionUnitType::STORE);
        }

        resetMemoryAccessInFlightState(entry);
    }
}

} // namespace riscv
