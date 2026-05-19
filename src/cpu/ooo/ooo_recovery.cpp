#include "cpu/ooo/ooo_recovery.h"

#include "common/debug_types.h"

#include <cinttypes>

namespace riscv {

namespace {

template <typename Queue>
uint64_t clearQueue(Queue& queue) {
    uint64_t dropped = 0;
    while (!queue.empty()) {
        queue.pop();
        ++dropped;
    }
    return dropped;
}

uint64_t currentRobOccupancy(const CPUState& state) {
    if (!state.reorder_buffer) {
        return 0;
    }
    return static_cast<uint64_t>(
        ReorderBuffer::MAX_ROB_ENTRIES - state.reorder_buffer->get_free_entry_count());
}

} // namespace

const char* OooRecovery::reasonName(Reason reason) {
    switch (reason) {
        case Reason::BranchMispredict:
            return "branch_mispredict";
        case Reason::UnconditionalRedirect:
            return "unconditional_redirect";
        case Reason::Trap:
            return "trap";
        case Reason::Mret:
            return "mret";
        case Reason::FenceI:
            return "fencei";
        case Reason::MemoryOrderViolation:
            return "memory_order_violation";
        case Reason::Exception:
            return "exception";
        case Reason::Other:
        default:
            return "other";
    }
}

void OooRecovery::recordFlushCounters(CPUState& state,
                                      Reason reason,
                                      uint64_t flushed_rob_entries) {
    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSHES);
    state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES, flushed_rob_entries);

    switch (reason) {
        case Reason::BranchMispredict:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_BRANCH_MISPREDICT);
            state.perf_counters.increment(
                PerfCounterId::ROB_FLUSHED_ENTRIES_BRANCH_MISPREDICT, flushed_rob_entries);
            break;
        case Reason::UnconditionalRedirect:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_UNCONDITIONAL_REDIRECT);
            state.perf_counters.increment(
                PerfCounterId::ROB_FLUSHED_ENTRIES_UNCONDITIONAL_REDIRECT, flushed_rob_entries);
            break;
        case Reason::Trap:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_TRAP);
            state.perf_counters.increment(
                PerfCounterId::ROB_FLUSHED_ENTRIES_TRAP, flushed_rob_entries);
            break;
        case Reason::Mret:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_MRET);
            state.perf_counters.increment(
                PerfCounterId::ROB_FLUSHED_ENTRIES_MRET, flushed_rob_entries);
            break;
        case Reason::FenceI:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_FENCEI);
            state.perf_counters.increment(
                PerfCounterId::ROB_FLUSHED_ENTRIES_FENCEI, flushed_rob_entries);
            break;
        case Reason::Exception:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_EXCEPTION);
            state.perf_counters.increment(
                PerfCounterId::ROB_FLUSHED_ENTRIES_EXCEPTION, flushed_rob_entries);
            break;
        case Reason::MemoryOrderViolation:
        case Reason::Other:
        default:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_OTHER);
            state.perf_counters.increment(
                PerfCounterId::ROB_FLUSHED_ENTRIES_OTHER, flushed_rob_entries);
            break;
    }
}

OooRecovery::Result OooRecovery::recoverFullPipeline(CPUState& state,
                                                     const FullPipelineRequest& request) {
    LOGT(COMMIT, "ooo recovery full-pipeline reason=%s", reasonName(request.reason));

    Result result;
    result.flushed_rob_entries = currentRobOccupancy(state);
    recordFlushCounters(state, request.reason, result.flushed_rob_entries);

    if (state.branch_predictor && request.reason != Reason::BranchMispredict) {
        state.branch_predictor->on_pipeline_flush();
    }

    result.fetch_buffer_dropped = clearQueue(state.fetch_buffer);

    if (state.reservation_station) {
        state.reservation_station->flush_pipeline();
    }
    if (state.reorder_buffer) {
        state.reorder_buffer->flush_pipeline();
    }
    if (state.register_rename) {
        state.register_rename->flush_pipeline();
    }
    state.rename_checkpoints.clear();

    result.flushed_cdb_entries = clearQueue(state.cdb_queue);

    if (state.store_buffer) {
        state.store_buffer->flush();
    }

    if (state.l1i_cache) {
        if (request.reason == Reason::FenceI) {
            state.l1i_cache->reset();
        } else {
            state.l1i_cache->flushInFlight();
        }
    }
    if (state.l1d_cache) {
        state.l1d_cache->flushInFlight();
        result.flushed_l1d_inflight = true;
    }
    state.icache.reset();

    if (request.clear_reservation) {
        state.reservation_valid = false;
        state.reservation_addr = 0;
    }

    if (request.reset_execution_units) {
        state.resetExecutionUnits();
    }

    LOGT(COMMIT, "ooo recovery full-pipeline completed reason=%s flushed_rob=%" PRIu64,
         reasonName(request.reason), result.flushed_rob_entries);
    return result;
}

} // namespace riscv
