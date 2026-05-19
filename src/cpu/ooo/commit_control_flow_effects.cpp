#include "cpu/ooo/commit_control_flow_effects.h"

#include "common/debug_types.h"
#include "cpu/ooo/branch_predictor.h"

namespace riscv {

namespace {

enum class JalrProfileKind {
    ReturnLike,
    CallLike,
    Other,
};

bool isLinkRegister(RegNum reg) {
    return reg == 1 || reg == 5;
}

JalrProfileKind classifyJalrKind(const DecodedInstruction& decoded) {
    if (decoded.rd == 0 && decoded.imm == 0 && isLinkRegister(decoded.rs1)) {
        return JalrProfileKind::ReturnLike;
    }
    if (isLinkRegister(decoded.rd)) {
        return JalrProfileKind::CallLike;
    }
    return JalrProfileKind::Other;
}

bool isControlFlow(const DecodedInstruction& decoded) {
    return decoded.opcode == Opcode::BRANCH ||
           decoded.opcode == Opcode::JAL ||
           decoded.opcode == Opcode::JALR;
}

void recordBranchProfile(CPUState& state,
                         const DynamicInstPtr& instruction,
                         uint64_t instruction_pc,
                         uint64_t fallthrough,
                         uint64_t predicted_next_pc,
                         bool correct) {
    const BranchPredictor::BranchMeta* branch_meta = nullptr;
    if (instruction->has_branch_predict_meta()) {
        branch_meta = &instruction->get_branch_predict_meta();
    }

    auto& profile = state.branch_profiles[instruction_pc];
    profile.executions++;
    if (instruction->is_jump()) {
        profile.taken++;
    }
    if (predicted_next_pc != fallthrough) {
        profile.predicted_taken++;
    }
    if (!correct) {
        profile.mispredicts++;
    }

    if (branch_meta && branch_meta->valid) {
        const bool actual_taken = instruction->is_jump();
        const bool local_correct = (branch_meta->local_pred_taken == actual_taken);
        const bool global_correct = (branch_meta->global_pred_taken == actual_taken);
        const bool chooser_correct = correct;

        if (local_correct) {
            profile.local_correct++;
            state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_LOCAL_CORRECT);
        } else {
            profile.local_incorrect++;
            state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_LOCAL_INCORRECT);
        }

        if (global_correct) {
            profile.global_correct++;
            state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_GLOBAL_CORRECT);
        } else {
            profile.global_incorrect++;
            state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_GLOBAL_INCORRECT);
        }

        if (branch_meta->chooser_use_global) {
            profile.chooser_selected_global++;
        } else {
            profile.chooser_selected_local++;
        }

        if (chooser_correct) {
            profile.chooser_correct++;
        } else {
            profile.chooser_incorrect++;
        }

        if (local_correct && global_correct) {
            profile.both_correct++;
        } else if (!local_correct && !global_correct) {
            profile.both_incorrect++;
        } else {
            const bool chosen_component_correct =
                branch_meta->chooser_use_global ? global_correct : local_correct;
            if (!chosen_component_correct) {
                profile.chooser_misses++;
            }
        }

        if (branch_meta->loop_override_used) {
            profile.loop_override_used++;
            if (chooser_correct) {
                profile.loop_override_correct++;
                state.perf_counters.increment(PerfCounterId::PREDICTOR_LOOP_CORRECT);
            } else {
                profile.loop_override_incorrect++;
                state.perf_counters.increment(PerfCounterId::PREDICTOR_LOOP_INCORRECT);
            }
        }
    }

    if (correct) {
        state.perf_counters.increment(PerfCounterId::PREDICTOR_BHT_CORRECT);
    } else {
        state.perf_counters.increment(PerfCounterId::PREDICTOR_BHT_INCORRECT);
        state.recordBranchMispredict();
    }
}

void recordJalrProfile(CPUState& state,
                       const DecodedInstruction& decoded,
                       uint64_t instruction_pc,
                       uint64_t fallthrough,
                       uint64_t predicted_next_pc,
                       bool correct) {
    if (!correct) {
        state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_MISPREDICTS);

        switch (classifyJalrKind(decoded)) {
            case JalrProfileKind::ReturnLike:
                state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_RETURN_MISPREDICTS);
                break;
            case JalrProfileKind::CallLike:
                state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_CALL_MISPREDICTS);
                break;
            case JalrProfileKind::Other:
                state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_OTHER_MISPREDICTS);
                break;
        }

        if (predicted_next_pc == fallthrough) {
            state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_FALLTHROUGH_MISPREDICTS);
        } else {
            state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_WRONG_TARGET_MISPREDICTS);
        }
    }

    auto& profile = state.jalr_profiles[instruction_pc];
    profile.executions++;
    if (!correct) {
        profile.mispredicts++;
        if (predicted_next_pc == fallthrough) {
            profile.predicted_fallthrough++;
        } else {
            profile.wrong_target++;
        }
    }

    switch (classifyJalrKind(decoded)) {
        case JalrProfileKind::ReturnLike:
            profile.return_like++;
            break;
        case JalrProfileKind::CallLike:
            profile.call_like++;
            break;
        case JalrProfileKind::Other:
            profile.other++;
            break;
    }
}

