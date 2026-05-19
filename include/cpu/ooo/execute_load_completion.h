#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class ExecuteLoadCompletion {
public:
    enum class Result {
        Completed,
        Deferred,
    };

    static Result perform(ExecutionUnit& unit, size_t unit_index, CPUState& state);
};

} // namespace riscv
