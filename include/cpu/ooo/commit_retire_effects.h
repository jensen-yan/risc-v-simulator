#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class CommitRetireEffects {
public:
    static void afterInstructionRetired(CPUState& state, const DynamicInstPtr& instruction);

private:
    static void retireStoreBufferAndRenameCheckpoint(CPUState& state,
                                                     const DynamicInstPtr& instruction);
    static void recordLoadProfile(CPUState& state, const DynamicInstPtr& instruction);
    static void recordStoreProfile(CPUState& state, const DynamicInstPtr& instruction);
};

} // namespace riscv
