#pragma once

#include "cpu/ooo/cpu_state.h"

#include <string>

namespace riscv {

class CommitMemoryEffects {
public:
    struct Result {
        bool success = true;
        bool applied = false;
        bool used_store_memory_port = false;
        std::string error_message;
    };

    static bool usesStoreMemoryPort(const DynamicInstPtr& instruction);
    static Result apply(CPUState& state, const DynamicInstPtr& instruction);

private:
    static Result applyStore(CPUState& state, const DynamicInstPtr& instruction);
    static Result applyAmo(CPUState& state, const DynamicInstPtr& instruction);
};

} // namespace riscv
