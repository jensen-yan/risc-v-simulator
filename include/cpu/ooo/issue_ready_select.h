#pragma once

#include "cpu/ooo/cpu_state.h"

#include <cstddef>
#include <vector>

namespace riscv {

class IssueReadySelect {
public:
    struct SelectedInstruction {
        DynamicInstPtr instruction;
        RSEntry rs_entry = 0;
        ExecutionUnitType unit_type = ExecutionUnitType::ALU;
        size_t unit_index = 0;
        ExecutionUnit* unit = nullptr;
    };

    struct Result {
        std::vector<SelectedInstruction> selected;
        size_t amo_wait_slots = 0;
        size_t no_unit_slots = 0;
        size_t resource_blocked_slots = 0;
        size_t frontend_empty_slots = 0;
        size_t dependency_blocked_slots = 0;
    };

    static Result select(CPUState& state, size_t issue_width = OOOPipelineConfig::ISSUE_WIDTH);
};

} // namespace riscv
