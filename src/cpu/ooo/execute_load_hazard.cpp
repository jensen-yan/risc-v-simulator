#include "cpu/ooo/execute_load_hazard.h"
#include "cpu/ooo/execute_memory_order.h"
#include "common/debug_types.h"

namespace riscv {

ExecuteLoadHazard::Decision ExecuteLoadHazard::handleEarlierStoreHazard(
    ExecutionUnit& unit,
    size_t unit_index,
    CPUState& state) {
    const auto hazard_info = state.reorder_buffer->get_earlier_store_hazard_info(
        unit.instruction->get_instruction_id(), unit.load_address, unit.load_size);
    const auto hazard_kind = hazard_info.kind;

    if (hazard_kind == ReorderBuffer::StoreHazardKind::None) {
        return Decision::ContinueExecution;
    }

    auto blocked_inst = unit.instruction;
    const bool speculated_past_addr_unknown =
        hazard_kind == ReorderBuffer::StoreHazardKind::AddressUnknown &&
        blocked_inst->get_memory_info().speculated_past_addr_unknown_store;

    if (speculated_past_addr_unknown) {
        LOGT(EXECUTE,
             "inst=%" PRId64 " LOAD%zu speculates past older STORE with unresolved address",
             blocked_inst->get_instruction_id(), unit_index);
    } else {
        blocked_inst->set_status(DynamicInst::Status::ISSUED);
        state.reservation_station->release_execution_unit(
            ExecutionUnitType::LOAD, static_cast<int>(unit_index));
        resetExecutionUnitState(unit);
        LOGT(EXECUTE,
             "inst=%" PRId64 " LOAD%zu waits on earlier STORE, replay and release load unit",
             blocked_inst->get_instruction_id(), unit_index);
        blocked_inst->get_memory_info().replay_count++;
    }

    switch (hazard_kind) {
        case ReorderBuffer::StoreHazardKind::Amo:
            if (!speculated_past_addr_unknown) {
                ExecuteMemoryOrder::recordLoadReplayReason(
                    blocked_inst, state, PerfCounterId::LOAD_REPLAYS_ROB_STORE_AMO);
            }
            break;
        case ReorderBuffer::StoreHazardKind::AddressUnknown:
            if (!speculated_past_addr_unknown && hazard_info.instruction) {
                hazard_info.instruction->get_memory_info().caused_rob_addr_unknown_block_count++;
            }
            if (!speculated_past_addr_unknown) {
                ExecuteMemoryOrder::recordLoadReplayReason(
                    blocked_inst, state, PerfCounterId::LOAD_REPLAYS_ROB_STORE_ADDR_UNKNOWN);
            }
            break;
        case ReorderBuffer::StoreHazardKind::Overlap:
            if (!speculated_past_addr_unknown && hazard_info.instruction) {
                hazard_info.instruction->get_memory_info().caused_rob_overlap_block_count++;
            }
            if (!speculated_past_addr_unknown) {
                ExecuteMemoryOrder::recordLoadReplayReason(
                    blocked_inst, state, PerfCounterId::LOAD_REPLAYS_ROB_STORE_OVERLAP);
            }
            break;
        case ReorderBuffer::StoreHazardKind::None:
            break;
    }

    return speculated_past_addr_unknown ? Decision::ContinueExecution : Decision::Replayed;
}

} // namespace riscv