void updatePredictor(CPUState& state,
                     const DynamicInstPtr& instruction,
                     uint64_t instruction_pc,
                     uint64_t actual_next_pc,
                     bool correct) {
    if (!state.branch_predictor) {
        return;
    }

    const auto& decoded = instruction->get_decoded_info();
    const bool actual_taken = instruction->is_jump();
    const BranchPredictor::BranchMeta* branch_meta = nullptr;
    if (decoded.opcode == Opcode::BRANCH && instruction->has_branch_predict_meta()) {
        branch_meta = &instruction->get_branch_predict_meta();
    }
    state.branch_predictor->update(instruction_pc, decoded, actual_taken, actual_next_pc, branch_meta);

    if (!correct && decoded.opcode == Opcode::BRANCH &&
        !instruction->is_control_recovered_early()) {
        state.branch_predictor->recover_after_branch_mispredict(
            instruction_pc, actual_taken, branch_meta);
        state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_RECOVERIES);
    }
}

} // namespace

CommitControlFlowEffects::Result CommitControlFlowEffects::apply(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    Result result;
    if (!instruction) {
        return result;
    }

    const auto& decoded = instruction->get_decoded_info();
    if (!isControlFlow(decoded)) {
        return result;
    }
    result.is_control_flow = true;

    const uint64_t instruction_pc = instruction->get_pc();
    const uint64_t fallthrough = instruction_pc + (decoded.is_compressed ? 2ULL : 4ULL);
    const uint64_t actual_next_pc = instruction->is_jump()
                                        ? instruction->get_jump_target()
                                        : fallthrough;
    const uint64_t predicted_next_pc = instruction->has_predicted_next_pc()
                                           ? instruction->get_predicted_next_pc()
                                           : fallthrough;
    const bool correct = (predicted_next_pc == actual_next_pc);

    if (correct) {
        state.perf_counters.increment(PerfCounterId::PREDICTOR_CONTROL_CORRECT);
    } else {
        state.perf_counters.increment(PerfCounterId::PREDICTOR_CONTROL_INCORRECT);
    }

    if (instruction->is_jump()) {
        state.perf_counters.increment(PerfCounterId::CONTROL_REDIRECTS);
        if (decoded.opcode == Opcode::JAL || decoded.opcode == Opcode::JALR) {
            state.perf_counters.increment(PerfCounterId::UNCONDITIONAL_REDIRECTS);
        }
    }

    if (decoded.opcode == Opcode::BRANCH) {
        recordBranchProfile(state, instruction, instruction_pc, fallthrough, predicted_next_pc, correct);
    }
    if (decoded.opcode == Opcode::JALR) {
        recordJalrProfile(state, decoded, instruction_pc, fallthrough, predicted_next_pc, correct);
    }

    updatePredictor(state, instruction, instruction_pc, actual_next_pc, correct);

    if (!correct && !instruction->is_control_recovered_early()) {
        result.needs_redirect_flush = true;
        result.redirect_pc = actual_next_pc;
        result.flush_reason = (decoded.opcode == Opcode::BRANCH)
                                  ? OooRecovery::Reason::BranchMispredict
                                  : OooRecovery::Reason::UnconditionalRedirect;
        LOGT(COMMIT,
             "inst=%" PRId64 " control-flow mispredict: pc=0x%" PRIx64
             " predicted_next=0x%" PRIx64 " actual_next=0x%" PRIx64 " -> redirect+flush",
             instruction->get_instruction_id(), instruction_pc, predicted_next_pc, actual_next_pc);
    } else if (!correct) {
        LOGT(COMMIT,
             "inst=%" PRId64 " control-flow mispredict already recovered early: pc=0x%" PRIx64
             " predicted_next=0x%" PRIx64 " actual_next=0x%" PRIx64,
             instruction->get_instruction_id(), instruction_pc, predicted_next_pc, actual_next_pc);
    } else {
        LOGT(COMMIT,
             "inst=%" PRId64 " control-flow correct: pc=0x%" PRIx64
             " next=0x%" PRIx64,
             instruction->get_instruction_id(), instruction_pc, actual_next_pc);
    }

    return result;
}

} // namespace riscv
