#include "cpu/ooo/issue_ready_select.h"

#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_memory_order.h"
#include "common/debug_types.h"

#include <array>
#include <optional>
#include <vector>

namespace riscv {

namespace {

template <size_t N>
std::array<bool, N> availabilityFrom(const std::array<ExecutionUnit, N>& units) {
    std::array<bool, N> available{};
    for (size_t i = 0; i < N; ++i) {
        available[i] = !units[i].busy;
    }
    return available;
}

struct UnitAvailability {
    std::array<bool, OOOPipelineConfig::ALU_UNITS> alu;
    std::array<bool, OOOPipelineConfig::FP_UNITS> fp;
    std::array<bool, OOOPipelineConfig::BRANCH_UNITS> branch;
    std::array<bool, OOOPipelineConfig::LOAD_UNITS> load;
    std::array<bool, OOOPipelineConfig::STORE_UNITS> store;
};

UnitAvailability captureAvailability(const CPUState& state) {
    return {
        availabilityFrom(state.alu_units),
        availabilityFrom(state.fp_units),
        availabilityFrom(state.branch_units),
        availabilityFrom(state.load_units),
        availabilityFrom(state.store_units),
    };
}

template <size_t N>
std::optional<size_t> firstAvailable(const std::array<bool, N>& available) {
    for (size_t i = 0; i < N; ++i) {
        if (available[i]) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<size_t> firstAvailableUnit(const UnitAvailability& availability,
                                         ExecutionUnitType unit_type) {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            return firstAvailable(availability.alu);
        case ExecutionUnitType::FP:
            return firstAvailable(availability.fp);
        case ExecutionUnitType::BRANCH:
            return firstAvailable(availability.branch);
        case ExecutionUnitType::LOAD:
            return firstAvailable(availability.load);
        case ExecutionUnitType::STORE:
            return firstAvailable(availability.store);
    }
    return std::nullopt;
}

void reserveUnit(UnitAvailability& availability, ExecutionUnitType unit_type, size_t unit_index) {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            availability.alu[unit_index] = false;
            break;
        case ExecutionUnitType::FP:
            availability.fp[unit_index] = false;
            break;
        case ExecutionUnitType::BRANCH:
            availability.branch[unit_index] = false;
            break;
        case ExecutionUnitType::LOAD:
            availability.load[unit_index] = false;
            break;
        case ExecutionUnitType::STORE:
            availability.store[unit_index] = false;
            break;
    }
}

template <size_t N>
ExecutionUnit* unitAt(std::array<ExecutionUnit, N>& units, size_t unit_index) {
    return unit_index < N ? &units[unit_index] : nullptr;
}

ExecutionUnit* executionUnit(CPUState& state, ExecutionUnitType unit_type, size_t unit_index) {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            return unitAt(state.alu_units, unit_index);
        case ExecutionUnitType::FP:
            return unitAt(state.fp_units, unit_index);
        case ExecutionUnitType::BRANCH:
            return unitAt(state.branch_units, unit_index);
        case ExecutionUnitType::LOAD:
            return unitAt(state.load_units, unit_index);
        case ExecutionUnitType::STORE:
            return unitAt(state.store_units, unit_index);
    }
    return nullptr;
}

void startExecutionUnit(ExecutionUnit& unit, const DynamicInstPtr& instruction) {
    const auto& decoded_info = instruction->get_decoded_info();
    unit.busy = true;
    unit.instruction = instruction;
    unit.has_exception = false;
    unit.completion_pending = false;
    unit.dcache.reset();
    unit.remaining_cycles = decoded_info.execution_cycles;
}

const char* unitTypeName(ExecutionUnitType unit_type) {
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            return "ALU";
        case ExecutionUnitType::FP:
            return "FP";
        case ExecutionUnitType::BRANCH:
            return "BRANCH";
        case ExecutionUnitType::LOAD:
            return "LOAD";
        case ExecutionUnitType::STORE:
            return "STORE";
    }
    return "UNKNOWN";
}

