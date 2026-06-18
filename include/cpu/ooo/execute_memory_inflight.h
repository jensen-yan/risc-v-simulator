#pragma once

#include "cpu/ooo/cpu_state.h"

#include <functional>

namespace riscv {

class ExecuteMemoryInflight {
public:
    using CompletionCallback = std::function<bool(ExecutionUnit&, ExecutionUnitType)>;

    static bool hasAny(const CPUState& state);
    static bool tryMove(ExecutionUnit& unit,
                        ExecutionUnitType unit_type,
                        size_t unit_index,
                        CPUState& state);
    static void advance(CPUState& state, const CompletionCallback& complete);
};

} // namespace riscv
