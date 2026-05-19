#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class ExecuteStoreAccess {
public:
    enum class Result {
        Completed,
        WaitingForCache,
        MovedToInflight,
        BlockedByDCacheOutstanding,
        ReplayedForHostComm,
        RecoveryTriggered,
    };

    static Result perform(ExecutionUnit& unit, size_t unit_index, CPUState& state);
};

} // namespace riscv
