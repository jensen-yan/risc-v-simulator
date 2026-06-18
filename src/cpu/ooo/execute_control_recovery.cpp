#include "cpu/ooo/execute_control_recovery.h"

#include "common/debug_types.h"
#include "cpu/ooo/branch_predictor.h"
#include "cpu/ooo/ooo_recovery.h"

namespace riscv {

bool ExecuteControlRecovery::tryRecoverEarly(ExecutionUnit& unit,
                                             ExecutionUnitType current_unit_type,
                                             size_t current_unit_index,
                                             CPUState& state) {
    if (!unit.instruction || unit.instruction->has_exception() || unit.instruction->has_trap()) {
        return false;
    }

    const auto& instruction = unit.instruction;
    const auto& decoded_info = instruction->get_decoded_info();
    if (decoded_info.opcode != Opcode::BRANCH && decoded_info.opcode != Opcode::JALR) {
        return false;
    }

    const uint64_t instruction_pc = instruction->get_pc();
    const uint64_t instruction_id = instruction->get_instruction_id();
    const uint64_t fallthrough = instruction_pc + (decoded_info.is_compressed ? 2ULL : 4ULL);
    const uint64_t actual_next_pc = instruction->is_jump() ? instruction->get_jump_target() : fallthrough;
    const uint64_t predicted_next_pc =
        instruction->has_predicted_next_pc() ? instruction->get_predicted_next_pc() : fallthrough;
    if (predicted_next_pc == actual_next_pc) {
        return false;
    }

    const auto checkpoint_it = state.rename_checkpoints.find(instruction_id);
    if (checkpoint_it == state.rename_checkpoints.end()) {
        LOGW(EXECUTE,
             "missing rename checkpoint for early control recovery, inst=%" PRId64 " pc=0x%" PRIx64,
             instruction_id, instruction_pc);
        return false;
    }

    const auto rename_checkpoint = checkpoint_it->second;
    OooRecovery::YoungerThanRequest recovery_request;
    recovery_request.instruction_id = instruction_id;
    recovery_request.rob_entry = instruction->get_rob_entry();
    recovery_request.current_unit_type = current_unit_type;
    recovery_request.current_unit_index = current_unit_index;
    recovery_request.has_redirect_pc = true;
    recovery_request.redirect_pc = actual_next_pc;
    recovery_request.rename_checkpoint = &rename_checkpoint;
    const auto recovery_result = OooRecovery::recoverYoungerThan(state, recovery_request);

    if (state.branch_predictor && instruction->has_ras_checkpoint()) {
        state.branch_predictor->restoreRasCheckpoint(instruction->get_ras_checkpoint());
        state.branch_predictor->applyResolvedControlToSpeculativeRas(
            instruction_pc, decoded_info, instruction->is_jump());
    }

    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSHES);
    state.perf_counters.increment(
        PerfCounterId::ROB_FLUSHED_ENTRIES, recovery_result.flushed_rob_entries);
    if (decoded_info.opcode == Opcode::BRANCH) {
        state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_BRANCH_MISPREDICT);
        state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_BRANCH_MISPREDICT,
                                      recovery_result.flushed_rob_entries);
        if (state.branch_predictor) {
            const BranchPredictor::BranchMeta* branch_meta =
                instruction->has_branch_predict_meta() ? &instruction->get_branch_predict_meta() : nullptr;
            state.branch_predictor->recover_after_branch_mispredict(
                instruction_pc, instruction->is_jump(), branch_meta);
            state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_RECOVERIES);
        }
    } else {
        state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_UNCONDITIONAL_REDIRECT);
        state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_UNCONDITIONAL_REDIRECT,
                                      recovery_result.flushed_rob_entries);
        if (state.branch_predictor) {
            state.branch_predictor->on_pipeline_flush();
        }
    }

    instruction->mark_control_recovered_early();
    LOGT(EXECUTE,
         "early control recovery: inst=%" PRId64 " pc=0x%" PRIx64
         " predicted_next=0x%" PRIx64 " actual_next=0x%" PRIx64
         " flushed_rob=%zu flushed_completion=%zu",
         instruction_id,
         instruction_pc,
         predicted_next_pc,
         actual_next_pc,
         static_cast<size_t>(recovery_result.flushed_rob_entries),
         static_cast<size_t>(recovery_result.flushed_completion_events));
    return true;
}

} // namespace riscv
