#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class ExecuteControlRecovery {
public:
    static bool tryRecoverEarly(ExecutionUnit& unit,
                                ExecutionUnitType current_unit_type,
                                size_t current_unit_index,
                                CPUState& state);
};

} // namespace riscv
