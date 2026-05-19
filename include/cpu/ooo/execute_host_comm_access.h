#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class ExecuteHostCommAccess {
public:
    static bool isAccess(const CPUState& state, uint64_t address, uint8_t size);
    static bool mustSerialize(const CPUState& state,
                              const DynamicInstPtr& instruction,
                              uint64_t address,
                              uint8_t size);
};

} // namespace riscv
