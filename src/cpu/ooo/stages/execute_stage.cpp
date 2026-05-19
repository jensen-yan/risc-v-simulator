#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/execute_control_recovery.h"
#include "cpu/ooo/execute_dcache_access.h"
#include "cpu/ooo/execute_host_comm_access.h"
#include "cpu/ooo/execute_load_hazard.h"
#include "cpu/ooo/execute_load_value.h"
#include "cpu/ooo/execute_memory_inflight.h"
#include "cpu/ooo/execute_memory_order.h"
#include "cpu/ooo/execute_semantics.h"
#include "common/debug_types.h"
#include "common/types.h"
#include "core/instruction_executor.h"
#include <algorithm>

namespace riscv {

ExecuteStage::ExecuteStage() {
    // 构造函数：初始化执行阶段
}

bool ExecuteStage::Context::hasInflightMemoryAccess() const {
    return ExecuteMemoryInflight::hasAny(state_);
}

ExecutionUnit* ExecuteStage::Context::executionUnit(ExecutionUnitType unit_type, int unit_id) {
    if (unit_id < 0) {
        return nullptr;
    }

    const auto index = static_cast<size_t>(unit_id);
    switch (unit_type) {
        case ExecutionUnitType::ALU:
            return index < state_.alu_units.size() ? &state_.alu_units[index] : nullptr;
        case ExecutionUnitType::FP:
            return index < state_.fp_units.size() ? &state_.fp_units[index] : nullptr;
        case ExecutionUnitType::BRANCH:
            return index < state_.branch_units.size() ? &state_.branch_units[index] : nullptr;
        case ExecutionUnitType::LOAD:
            return index < state_.load_units.size() ? &state_.load_units[index] : nullptr;
        case ExecutionUnitType::STORE:
            return index < state_.store_units.size() ? &state_.store_units[index] : nullptr;
    }
    return nullptr;
}

void ExecuteStage::execute(Context& context) {
    CPUState& state = context.stateForLegacyExecuteInternals();

    // 首先更新正在执行的指令的状态
    update_execution_units(state);
    ExecuteMemoryInflight::advance(
        state,
        [this, &state](ExecutionUnit& unit, ExecutionUnitType unit_type) {
            complete_execution_unit(
                unit, unit_type, 0, state, /*release_dispatch_unit=*/false);
        });

    context.incrementCounter(PerfCounterId::DISPATCH_SLOTS, OOOPipelineConfig::DISPATCH_WIDTH);
    const auto addr_unknown_store_snapshot =
        ExecuteMemoryOrder::captureAddrUnknownStoreSnapshot(state);

    const auto dispatch_results = context.dispatchReadyInstructions(
        OOOPipelineConfig::DISPATCH_WIDTH,
        [&](const DynamicInstPtr& instruction) {
            return !ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(
                state, instruction, addr_unknown_store_snapshot);
        });
    if (dispatch_results.empty()) {
        LOGT(EXECUTE, "no ready instruction in reservation station");
        context.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_READY);

        if (context.hasInflightMemoryAccess()) {
            context.incrementCounter(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED);
            return;
        }

        const size_t rs_occupied = context.reservationStationOccupiedCount();
        if (rs_occupied == 0) {
            context.incrementCounter(PerfCounterId::STALL_EXECUTE_FRONTEND_STARVED);
        } else {
            const size_t rs_ready = context.reservationStationReadyCount();
            if (rs_ready == 0) {
                context.incrementCounter(PerfCounterId::STALL_EXECUTE_DEPENDENCY_BLOCKED);
            } else {
                context.incrementCounter(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED);
            }
        }
    }

