#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class ExecuteLoadHazard {
public:
    enum class Decision {
        ContinueExecution,
        Replayed,
    };

    static Decision handleEarlierStoreHazard(ExecutionUnit& unit,
                                             size_t unit_index,
                                             CPUState& state);
};

} // namespace riscv
