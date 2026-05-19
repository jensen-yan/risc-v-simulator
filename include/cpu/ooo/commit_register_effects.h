#pragma once

#include "cpu/ooo/cpu_state.h"

#include <string>

namespace riscv {

class CommitRegisterEffects {
public:
    struct Result {
        bool success = true;
        bool applied = false;
        std::string error_message;
    };

    static Result apply(CPUState& state, const DynamicInstPtr& instruction);

private:
    static Result applyInteger(CPUState& state, const DynamicInstPtr& instruction);
    static Result applyFloatingPoint(CPUState& state, const DynamicInstPtr& instruction);
};

} // namespace riscv
