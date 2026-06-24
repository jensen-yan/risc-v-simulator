#include "cpu/ooo/execute_store_access.h"
#include "cpu/ooo/execute_dcache_access.h"
#include "cpu/ooo/execute_host_comm_access.h"
#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_memory_order.h"
#include "common/debug_types.h"

namespace riscv {

namespace {

void requeueInternalStoreOp(ExecutionUnit& unit) {
    auto instruction = unit.instruction;
    if (instruction) {
        instruction->set_status(DynamicInst::Status::DISPATCHED);
    }
    resetExecutionUnitState(unit);
}

} // namespace

ExecuteStoreAccess::Result ExecuteStoreAccess::perform(ExecutionUnit& unit,
                                                       size_t unit_index,
                                                       CPUState& state) {
    if (!unit.instruction) {
        resetExecutionUnitState(unit);
        return Result::Completed;
    }

    if (unit.has_exception || unit.instruction->has_exception() || unit.instruction->has_trap()) {
        unit.result = 0;
        LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu completes with exception/trap",
             unit.instruction->get_instruction_id(),
             unit_index);
        return Result::Completed;
    }

    if (unit.work_kind == ExecutionWorkKind::StoreAddress) {
        if (ExecuteMemoryOrder::tryRecoverViolation(unit.instruction, state)) {
            return Result::RecoveryTriggered;
        }

        auto address_ready_inst = unit.instruction;
        requeueInternalStoreOp(unit);
        LOGT(EXECUTE,
             "inst=%" PRId64 " STORE%zu store-address op complete",
             address_ready_inst->get_instruction_id(),
             unit_index);
        return Result::AddressOnlyCompleted;
    }

    if (unit.work_kind == ExecutionWorkKind::StoreData) {
        auto data_ready_inst = unit.instruction;
        requeueInternalStoreOp(unit);
        LOGT(EXECUTE,
             "inst=%" PRId64 " STORE%zu store-data op complete",
             data_ready_inst->get_instruction_id(),
             unit_index);
        return Result::DataOnlyCompleted;
    }

    if (ExecuteHostCommAccess::mustSerialize(
            state, unit.instruction, unit.load_address, unit.load_size)) {
        auto blocked_inst = unit.instruction;
        blocked_inst->set_status(DynamicInst::Status::DISPATCHED);
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
            blocked_inst->set_status(DynamicInst::Status::DISPATCHED);
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

    if (state.store_queue) {
        state.store_queue->markCompleted(unit.instruction);
    }

    if (ExecuteMemoryOrder::tryRecoverViolation(unit.instruction, state)) {
        return Result::RecoveryTriggered;
    }

    return Result::Completed;
}

} // namespace riscv