    size_t dispatched_this_cycle = 0;
    for (size_t slot = 0; slot < dispatch_results.size(); ++slot) {
        auto dispatch_result = dispatch_results[slot];
        LOGT(EXECUTE, "dispatch slot=%zu inst=%" PRId64 " from rs[%d] to execution unit",
             slot, dispatch_result.instruction->get_instruction_id(), dispatch_result.rs_entry);

        const auto& decoded_info = dispatch_result.instruction->get_decoded_info();
        if (decoded_info.opcode == Opcode::AMO &&
            context.hasEarlierStoreUncommitted(dispatch_result.instruction->get_instruction_id())) {
            dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
            context.releaseExecutionUnit(dispatch_result.unit_type, dispatch_result.unit_id);
            context.recordPipelineStall(PerfCounterId::STALL_EXECUTE_AMO_WAIT);
            LOGT(EXECUTE, "inst=%" PRId64 " AMO waits on earlier uncommitted store-like op, delay dispatch",
                 dispatch_result.instruction->get_instruction_id());
            continue;
        }

        ExecutionUnit* unit = context.executionUnit(dispatch_result.unit_type, dispatch_result.unit_id);

        if (!unit || unit->busy) {
            dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
            context.releaseExecutionUnit(dispatch_result.unit_type, dispatch_result.unit_id);
            LOGT(EXECUTE, "no available execution unit");
            context.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_UNIT);
            continue;
        }

        unit->busy = true;
        unit->instruction = dispatch_result.instruction;
        unit->has_exception = false;
        unit->dcache.reset();
        unit->remaining_cycles = decoded_info.execution_cycles;

        if (dispatch_result.unit_type == ExecutionUnitType::LOAD) {
            auto& memory_info = dispatch_result.instruction->get_memory_info();
            if (ExecuteMemoryOrder::markBlockedAddrUnknownPairIfNeeded(
                    state, dispatch_result.instruction, addr_unknown_store_snapshot)) {
                dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
                context.releaseExecutionUnit(
                    ExecutionUnitType::LOAD, dispatch_result.unit_id);
                continue;
            }
            const auto older_unknown_store_pc = ExecuteMemoryOrder::findFirstOlderAddrUnknownStorePc(
                addr_unknown_store_snapshot, dispatch_result.instruction->get_instruction_id());
            if (older_unknown_store_pc.has_value()) {
                const uint64_t load_pc = dispatch_result.instruction->get_pc();
                if (!memory_info.speculated_past_addr_unknown_store &&
                    state.shouldSpeculatePastAddrUnknownStore(load_pc, *older_unknown_store_pc)) {
                    const uint64_t store_pc = *older_unknown_store_pc;
                    memory_info.speculated_past_addr_unknown_store = true;
                    memory_info.has_speculated_addr_unknown_source = true;
                    memory_info.speculated_addr_unknown_store_pc = store_pc;
                    context.incrementCounter(PerfCounterId::LOADS_SPECULATED_ADDR_UNKNOWN);
                    LOGT(EXECUTE,
                         "inst=%" PRId64
                         " load dispatch speculates past unresolved STORE pc=0x%" PRIx64,
                         dispatch_result.instruction->get_instruction_id(),
                         store_pc);
                }
            }
        }

        const char* unit_type_str = "";
        switch (dispatch_result.unit_type) {
            case ExecutionUnitType::ALU:
                unit_type_str = "ALU";
                break;
            case ExecutionUnitType::FP:
                unit_type_str = "FP";
                break;
            case ExecutionUnitType::BRANCH:
                unit_type_str = "BRANCH";
                break;
            case ExecutionUnitType::LOAD:
                unit_type_str = "LOAD";
                break;
            case ExecutionUnitType::STORE:
                unit_type_str = "STORE";
                break;
        }

        LOGT(EXECUTE, "inst=%" PRId64 " start on %s%d, cycles=%d",
             dispatch_result.instruction->get_instruction_id(),
             unit_type_str,
             dispatch_result.unit_id,
             unit->remaining_cycles);

        context.incrementCounter(PerfCounterId::DISPATCHED_INSTRUCTIONS);
        dispatch_result.instruction->set_execute_cycle(context.cycleCount());
        OOOExecuteSemantics::executeInstruction(
            *unit, dispatch_result.instruction, context.stateForExecuteSemantics());
        dispatched_this_cycle++;
    }

    context.incrementCounter(PerfCounterId::DISPATCH_UTILIZED_SLOTS, dispatched_this_cycle);
}

