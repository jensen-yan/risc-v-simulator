#include "cpu/ooo/execute_memory_order.h"

#include "common/debug_types.h"

#include <queue>

namespace riscv {

namespace {

bool rangesOverlap(uint64_t lhs_addr, uint64_t lhs_size, uint64_t rhs_addr, uint64_t rhs_size) {
    const uint64_t lhs_end = lhs_addr + lhs_size - 1;
    const uint64_t rhs_end = rhs_addr + rhs_size - 1;
    return lhs_addr <= rhs_end && rhs_addr <= lhs_end;
}

template <typename Queue>
void clearQueue(Queue& queue) {
    while (!queue.empty()) {
        queue.pop();
    }
}

} // namespace

ExecuteMemoryOrder::AddrUnknownStoreSnapshot
ExecuteMemoryOrder::captureAddrUnknownStoreSnapshot(const CPUState& state) {
    AddrUnknownStoreSnapshot snapshot;
    if (!state.reorder_buffer) {
        return snapshot;
    }

    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        const auto rob_entry = static_cast<ROBEntry>(i);
        if (!state.reorder_buffer->is_entry_valid(rob_entry)) {
            continue;
        }

        const auto instruction = state.reorder_buffer->get_entry(rob_entry);
        if (!instruction || !instruction->is_store_instruction() || instruction->is_completed()) {
            continue;
        }

        const auto& memory_info = instruction->get_memory_info();
        if (memory_info.address_ready && memory_info.memory_size != 0) {
            continue;
        }

        snapshot.push_back({instruction->get_instruction_id(), instruction->get_pc()});
    }
    return snapshot;
}

std::optional<uint64_t> ExecuteMemoryOrder::findFirstOlderAddrUnknownStorePc(
    const AddrUnknownStoreSnapshot& snapshot, uint64_t instruction_id) {
    for (const auto& entry : snapshot) {
        if (entry.instruction_id < instruction_id) {
            return entry.pc;
        }
    }
    return std::nullopt;
}

bool ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(
    CPUState& state,
    const DynamicInstPtr& instruction,
    const AddrUnknownStoreSnapshot& snapshot) {
    if (!instruction || !instruction->is_load_instruction() || !state.reorder_buffer) {
        return false;
    }

    auto& memory_info = instruction->get_memory_info();
    const uint64_t load_pc = instruction->get_pc();
    std::optional<uint64_t> blocked_store_pc;
    for (const auto& entry : snapshot) {
        if (entry.instruction_id >= instruction->get_instruction_id()) {
            break;
        }
        if (state.isBlockedAddrUnknownPair(load_pc, entry.pc)) {
            blocked_store_pc = entry.pc;
            break;
        }
    }
    if (!blocked_store_pc.has_value()) {
        return false;
    }

    if (!memory_info.blocked_by_addr_unknown_pair) {
        memory_info.blocked_by_addr_unknown_pair = true;
        state.perf_counters.increment(PerfCounterId::LOADS_BLOCKED_ADDR_UNKNOWN_PAIR);
    }

    LOGT(EXECUTE,
         "inst=%" PRId64
         " load dispatch blocks addr-unknown speculation for bad pair load_pc=0x%" PRIx64
         " store_pc=0x%" PRIx64,
         instruction->get_instruction_id(),
         load_pc,
         *blocked_store_pc);
    return true;
}

bool ExecuteMemoryOrder::tryRecoverViolation(const DynamicInstPtr& store_instruction,
                                             CPUState& state) {
    if (!store_instruction || !store_instruction->is_store_instruction() || !state.reorder_buffer) {
        return false;
    }

    const auto& store_memory = store_instruction->get_memory_info();
    if (!store_memory.address_ready || store_memory.memory_size == 0) {
        return false;
    }

    DynamicInstPtr violating_load = nullptr;
    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        if (!state.reorder_buffer->is_entry_valid(static_cast<ROBEntry>(i))) {
            continue;
        }

        auto candidate = state.reorder_buffer->get_entry(static_cast<ROBEntry>(i));
        if (!candidate || candidate->get_instruction_id() <= store_instruction->get_instruction_id()) {
            continue;
        }
        if (!candidate->is_load_instruction()) {
            continue;
        }

        const auto& load_memory = candidate->get_memory_info();
        if (!load_memory.speculated_past_addr_unknown_store || !load_memory.address_ready ||
            load_memory.memory_size == 0) {
            continue;
        }
        if (!candidate->is_executing() && !candidate->is_completed()) {
            continue;
        }
        if (!rangesOverlap(store_memory.memory_address,
                           store_memory.memory_size,
                           load_memory.memory_address,
                           load_memory.memory_size)) {
            continue;
        }

        violating_load = std::move(candidate);
        break;
    }

    if (!violating_load) {
        return false;
    }

    state.load_profiles[violating_load->get_pc()].speculated_addr_unknown_violation++;
    state.store_profiles[store_instruction->get_pc()].caused_order_violation++;
    state.recordAddrUnknownPairViolation(violating_load->get_pc(), store_instruction->get_pc());
    state.trainLoadAddrUnknownPredictor(violating_load->get_pc(), false);

    uint64_t restart_pc = store_instruction->get_pc();
    const ROBEntry head_entry = state.reorder_buffer->get_head_entry();
    if (!state.reorder_buffer->is_empty() && state.reorder_buffer->is_entry_valid(head_entry)) {
        if (const auto head_inst = state.reorder_buffer->get_entry(head_entry)) {
            restart_pc = head_inst->get_pc();
        }
    }

    const uint64_t rob_used =
        static_cast<uint64_t>(ReorderBuffer::MAX_ROB_ENTRIES -
                              state.reorder_buffer->get_free_entry_count());
    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSHES);
    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_OTHER);
    state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES, rob_used);
    state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_OTHER, rob_used);
    state.perf_counters.increment(PerfCounterId::MEMORY_ORDER_VIOLATION_RECOVERIES);

    state.pc = restart_pc;
    clearQueue(state.fetch_buffer);
    clearQueue(state.cdb_queue);
    state.reservation_station->flush_pipeline();
    state.reorder_buffer->flush_pipeline();
    state.register_rename->flush_pipeline();
    state.rename_checkpoints.clear();
    state.store_buffer->flush();
    state.resetExecutionUnits();
    state.reservation_valid = false;
    state.reservation_addr = 0;
    if (state.l1i_cache) {
        state.l1i_cache->flushInFlight();
    }
    if (state.l1d_cache) {
        state.l1d_cache->flushInFlight();
    }
    state.icache.reset();
    if (state.branch_predictor) {
        state.branch_predictor->on_pipeline_flush();
    }

    LOGT(EXECUTE,
         "memory order violation recovery: store inst=%" PRId64 " pc=0x%" PRIx64
         " addr=0x%" PRIx64 " load inst=%" PRId64 " pc=0x%" PRIx64 " addr=0x%" PRIx64
         " restart_pc=0x%" PRIx64,
         store_instruction->get_instruction_id(),
         store_instruction->get_pc(),
         store_memory.memory_address,
         violating_load->get_instruction_id(),
         violating_load->get_pc(),
         violating_load->get_memory_info().memory_address,
         restart_pc);
    return true;
}

} // namespace riscv
