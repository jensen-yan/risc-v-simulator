#pragma once

#include "cpu/ooo/cpu_state.h"

#include <string>

namespace riscv {

class CommitMemoryEffects {
public:
    struct Result {
        bool success = true;
        bool applied = false;
        std::string error_message;
    };

    static Result apply(CPUState& state, const DynamicInstPtr& instruction);

private:
    static Result applyStore(CPUState& state, const DynamicInstPtr& instruction);
    static Result applyAmo(CPUState& state, const DynamicInstPtr& instruction);
};

} // namespace riscv