void ExecuteStage::update_execution_units(CPUState& state) {
    // 更新ALU单元
    for (size_t i = 0; i < state.alu_units.size(); ++i) {
        auto& unit = state.alu_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " ALU%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                const auto& inst = unit.instruction->get_decoded_info();
                if (inst.opcode == Opcode::AMO &&
                    state.reorder_buffer->has_earlier_store_uncommitted(unit.instruction->get_instruction_id())) {
                    // 双保险：若AMO执行期间出现顺序约束，延迟完成，等待更老Store/AMO提交。
                    unit.remaining_cycles = 1;
                    LOGT(EXECUTE, "inst=%" PRId64 " AMO waits on earlier uncommitted store-like op, delay completion",
                        unit.instruction->get_instruction_id());
                    continue;
                }

                LOGT(EXECUTE, "inst=%" PRId64 " ALU%zu done, result=0x%" PRIx64 " -> CDB",
                    unit.instruction->get_instruction_id(), i, unit.result);
                
                complete_execution_unit(unit, ExecutionUnitType::ALU, i, state);
            }
        }
    }

    for (size_t i = 0; i < state.fp_units.size(); ++i) {
        auto& unit = state.fp_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " FP%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);

            if (unit.remaining_cycles <= 0) {
                LOGT(EXECUTE, "inst=%" PRId64 " FP%zu done, result=0x%" PRIx64 " -> CDB",
                    unit.instruction->get_instruction_id(), i, unit.result);
                complete_execution_unit(unit, ExecutionUnitType::FP, i, state);
            }
        }
    }
    
    // 更新分支单元
    for (size_t i = 0; i < state.branch_units.size(); ++i) {
        auto& unit = state.branch_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " BRANCH%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                LOGT(EXECUTE, "inst=%" PRId64 " BRANCH%zu done -> CDB",
                    unit.instruction->get_instruction_id(), i);
                complete_execution_unit(unit, ExecutionUnitType::BRANCH, i, state);
            }
        }
    }
    
    // 更新加载单元
    for (size_t i = 0; i < state.load_units.size(); ++i) {
        auto& unit = state.load_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                if (ExecuteHostCommAccess::mustSerialize(
                        state, unit.instruction, unit.load_address, unit.load_size)) {
                    auto blocked_inst = unit.instruction;
                    blocked_inst->set_status(DynamicInst::Status::ISSUED);
                    state.reservation_station->release_execution_unit(ExecutionUnitType::LOAD, static_cast<int>(i));
                    resetExecutionUnitState(unit);
                    LOGT(EXECUTE,
                         "inst=%" PRId64 " LOAD%zu waits for ROB head before host-comm access",
                         blocked_inst->get_instruction_id(), i);
                    blocked_inst->get_memory_info().replay_count++;
                    ExecuteMemoryOrder::recordLoadReplayReason(
                        blocked_inst, state, PerfCounterId::LOAD_REPLAYS_HOST_COMM);
                    continue;
                }

                if (ExecuteLoadHazard::handleEarlierStoreHazard(unit, i, state) ==
                    ExecuteLoadHazard::Decision::Replayed) {
                    continue;
                }
                
                // 没有Store依赖，可以完成
                // 尝试Store-to-Load Forwarding/内存读取
                LoadExecutionResult load_result = perform_load_execution(unit, state);
                if (load_result == LoadExecutionResult::BlockedByStore) {
                    auto blocked_inst = unit.instruction;
                    blocked_inst->set_status(DynamicInst::Status::ISSUED);
                    state.reservation_station->release_execution_unit(ExecutionUnitType::LOAD, static_cast<int>(i));
                    resetExecutionUnitState(unit);
                    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu blocked by older store overlap, replay and release load unit",
                        blocked_inst->get_instruction_id(), i);
                    blocked_inst->get_memory_info().replay_count++;
                    ExecuteMemoryOrder::recordLoadReplayReason(
                        blocked_inst, state, PerfCounterId::LOAD_REPLAYS_STORE_BUFFER_OVERLAP);
                    continue;
                }
                if (load_result == LoadExecutionResult::WaitingForCache) {
                    if (unit.dcache.request_sent &&
                        ExecuteMemoryInflight::tryMove(unit, ExecutionUnitType::LOAD, i, state)) {
                        continue;
                    }

                    if (!unit.dcache.request_sent) {
                        auto blocked_inst = unit.instruction;
                        blocked_inst->set_status(DynamicInst::Status::ISSUED);
                        state.reservation_station->release_execution_unit(
                            ExecutionUnitType::LOAD, static_cast<int>(i));
                        resetExecutionUnitState(unit);
                        LOGT(EXECUTE,
                             "inst=%" PRId64 " LOAD%zu blocked by dcache outstanding limit, release and retry",
                             blocked_inst->get_instruction_id(), i);
                        continue;
                    }

                    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu waiting for dcache, remaining=%d",
                        unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
                    continue;
                }
                if (load_result == LoadExecutionResult::Exception) {
                    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu raised exception: %s",
                        unit.instruction->get_instruction_id(), i, unit.exception_msg.c_str());
                    ExecuteMemoryOrder::recordLoadReplayBucket(unit.instruction, state);
                    complete_execution_unit(unit, ExecutionUnitType::LOAD, i, state);
                    continue;
                }
                
                LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu done, %s result=0x%" PRIx64 " -> CDB",
                    unit.instruction->get_instruction_id(),
                    i, (load_result == LoadExecutionResult::Forwarded ? "(store-forwarded)" : "(loaded-from-memory)"),
                    unit.result);

                ExecuteMemoryOrder::recordLoadReplayBucket(unit.instruction, state);
                
                complete_execution_unit(unit, ExecutionUnitType::LOAD, i, state);
            }
        }
    }
    
    // 更新存储单元
    for (size_t i = 0; i < state.store_units.size(); ++i) {
        auto& unit = state.store_units[i];
        if (unit.busy) {
            unit.remaining_cycles--;
            LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu running, remaining=%d",
                unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
            
            if (unit.remaining_cycles <= 0) {
                if (ExecuteHostCommAccess::mustSerialize(
                        state, unit.instruction, unit.load_address, unit.load_size)) {
                    auto blocked_inst = unit.instruction;
                    blocked_inst->set_status(DynamicInst::Status::ISSUED);
                    state.reservation_station->release_execution_unit(ExecutionUnitType::STORE, static_cast<int>(i));
                    resetExecutionUnitState(unit);
                    LOGT(EXECUTE,
                         "inst=%" PRId64 " STORE%zu waits for ROB head before host-comm access",
                         blocked_inst->get_instruction_id(), i);
                    continue;
                }

                if (!ExecuteDCacheAccess::startOrWait(
                        unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE)) {
                    if (unit.dcache.request_sent &&
                        ExecuteMemoryInflight::tryMove(unit, ExecutionUnitType::STORE, i, state)) {
                        continue;
                    }

                    if (!unit.dcache.request_sent) {
                        auto blocked_inst = unit.instruction;
                        blocked_inst->set_status(DynamicInst::Status::ISSUED);
                        state.reservation_station->release_execution_unit(
                            ExecutionUnitType::STORE, static_cast<int>(i));
                        resetExecutionUnitState(unit);
                        LOGT(EXECUTE,
                             "inst=%" PRId64 " STORE%zu blocked by dcache outstanding limit, release and retry",
                             blocked_inst->get_instruction_id(), i);
                        continue;
                    }

                    LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu waiting for dcache, remaining=%d",
                        unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
                    continue;
                }

                // 存储指令结果为0
                unit.result = 0;
                
                LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu done, notify ROB",
                    unit.instruction->get_instruction_id(), i);

                if (ExecuteMemoryOrder::tryRecoverViolation(unit.instruction, state)) {
                    return;
                }
                
                complete_execution_unit(unit, ExecutionUnitType::STORE, i, state);
            }
        }
    }
}

