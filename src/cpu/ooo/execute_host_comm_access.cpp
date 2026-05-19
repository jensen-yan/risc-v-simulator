#include "cpu/ooo/execute_host_comm_access.h"

namespace riscv {

namespace {

bool rangesOverlap(uint64_t lhs_addr, uint64_t lhs_size, uint64_t rhs_addr, uint64_t rhs_size) {
    const uint64_t lhs_end = lhs_addr + lhs_size - 1;
    const uint64_t rhs_end = rhs_addr + rhs_size - 1;
    return lhs_addr <= rhs_end && rhs_addr <= lhs_end;
}

} // namespace

bool ExecuteHostCommAccess::isAccess(const CPUState& state, uint64_t address, uint8_t size) {
    if (!state.memory || size == 0) {
        return false;
    }

    return rangesOverlap(address, size, state.memory->getTohostAddr(), 8) ||
           rangesOverlap(address, size, state.memory->getFromhostAddr(), 8);
}

bool ExecuteHostCommAccess::mustSerialize(const CPUState& state,
                                          const DynamicInstPtr& instruction,
                                          uint64_t address,
                                          uint8_t size) {
    if (!instruction || !isAccess(state, address, size) || !state.reorder_buffer) {
        return false;
    }

    return !state.reorder_buffer->is_head_instruction(instruction->get_instruction_id());
}

} // namespace riscv