bool isAmoWaitingForOlderStore(const CPUState& state, const DynamicInstPtr& instruction) {
    return instruction && instruction->get_decoded_info().opcode == Opcode::AMO &&
           state.reorder_buffer &&
           state.reorder_buffer->has_earlier_store_uncommitted(instruction->get_instruction_id());
}

void maybeMarkAddrUnknownSpeculation(
    CPUState& state,
    const DynamicInstPtr& instruction,
    const ExecuteMemoryOrder::AddrUnknownStoreSnapshot& snapshot) {
    if (!instruction || !instruction->is_load_instruction()) {
        return;
    }

    auto& memory_info = instruction->get_memory_info();
    const auto older_unknown_store_pc =
        ExecuteMemoryOrder::findFirstOlderAddrUnknownStorePc(snapshot, instruction->get_instruction_id());
    if (!older_unknown_store_pc.has_value()) {
        return;
    }

    const uint64_t load_pc = instruction->get_pc();
    if (!memory_info.speculated_past_addr_unknown_store &&
        state.shouldSpeculatePastAddrUnknownStore(load_pc, *older_unknown_store_pc)) {
        const uint64_t store_pc = *older_unknown_store_pc;
        memory_info.speculated_past_addr_unknown_store = true;
        memory_info.has_speculated_addr_unknown_source = true;
        memory_info.speculated_addr_unknown_store_pc = store_pc;
        state.perf_counters.increment(PerfCounterId::LOADS_SPECULATED_ADDR_UNKNOWN);
        LOGT(EXECUTE,
             "inst=%" PRId64 " load issue speculates past unresolved STORE pc=0x%" PRIx64,
             instruction->get_instruction_id(),
             store_pc);
    }
}

void classifyEmptySlots(CPUState& state,
                        IssueReadySelect::Result& result,
                        size_t issue_width,
                        size_t classified_slots,
                        bool use_inflight_override) {
    if (classified_slots >= issue_width) {
        if (classified_slots > issue_width) {
            state.perf_counters.increment(
                PerfCounterId::TOPDOWN_SLOTS_OTHER,
                classified_slots - issue_width);
        }
        return;
    }

    const size_t empty_slots = issue_width - classified_slots;
    if (use_inflight_override && ExecuteMemoryInflight::hasAny(state)) {
        result.resource_blocked_slots += empty_slots;
        return;
    }

    const size_t rs_occupied = state.reservation_station
                                   ? state.reservation_station->get_occupied_entry_count()
                                   : 0;
    if (rs_occupied == 0) {
        result.frontend_empty_slots += empty_slots;
    } else if (state.reservation_station->get_ready_entry_count() == 0) {
        result.dependency_blocked_slots += empty_slots;
    } else {
        result.resource_blocked_slots += empty_slots;
    }
}

} // namespace