void ExecuteStage::complete_execution_unit(ExecutionUnit& unit,
                                           ExecutionUnitType unit_type,
                                           size_t unit_index,
                                           CPUState& state,
                                           bool release_dispatch_unit) {
    unit.instruction->set_complete_cycle(state.cycle_count);

    if (unit.has_exception) {
        unit.instruction->set_exception(unit.exception_msg);
    } else {
        unit.instruction->clear_exception();
    }

    // 设置执行结果和跳转信息到DynamicInst
    unit.instruction->set_result(unit.result);
    unit.instruction->set_jump_info(unit.is_jump, unit.jump_target);
    ExecuteControlRecovery::tryRecoverEarly(unit, unit_type, unit_index, state);
    
    // 执行完成，发送到CDB
    CommonDataBusEntry cdb_entry(unit.instruction);
    state.cdb_queue.push(cdb_entry);
    state.perf_counters.increment(PerfCounterId::CDB_ENQUEUED);
    
    // 清空对应的保留站条目
    RSEntry rs_entry = unit.instruction->get_rs_entry();
    state.reservation_station->release_entry(rs_entry);

    if (release_dispatch_unit) {
        state.reservation_station->release_execution_unit(unit_type, unit_index);
    }

    // 释放执行单元
    resetExecutionUnitState(unit);
}

