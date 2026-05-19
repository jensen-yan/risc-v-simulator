#pragma once

#include "cpu/ooo/cpu_state.h"
#include "cpu/ooo/ooo_recovery.h"

namespace riscv {

class CommitSystemEffects {
public:
    struct Result {
        bool applied = false;
        bool should_stop_commit = false;
        bool has_flush_summary = false;
        OooRecovery::Reason flush_reason = OooRecovery::Reason::Other;
        bool has_redirect_pc = false;
        uint64_t redirect_pc = 0;
    };

    static Result apply(CPUState& state, const DynamicInstPtr& instruction);
    static void enterMachineTrap(CPUState& state, uint64_t instruction_pc, uint64_t cause, uint64_t tval);
    static void flushPipelineAfterCommit(CPUState& state, OooRecovery::Reason reason);

private:
    static bool handleEcall(CPUState& state, uint64_t instruction_pc);
    static bool handleEbreak(CPUState& state, uint64_t instruction_pc);
    static bool handleMret(CPUState& state);
    static bool handleFenceI(CPUState& state, uint64_t instruction_pc, bool is_compressed);
};

} // namespace riscv
