#include "cpu/ooo/ooo_recovery.h"

#include "common/debug_types.h"

#include <cinttypes>
#include <queue>
#include <utility>
#include <vector>

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

bool shouldDelayRedirectFetch(OooRecovery::Reason reason, bool has_explicit_pc) {
    if (has_explicit_pc) {
        return true;
    }

    switch (reason) {
        case OooRecovery::Reason::BranchMispredict:
        case OooRecovery::Reason::UnconditionalRedirect:
        case OooRecovery::Reason::Trap:
        case OooRecovery::Reason::Mret:
        case OooRecovery::Reason::FenceI:
        case OooRecovery::Reason::MemoryOrderViolation:
            return true;
        case OooRecovery::Reason::Exception:
        case OooRecovery::Reason::Other:
        default:
            return false;
    }
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
    if (request.has_restart_pc) {
        state.pc = request.restart_pc;
    }
    if (shouldDelayRedirectFetch(request.reason, request.has_restart_pc)) {
        state.startRedirectStall();
    }

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

    result.flushed_completion_events = state.completion_fabric.clear();

    if (request.flush_store_buffer && state.store_buffer) {
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

uint64_t OooRecovery::flushYoungerCompletionEvents(CPUState& state, uint64_t instruction_id) {
    return state.completion_fabric.flushYoungerThan(instruction_id);
}

bool OooRecovery::flushYoungerExecutionUnits(CPUState& state, const YoungerThanRequest& request) {
    bool flushed_dcache_request = false;

    auto flush_container = [&](auto& units, ExecutionUnitType unit_type) {
        for (size_t i = 0; i < units.size(); ++i) {
            auto& other_unit = units[i];
            if (!other_unit.busy || !other_unit.instruction) {
                continue;
            }
            if (unit_type == request.current_unit_type && i == request.current_unit_index) {
                continue;
            }
            if (other_unit.instruction->get_instruction_id() <= request.instruction_id) {
                continue;
            }

            if ((unit_type == ExecutionUnitType::LOAD || unit_type == ExecutionUnitType::STORE) &&
                other_unit.dcache.request_sent) {
                flushed_dcache_request = true;
            }

            LOGT(EXECUTE, "flush younger execution unit inst=%" PRId64,
                 other_unit.instruction->get_instruction_id());
            if (state.reservation_station) {
                state.reservation_station->release_execution_unit(unit_type, static_cast<int>(i));
            }
            resetExecutionUnitState(other_unit);
        }
    };

    flush_container(state.alu_units, ExecutionUnitType::ALU);
    flush_container(state.fp_units, ExecutionUnitType::FP);
    flush_container(state.branch_units, ExecutionUnitType::BRANCH);
    flush_container(state.load_units, ExecutionUnitType::LOAD);
    flush_container(state.store_units, ExecutionUnitType::STORE);

    for (auto& entry : state.memory_access_inflight) {
        if (!entry.valid || !entry.state.instruction) {
            continue;
        }
        if (entry.state.instruction->get_instruction_id() <= request.instruction_id) {
            continue;
        }

        flushed_dcache_request = true;
        LOGT(EXECUTE, "flush younger inflight memory access inst=%" PRId64,
             entry.state.instruction->get_instruction_id());
        resetMemoryAccessInFlightState(entry);
    }
    return flushed_dcache_request;
}

void OooRecovery::restoreRenameCheckpointForSurvivingWork(
    CPUState& state,
    uint64_t instruction_id,
    const RegisterRenameUnit::Checkpoint& checkpoint) {
    std::vector<PhysRegNum> surviving_live_regs;
    std::vector<PhysRegNum> surviving_live_fp_regs;
    surviving_live_regs.reserve(ReorderBuffer::MAX_ROB_ENTRIES);
    surviving_live_fp_regs.reserve(ReorderBuffer::MAX_ROB_ENTRIES);

    if (state.reorder_buffer) {
        for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
            const auto rob_entry = static_cast<ROBEntry>(i);
            if (!state.reorder_buffer->is_entry_valid(rob_entry)) {
                continue;
            }

            const auto live_entry = state.reorder_buffer->get_entry(rob_entry);
            if (!live_entry || live_entry->get_physical_dest_kind() == RegisterFileKind::None) {
                continue;
            }

            if (live_entry->get_physical_dest_kind() == RegisterFileKind::FloatingPoint) {
                surviving_live_fp_regs.push_back(live_entry->get_physical_dest());
            } else if (live_entry->get_physical_dest() != 0) {
                surviving_live_regs.push_back(live_entry->get_physical_dest());
            }
        }
    }

    if (state.register_rename) {
        state.register_rename->restore_checkpoint(
            checkpoint, surviving_live_regs, surviving_live_fp_regs);
    }

    for (auto it = state.rename_checkpoints.begin(); it != state.rename_checkpoints.end();) {
        if (it->first >= instruction_id) {
            it = state.rename_checkpoints.erase(it);
        } else {
            ++it;
        }
    }
}

OooRecovery::Result OooRecovery::recoverYoungerThan(CPUState& state,
                                                    const YoungerThanRequest& request) {
    LOGT(EXECUTE, "ooo recovery younger-than inst=%" PRIu64, request.instruction_id);

    Result result;
    if (request.has_redirect_pc) {
        state.pc = request.redirect_pc;
        state.startRedirectStall();
    }

    result.fetch_buffer_dropped = clearQueue(state.fetch_buffer);
    if (state.l1i_cache) {
        state.l1i_cache->flushInFlight();
    }
    state.icache.reset();

    if (state.reorder_buffer) {
        result.flushed_rob_entries = state.reorder_buffer->flush_after_entry(request.rob_entry);
    }
    if (state.reservation_station) {
        state.reservation_station->flush_younger_than(request.instruction_id);
    }
    if (state.store_buffer) {
        state.store_buffer->flush_after(request.instruction_id);
    }
    result.flushed_completion_events =
        flushYoungerCompletionEvents(state, request.instruction_id);
    result.flushed_l1d_inflight = flushYoungerExecutionUnits(state, request);
    if (result.flushed_l1d_inflight && state.l1d_cache) {
        state.l1d_cache->flushInFlight();
    }

    if (request.rename_checkpoint) {
        restoreRenameCheckpointForSurvivingWork(
            state, request.instruction_id, *request.rename_checkpoint);
    }

    LOGT(EXECUTE,
         "ooo recovery younger-than completed inst=%" PRIu64
         " flushed_rob=%" PRIu64 " flushed_completion=%" PRIu64,
         request.instruction_id,
         result.flushed_rob_entries,
         result.flushed_completion_events);
    return result;
}

} // namespace riscv