ExecuteStage::LoadExecutionResult ExecuteStage::perform_load_execution(ExecutionUnit& unit, CPUState& state) {
    const auto& inst = unit.instruction->get_decoded_info();
    uint64_t addr = unit.load_address;
    uint8_t access_size = unit.load_size;
    auto& memory_info = unit.instruction->get_memory_info();
    const uint8_t full_forward_mask =
        static_cast<uint8_t>(access_size == 8 ? 0xFFu : ((1u << access_size) - 1u));
    
    if (!unit.dcache.request_sent) {
        // 尝试Store-to-Load Forwarding
        const auto forwarding_info = state.store_buffer->analyze_load_forwarding(
            addr, access_size, unit.instruction->get_instruction_id());
        const auto forwarding_kind = forwarding_info.kind;
        const uint64_t forwarded_value = forwarding_info.value;
        const bool needs_memory_merge =
            forwarding_kind == StoreBuffer::LoadForwardingKind::PartialMatch &&
            forwarding_info.byte_mask != 0 &&
            forwarding_info.byte_mask != full_forward_mask;

        if (forwarding_kind == StoreBuffer::LoadForwardingKind::FullMatch ||
            (forwarding_kind == StoreBuffer::LoadForwardingKind::PartialMatch && !needs_memory_merge)) {
            memory_info.store_forwarded = true;
            state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED);
            if (forwarding_kind == StoreBuffer::LoadForwardingKind::FullMatch) {
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED_FULL_MATCH);
                memory_info.load_final_source = DynamicInst::MemoryInfo::LoadFinalSource::ForwardedFull;
                for (size_t idx = 0; idx < forwarding_info.contributing_count; ++idx) {
                    forwarding_info.contributing_stores[idx]->get_memory_info().caused_forwarded_full_count++;
                }
            } else {
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED_PARTIAL_MATCH);
                memory_info.load_final_source = DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial;
                for (size_t idx = 0; idx < forwarding_info.contributing_count; ++idx) {
                    forwarding_info.contributing_stores[idx]->get_memory_info().caused_forwarded_partial_count++;
                }
            }
            if (inst.opcode == Opcode::LOAD_FP) {
                unit.result = ExecuteLoadValue::format(inst, access_size, forwarded_value);
                LOGT(EXECUTE, "fp store-to-load forwarding hit: addr=0x%" PRIx64 " value=0x%" PRIx64,
                     addr, unit.result);
                memory_info.memory_value = unit.result;
                return LoadExecutionResult::Forwarded;
            }

            unit.result = ExecuteLoadValue::format(inst, access_size, forwarded_value);

            LOGT(EXECUTE, "store-to-load forwarding hit: addr=0x%" PRIx64 " value=0x%" PRIx64 " %s-extended",
                           addr, unit.result, inst.is_signed_load ? "sign" : "zero");
            memory_info.memory_value = unit.result;
            return LoadExecutionResult::Forwarded;
        }

        memory_info.store_forwarded = false;
        memory_info.load_final_source =
            needs_memory_merge ? DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial
                               : DynamicInst::MemoryInfo::LoadFinalSource::FromMemory;
        try {
            uint64_t raw_value = 0;
            CacheAccessResult cache_result{};
            if (state.l1d_cache) {
                cache_result = state.l1d_cache->load(state.memory, addr, access_size, raw_value);
            } else {
                switch (access_size) {
                    case 1:
                        raw_value = state.memory->readByte(addr);
                        break;
                    case 2:
                        raw_value = state.memory->readHalfWord(addr);
                        break;
                    case 4:
                        raw_value = state.memory->readWord(addr);
                        break;
                    case 8:
                        raw_value = state.memory->read64(addr);
                        break;
                    default:
                        throw SimulatorException("unsupported load size: " + std::to_string(access_size));
                }
                cache_result.hit = true;
                cache_result.latency_cycles = 1;
            }

            if (cache_result.blocked) {
                unit.dcache.waiting = true;
                unit.remaining_cycles = 1;
                state.perf_counters.increment(PerfCounterId::CACHE_L1D_STALL_CYCLES_LOAD);
                return LoadExecutionResult::WaitingForCache;
            }

            ExecuteDCacheAccess::recordResult(state, CacheAccessType::Read, cache_result);

            if (needs_memory_merge) {
                for (uint8_t byte_index = 0; byte_index < access_size; ++byte_index) {
                    const uint8_t bit = static_cast<uint8_t>(1u << byte_index);
                    if ((forwarding_info.byte_mask & bit) == 0) {
                        continue;
                    }
                    const uint64_t byte_mask = 0xFFull << (byte_index * 8);
                    raw_value &= ~byte_mask;
                    raw_value |= forwarded_value & byte_mask;
                }
                memory_info.store_forwarded = true;
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED);
                state.perf_counters.increment(PerfCounterId::LOADS_FORWARDED_PARTIAL_MATCH);
                for (size_t idx = 0; idx < forwarding_info.contributing_count; ++idx) {
                    forwarding_info.contributing_stores[idx]
                        ->get_memory_info()
                        .caused_forwarded_partial_count++;
                }
            } else if (forwarding_kind == StoreBuffer::LoadForwardingKind::BlockedByOverlap) {
                if (forwarding_info.primary_store) {
                    forwarding_info.primary_store->get_memory_info().caused_store_buffer_overlap_block_count++;
                }
                state.perf_counters.increment(PerfCounterId::LOADS_BLOCKED_BY_STORE);
                return LoadExecutionResult::BlockedByStore;
            }

            unit.result = ExecuteLoadValue::format(inst, access_size, raw_value);

            memory_info.memory_value = unit.result;
            state.perf_counters.increment(PerfCounterId::LOADS_FROM_MEMORY);
            unit.dcache.request_sent = true;
            unit.dcache.waiting = true;

            const int extra_cycles = std::max(0, cache_result.latency_cycles - 1);
            if (extra_cycles > 0) {
                unit.remaining_cycles = extra_cycles;
                state.perf_counters.increment(
                    PerfCounterId::CACHE_L1D_STALL_CYCLES_LOAD, static_cast<uint64_t>(extra_cycles));
                return LoadExecutionResult::WaitingForCache;
            }

            unit.dcache.waiting = false;
            return LoadExecutionResult::LoadedFromMemory;
        } catch (const SimulatorException& e) {
            unit.has_exception = true;
            unit.exception_msg = e.what();
            unit.result = 0;
            unit.dcache.waiting = false;
            return LoadExecutionResult::Exception;
        }
    }

    unit.dcache.waiting = false;
    return LoadExecutionResult::LoadedFromMemory;
}

} // namespace riscv 
