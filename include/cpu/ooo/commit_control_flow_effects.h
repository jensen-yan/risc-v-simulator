#pragma once

#include "cpu/ooo/cpu_state.h"
#include "cpu/ooo/ooo_recovery.h"

namespace riscv {

class CommitControlFlowEffects {
public:
    struct Result {
        bool is_control_flow = false;
        bool needs_redirect_flush = false;
        uint64_t redirect_pc = 0;
        OooRecovery::Reason flush_reason = OooRecovery::Reason::Other;
    };

    static Result apply(CPUState& state, const DynamicInstPtr& instruction);
};

} // namespace riscv
