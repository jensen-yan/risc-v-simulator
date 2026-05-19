#include "cpu/ooo/execute_store_access.h"
#include "cpu/ooo/execute_dcache_access.h"
#include "cpu/ooo/execute_host_comm_access.h"
#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_memory_order.h"
#include "common/debug_types.h"

namespace riscv {

ExecuteStoreAccess::Result ExecuteStoreAccess::perform(ExecutionUnit& unit,
                                                       size_t unit_index,
                                                       CPUState& state) {
    if (ExecuteHostCommAccess::mustSerialize(
            state, unit.instruction, unit.load_address, unit.load_size)) {
        auto blocked_inst = unit.instruction;
        blocked_inst->set_status(DynamicInst::Status::ISSUED);
        state.reservation_station->release_execution_unit(
            ExecutionUnitType::STORE, static_cast<int>(unit_index));
        resetExecutionUnitState(unit);
        LOGT(EXECUTE,
             "inst=%" PRId64 " STORE%zu waits for ROB head before host-comm access",
             blocked_inst->get_instruction_id(),
             unit_index);
        return Result::ReplayedForHostComm;
    }

    if (!ExecuteDCacheAccess::startOrWait(
            unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE)) {
        if (unit.dcache.request_sent &&
            ExecuteMemoryInflight::tryMove(unit, ExecutionUnitType::STORE, unit_index, state)) {
            return Result::MovedToInflight;
        }

        if (!unit.dcache.request_sent) {
            auto blocked_inst = unit.instruction;
            blocked_inst->set_status(DynamicInst::Status::ISSUED);
            state.reservation_station->release_execution_unit(
                ExecutionUnitType::STORE, static_cast<int>(unit_index));
            resetExecutionUnitState(unit);
            LOGT(EXECUTE,
                 "inst=%" PRId64 " STORE%zu blocked by dcache outstanding limit, release and retry",
                 blocked_inst->get_instruction_id(),
                 unit_index);
            return Result::BlockedByDCacheOutstanding;
        }

        LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu waiting for dcache, remaining=%d",
             unit.instruction->get_instruction_id(),
             unit_index,
             unit.remaining_cycles);
        return Result::WaitingForCache;
    }

    unit.result = 0;

    LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu done, notify ROB",
         unit.instruction->get_instruction_id(),
         unit_index);

    if (ExecuteMemoryOrder::tryRecoverViolation(unit.instruction, state)) {
        return Result::RecoveryTriggered;
    }

    return Result::Completed;
}

} // namespace riscv
