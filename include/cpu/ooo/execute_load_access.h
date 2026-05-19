#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class ExecuteLoadAccess {
public:
    enum class Result {
        Forwarded,
        LoadedFromMemory,
        WaitingForCache,
        BlockedByStore,
        Exception,
    };

    static Result perform(ExecutionUnit& unit, CPUState& state);
};

} // namespace riscv