IssueReadySelect::Result IssueReadySelect::select(CPUState& state, size_t issue_width) {
    Result result;
    state.perf_counters.increment(PerfCounterId::ISSUE_SLOTS, issue_width);
    state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_TOTAL, issue_width);

    if (!state.reservation_station || issue_width == 0) {
        classifyEmptySlots(state, result, issue_width, 0, false);
        state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_FRONTEND_EMPTY,
                                      result.frontend_empty_slots);
        return result;
    }

    const auto ready_entries = state.reservation_station->ready_entries();
    auto availability = captureAvailability(state);
    std::vector<bool> consumed(ready_entries.size(), false);
    const auto addr_unknown_store_snapshot =
        ExecuteMemoryOrder::captureAddrUnknownStoreSnapshot(state);

    while (result.selected.size() + result.amo_wait_slots + result.no_unit_slots < issue_width) {
        std::optional<size_t> chosen_ready_index;
        std::optional<size_t> chosen_unit_index;
        ExecutionUnitType chosen_unit_type = ExecutionUnitType::ALU;

        for (size_t i = 0; i < ready_entries.size(); ++i) {
            if (consumed[i]) {
                continue;
            }

            const auto& entry = ready_entries[i];
            const auto unit_type = entry.instruction->get_required_execution_unit();
            const auto unit_index = firstAvailableUnit(availability, unit_type);
            if (!unit_index.has_value()) {
                continue;
            }

            if (ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(
                    state, entry.instruction, addr_unknown_store_snapshot)) {
                consumed[i] = true;
                continue;
            }

            chosen_ready_index = i;
            chosen_unit_index = *unit_index;
            chosen_unit_type = unit_type;
            break;
        }

        if (!chosen_ready_index.has_value() || !chosen_unit_index.has_value()) {
            break;
        }

        consumed[*chosen_ready_index] = true;
        reserveUnit(availability, chosen_unit_type, *chosen_unit_index);

        const auto& entry = ready_entries[*chosen_ready_index];
        if (isAmoWaitingForOlderStore(state, entry.instruction)) {
            state.recordPipelineStall(PerfCounterId::STALL_EXECUTE_AMO_WAIT);
            ++result.amo_wait_slots;
            LOGT(EXECUTE,
                 "inst=%" PRId64 " AMO waits on earlier uncommitted store-like op, delay issue",
                 entry.instruction->get_instruction_id());
            continue;
        }

        ExecutionUnit* unit = executionUnit(state, chosen_unit_type, *chosen_unit_index);
        if (!unit || unit->busy) {
            state.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_UNIT);
            ++result.no_unit_slots;
            LOGT(EXECUTE,
                 "inst=%" PRId64 " selected %s%zu but execution unit is unavailable",
                 entry.instruction->get_instruction_id(),
                 unitTypeName(chosen_unit_type),
                 *chosen_unit_index);
            continue;
        }

        maybeMarkAddrUnknownSpeculation(state, entry.instruction, addr_unknown_store_snapshot);
        entry.instruction->set_status(DynamicInst::Status::EXECUTING);
        auto& exec_info = entry.instruction->get_execution_info();
        exec_info.remaining_cycles = exec_info.execution_cycles;
        startExecutionUnit(*unit, entry.instruction);

        result.selected.push_back(
            {entry.instruction, entry.rs_entry, chosen_unit_type, *chosen_unit_index, unit});
        LOGT(EXECUTE, "issue select inst=%" PRId64 " from rs[%d] to %s%zu",
             entry.instruction->get_instruction_id(),
             entry.rs_entry,
             unitTypeName(chosen_unit_type),
             *chosen_unit_index);
    }

    const size_t classified_slots =
        result.selected.size() + result.amo_wait_slots + result.no_unit_slots;
    if (classified_slots == 0) {
        LOGT(EXECUTE, "no ready instruction selected for execution");
        state.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_READY);

        if (ExecuteMemoryInflight::hasAny(state)) {
            state.perf_counters.increment(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED);
        } else if (state.reservation_station->get_occupied_entry_count() == 0) {
            state.perf_counters.increment(PerfCounterId::STALL_EXECUTE_FRONTEND_STARVED);
        } else if (ready_entries.empty()) {
            state.perf_counters.increment(PerfCounterId::STALL_EXECUTE_DEPENDENCY_BLOCKED);
        } else {
            state.perf_counters.increment(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED);
        }
    }

    classifyEmptySlots(
        state, result, issue_width, classified_slots, classified_slots == 0);

    state.perf_counters.increment(PerfCounterId::ISSUE_UTILIZED_SLOTS, result.selected.size());
    state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_EXECUTED, result.selected.size());
    state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_NO_UNIT, result.no_unit_slots);
    state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_AMO_WAIT, result.amo_wait_slots);
    state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_RESOURCE_BLOCKED,
                                  result.resource_blocked_slots);
    state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_FRONTEND_EMPTY,
                                  result.frontend_empty_slots);
    state.perf_counters.increment(PerfCounterId::TOPDOWN_SLOTS_DEP_BLOCKED,
                                  result.dependency_blocked_slots);
    return result;
}

} // namespace riscv
