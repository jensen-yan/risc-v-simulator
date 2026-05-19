#include "cpu/ooo/execute_load_completion.h"
#include "cpu/ooo/execute_host_comm_access.h"
#include "cpu/ooo/execute_load_access.h"
#include "cpu/ooo/execute_load_hazard.h"
#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_memory_order.h"
#include "common/debug_types.h"

namespace riscv {

ExecuteLoadCompletion::Result ExecuteLoadCompletion::perform(ExecutionUnit& unit,
                                                             size_t unit_index,
                                                             CPUState& state) {
    if (ExecuteHostCommAccess::mustSerialize(
            state, unit.instruction, unit.load_address, unit.load_size)) {
        auto blocked_inst = unit.instruction;
        blocked_inst->set_status(DynamicInst::Status::ISSUED);
        state.reservation_station->release_execution_unit(
            ExecutionUnitType::LOAD, static_cast<int>(unit_index));
        resetExecutionUnitState(unit);
        LOGT(EXECUTE,
             "inst=%" PRId64 " LOAD%zu waits for ROB head before host-comm access",
             blocked_inst->get_instruction_id(),
             unit_index);
        blocked_inst->get_memory_info().replay_count++;
        ExecuteMemoryOrder::recordLoadReplayReason(
            blocked_inst, state, PerfCounterId::LOAD_REPLAYS_HOST_COMM);
        return Result::Deferred;
    }

    if (ExecuteLoadHazard::handleEarlierStoreHazard(unit, unit_index, state) ==
        ExecuteLoadHazard::Decision::Replayed) {
        return Result::Deferred;
    }

    const auto load_result = ExecuteLoadAccess::perform(unit, state);
    if (load_result == ExecuteLoadAccess::Result::BlockedByStore) {
        auto blocked_inst = unit.instruction;
        blocked_inst->set_status(DynamicInst::Status::ISSUED);
        state.reservation_station->release_execution_unit(
            ExecutionUnitType::LOAD, static_cast<int>(unit_index));
        resetExecutionUnitState(unit);
        LOGT(EXECUTE,
             "inst=%" PRId64 " LOAD%zu blocked by older store overlap, replay and release load unit",
             blocked_inst->get_instruction_id(),
             unit_index);
        blocked_inst->get_memory_info().replay_count++;
        ExecuteMemoryOrder::recordLoadReplayReason(
            blocked_inst, state, PerfCounterId::LOAD_REPLAYS_STORE_BUFFER_OVERLAP);
        return Result::Deferred;
    }

    if (load_result == ExecuteLoadAccess::Result::WaitingForCache) {
        if (unit.dcache.request_sent &&
            ExecuteMemoryInflight::tryMove(unit, ExecutionUnitType::LOAD, unit_index, state)) {
            return Result::Deferred;
        }

        if (!unit.dcache.request_sent) {
            auto blocked_inst = unit.instruction;
            blocked_inst->set_status(DynamicInst::Status::ISSUED);
            state.reservation_station->release_execution_unit(
                ExecutionUnitType::LOAD, static_cast<int>(unit_index));
            resetExecutionUnitState(unit);
            LOGT(EXECUTE,
                 "inst=%" PRId64 " LOAD%zu blocked by dcache outstanding limit, release and retry",
                 blocked_inst->get_instruction_id(),
                 unit_index);
            return Result::Deferred;
        }

        LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu waiting for dcache, remaining=%d",
             unit.instruction->get_instruction_id(),
             unit_index,
             unit.remaining_cycles);
        return Result::Deferred;
    }

    if (load_result == ExecuteLoadAccess::Result::Exception) {
        LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu raised exception: %s",
             unit.instruction->get_instruction_id(),
             unit_index,
             unit.exception_msg.c_str());
        ExecuteMemoryOrder::recordLoadReplayBucket(unit.instruction, state);
        return Result::Completed;
    }

    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu done, %s result=0x%" PRIx64 " -> CDB",
         unit.instruction->get_instruction_id(),
         unit_index,
         (load_result == ExecuteLoadAccess::Result::Forwarded ? "(store-forwarded)"
                                                              : "(loaded-from-memory)"),
         unit.result);

    ExecuteMemoryOrder::recordLoadReplayBucket(unit.instruction, state);

    return Result::Completed;
}

} // namespace riscv
