#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/execute_semantics.h"
#include "common/debug_types.h"
#include "common/types.h"
#include "core/instruction_executor.h"
#include <algorithm>

namespace riscv {

namespace {

bool rangesOverlap(uint64_t lhs_addr, uint64_t lhs_size, uint64_t rhs_addr, uint64_t rhs_size) {
    const uint64_t lhs_end = lhs_addr + lhs_size - 1;
    const uint64_t rhs_end = rhs_addr + rhs_size - 1;
    return lhs_addr <= rhs_end && rhs_addr <= lhs_end;
}

bool isHostCommAccess(const CPUState& state, uint64_t address, uint8_t size) {
    if (!state.memory || size == 0) {
        return false;
    }

    return rangesOverlap(address, size, state.memory->getTohostAddr(), 8) ||
           rangesOverlap(address, size, state.memory->getFromhostAddr(), 8);
}

bool mustSerializeHostCommAccess(const CPUState& state,
                                 const DynamicInstPtr& instruction,
                                 uint64_t address,
                                 uint8_t size) {
    if (!instruction || !isHostCommAccess(state, address, size) || !state.reorder_buffer) {
        return false;
    }

    return !state.reorder_buffer->is_head_instruction(instruction->get_instruction_id());
}

template <typename Queue>
void clearQueue(Queue& queue) {
    while (!queue.empty()) {
        queue.pop();
    }
}

}  // namespace

ExecuteStage::ExecuteStage() {
    // 构造函数：初始化执行阶段
}

void ExecuteStage::execute(CPUState& state) {
    // 首先更新正在执行的指令的状态
    update_execution_units(state);

    state.perf_counters.increment(PerfCounterId::DISPATCH_SLOTS, OOOPipelineConfig::DISPATCH_WIDTH);

    const auto dispatch_results =
        state.reservation_station->dispatch_instructions(OOOPipelineConfig::DISPATCH_WIDTH);
    if (dispatch_results.empty()) {
        LOGT(EXECUTE, "no ready instruction in reservation station");
        state.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_READY);

        const size_t rs_occupied = state.reservation_station->get_occupied_entry_count();
        if (rs_occupied == 0) {
            state.perf_counters.increment(PerfCounterId::STALL_EXECUTE_FRONTEND_STARVED);
        } else {
            const size_t rs_ready = state.reservation_station->get_ready_entry_count();
            if (rs_ready == 0) {
                state.perf_counters.increment(PerfCounterId::STALL_EXECUTE_DEPENDENCY_BLOCKED);
            } else {
                state.perf_counters.increment(PerfCounterId::STALL_EXECUTE_RESOURCE_BLOCKED);
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
            state.reorder_buffer->has_earlier_store_uncommitted(dispatch_result.instruction->get_instruction_id())) {
            dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
            state.reservation_station->release_execution_unit(dispatch_result.unit_type, dispatch_result.unit_id);
            state.recordPipelineStall(PerfCounterId::STALL_EXECUTE_AMO_WAIT);
            LOGT(EXECUTE, "inst=%" PRId64 " AMO waits on earlier uncommitted store-like op, delay dispatch",
                 dispatch_result.instruction->get_instruction_id());
            continue;
        }

        ExecutionUnit* unit = nullptr;
        switch (dispatch_result.unit_type) {
            case ExecutionUnitType::ALU:
                unit = &state.alu_units[dispatch_result.unit_id];
                break;
            case ExecutionUnitType::BRANCH:
                unit = &state.branch_units[dispatch_result.unit_id];
                break;
            case ExecutionUnitType::LOAD:
                unit = &state.load_units[dispatch_result.unit_id];
                break;
            case ExecutionUnitType::STORE:
                unit = &state.store_units[dispatch_result.unit_id];
                break;
        }

        if (!unit || unit->busy) {
            dispatch_result.instruction->set_status(DynamicInst::Status::ISSUED);
            state.reservation_station->release_execution_unit(dispatch_result.unit_type, dispatch_result.unit_id);
            LOGT(EXECUTE, "no available execution unit");
            state.recordPipelineStall(PerfCounterId::STALL_EXECUTE_NO_UNIT);
            continue;
        }

        unit->busy = true;
        unit->instruction = dispatch_result.instruction;
        unit->has_exception = false;
        unit->dcache.reset();
        unit->remaining_cycles = decoded_info.execution_cycles;

        if (dispatch_result.unit_type == ExecutionUnitType::LOAD) {
            auto& memory_info = dispatch_result.instruction->get_memory_info();
            if (!memory_info.speculated_past_addr_unknown_store &&
                state.shouldSpeculatePastAddrUnknownStore(dispatch_result.instruction->get_pc()) &&
                state.reorder_buffer->has_earlier_address_unknown_store(
                    dispatch_result.instruction->get_instruction_id())) {
                memory_info.speculated_past_addr_unknown_store = true;
                state.perf_counters.increment(PerfCounterId::LOADS_SPECULATED_ADDR_UNKNOWN);
                LOGT(EXECUTE,
                     "inst=%" PRId64
                     " load dispatch marks speculative bypass for older STORE with unresolved address",
                     dispatch_result.instruction->get_instruction_id());
            }
        }

        const char* unit_type_str = "";
        switch (dispatch_result.unit_type) {
            case ExecutionUnitType::ALU:
                unit_type_str = "ALU";
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

        state.perf_counters.increment(PerfCounterId::DISPATCHED_INSTRUCTIONS);
        dispatch_result.instruction->set_execute_cycle(state.cycle_count);
        OOOExecuteSemantics::executeInstruction(*unit, dispatch_result.instruction, state);
        dispatched_this_cycle++;
    }

    state.perf_counters.increment(PerfCounterId::DISPATCH_UTILIZED_SLOTS, dispatched_this_cycle);
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
                if (mustSerializeHostCommAccess(state, unit.instruction, unit.load_address, unit.load_size)) {
                    auto blocked_inst = unit.instruction;
                    blocked_inst->set_status(DynamicInst::Status::ISSUED);
                    state.reservation_station->release_execution_unit(ExecutionUnitType::LOAD, static_cast<int>(i));
                    resetExecutionUnitState(unit);
                    LOGT(EXECUTE,
                         "inst=%" PRId64 " LOAD%zu waits for ROB head before host-comm access",
                         blocked_inst->get_instruction_id(), i);
                    blocked_inst->get_memory_info().replay_count++;
                    record_load_replay_reason(
                        blocked_inst, state, PerfCounterId::LOAD_REPLAYS_HOST_COMM);
                    continue;
                }

                // 在Load指令完成前，再次检查Store依赖
                const auto hazard_info = state.reorder_buffer->get_earlier_store_hazard_info(
                    unit.instruction->get_instruction_id(), unit.load_address, unit.load_size);
                const auto hazard_kind = hazard_info.kind;
                
                if (hazard_kind != ReorderBuffer::StoreHazardKind::None) {
                    // 关键修复：不要长期占用唯一LOAD单元，否则会阻塞更老的LOAD并形成死锁。
                    // 将该指令回退到ISSUED状态，释放执行单元，等待下个周期重调度。
                    auto blocked_inst = unit.instruction;
                    const bool speculated_past_addr_unknown =
                        hazard_kind == ReorderBuffer::StoreHazardKind::AddressUnknown &&
                        blocked_inst->get_memory_info().speculated_past_addr_unknown_store;

                    if (speculated_past_addr_unknown) {
                        LOGT(EXECUTE,
                             "inst=%" PRId64 " LOAD%zu speculates past older STORE with unresolved address",
                             blocked_inst->get_instruction_id(), i);
                    } else {
                        blocked_inst->set_status(DynamicInst::Status::ISSUED);
                        state.reservation_station->release_execution_unit(ExecutionUnitType::LOAD, static_cast<int>(i));
                        resetExecutionUnitState(unit);
                        LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu waits on earlier STORE, replay and release load unit",
                            blocked_inst->get_instruction_id(), i);
                        blocked_inst->get_memory_info().replay_count++;
                    }

                    switch (hazard_kind) {
                        case ReorderBuffer::StoreHazardKind::Amo:
                            if (!speculated_past_addr_unknown) {
                                record_load_replay_reason(
                                    blocked_inst, state, PerfCounterId::LOAD_REPLAYS_ROB_STORE_AMO);
                            }
                            break;
                        case ReorderBuffer::StoreHazardKind::AddressUnknown:
                            if (!speculated_past_addr_unknown && hazard_info.instruction) {
                                hazard_info.instruction->get_memory_info()
                                    .caused_rob_addr_unknown_block_count++;
                            }
                            if (!speculated_past_addr_unknown) {
                                record_load_replay_reason(
                                    blocked_inst, state, PerfCounterId::LOAD_REPLAYS_ROB_STORE_ADDR_UNKNOWN);
                            }
                            break;
                        case ReorderBuffer::StoreHazardKind::Overlap:
                            if (!speculated_past_addr_unknown && hazard_info.instruction) {
                                hazard_info.instruction->get_memory_info()
                                    .caused_rob_overlap_block_count++;
                            }
                            if (!speculated_past_addr_unknown) {
                                record_load_replay_reason(
                                    blocked_inst, state, PerfCounterId::LOAD_REPLAYS_ROB_STORE_OVERLAP);
                            }
                            break;
                        case ReorderBuffer::StoreHazardKind::None:
                            break;
                    }
                    if (!speculated_past_addr_unknown) {
                        continue; // 跳过完成处理
                    }
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
                    record_load_replay_reason(
                        blocked_inst, state, PerfCounterId::LOAD_REPLAYS_STORE_BUFFER_OVERLAP);
                    continue;
                }
                if (load_result == LoadExecutionResult::WaitingForCache) {
                    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu waiting for dcache, remaining=%d",
                        unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
                    continue;
                }
                if (load_result == LoadExecutionResult::Exception) {
                    LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu raised exception: %s",
                        unit.instruction->get_instruction_id(), i, unit.exception_msg.c_str());
                    record_load_replay_bucket(unit.instruction, state);
                    complete_execution_unit(unit, ExecutionUnitType::LOAD, i, state);
                    continue;
                }
                
                LOGT(EXECUTE, "inst=%" PRId64 " LOAD%zu done, %s result=0x%" PRIx64 " -> CDB",
                    unit.instruction->get_instruction_id(),
                    i, (load_result == LoadExecutionResult::Forwarded ? "(store-forwarded)" : "(loaded-from-memory)"),
                    unit.result);

                record_load_replay_bucket(unit.instruction, state);
                
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
                if (mustSerializeHostCommAccess(state, unit.instruction, unit.load_address, unit.load_size)) {
                    auto blocked_inst = unit.instruction;
                    blocked_inst->set_status(DynamicInst::Status::ISSUED);
                    state.reservation_station->release_execution_unit(ExecutionUnitType::STORE, static_cast<int>(i));
                    resetExecutionUnitState(unit);
                    LOGT(EXECUTE,
                         "inst=%" PRId64 " STORE%zu waits for ROB head before host-comm access",
                         blocked_inst->get_instruction_id(), i);
                    continue;
                }

                if (!start_or_wait_dcache_access(
                        unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE)) {
                    LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu waiting for dcache, remaining=%d",
                        unit.instruction->get_instruction_id(), i, unit.remaining_cycles);
                    continue;
                }

                // 存储指令结果为0
                unit.result = 0;
                
                LOGT(EXECUTE, "inst=%" PRId64 " STORE%zu done, notify ROB",
                    unit.instruction->get_instruction_id(), i);

                if (try_recover_memory_order_violation(unit.instruction, state)) {
                    return;
                }
                
                complete_execution_unit(unit, ExecutionUnitType::STORE, i, state);
            }
        }
    }
}

ExecutionUnit* ExecuteStage::get_available_unit(ExecutionUnitType type, CPUState& state) {
    switch (type) {
        case ExecutionUnitType::ALU:
            for (auto& unit : state.alu_units) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::BRANCH:
            for (auto& unit : state.branch_units) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::LOAD:
            for (auto& unit : state.load_units) {
                if (!unit.busy) return &unit;
            }
            break;
        case ExecutionUnitType::STORE:
            for (auto& unit : state.store_units) {
                if (!unit.busy) return &unit;
            }
            break;
    }
    return nullptr;
}

void ExecuteStage::complete_execution_unit(ExecutionUnit& unit, ExecutionUnitType unit_type, size_t unit_index, CPUState& state) {
    unit.instruction->set_complete_cycle(state.cycle_count);

    if (unit.has_exception) {
        unit.instruction->set_exception(unit.exception_msg);
    } else {
        unit.instruction->clear_exception();
    }

    // 设置执行结果和跳转信息到DynamicInst
    unit.instruction->set_result(unit.result);
    unit.instruction->set_jump_info(unit.is_jump, unit.jump_target);
    try_recover_control_mispredict_early(unit, unit_type, unit_index, state);
    
    // 执行完成，发送到CDB
    CommonDataBusEntry cdb_entry(unit.instruction);
    state.cdb_queue.push(cdb_entry);
    state.perf_counters.increment(PerfCounterId::CDB_ENQUEUED);
    
    // 清空对应的保留站条目
    RSEntry rs_entry = unit.instruction->get_rs_entry();
    state.reservation_station->release_entry(rs_entry);
    
    // 释放执行单元
    resetExecutionUnitState(unit);
    
    // 释放保留站中的执行单元状态
    state.reservation_station->release_execution_unit(unit_type, unit_index);
}

bool ExecuteStage::try_recover_control_mispredict_early(ExecutionUnit& unit,
                                                        ExecutionUnitType current_unit_type,
                                                        size_t current_unit_index,
                                                        CPUState& state) {
    if (!unit.instruction || unit.instruction->has_exception() || unit.instruction->has_trap()) {
        return false;
    }

    const auto& instruction = unit.instruction;
    const auto& decoded_info = instruction->get_decoded_info();
    if (decoded_info.opcode != Opcode::BRANCH && decoded_info.opcode != Opcode::JALR) {
        return false;
    }

    const uint64_t instruction_pc = instruction->get_pc();
    const uint64_t instruction_id = instruction->get_instruction_id();
    const uint64_t fallthrough = instruction_pc + (decoded_info.is_compressed ? 2ULL : 4ULL);
    const uint64_t actual_next_pc = instruction->is_jump() ? instruction->get_jump_target() : fallthrough;
    const uint64_t predicted_next_pc =
        instruction->has_predicted_next_pc() ? instruction->get_predicted_next_pc() : fallthrough;
    if (predicted_next_pc == actual_next_pc) {
        return false;
    }

    const auto checkpoint_it = state.rename_checkpoints.find(instruction_id);
    if (checkpoint_it == state.rename_checkpoints.end()) {
        LOGW(EXECUTE,
             "missing rename checkpoint for early control recovery, inst=%" PRId64 " pc=0x%" PRIx64,
             instruction_id, instruction_pc);
        return false;
    }

    state.pc = actual_next_pc;
    clearQueue(state.fetch_buffer);
    if (state.l1i_cache) {
        state.l1i_cache->flushInFlight();
    }
    state.icache.reset();

    const size_t rob_flushed = state.reorder_buffer->flush_after_entry(instruction->get_rob_entry());
    state.reservation_station->flush_younger_than(instruction_id);
    state.store_buffer->flush_after(instruction_id);
    const size_t cdb_flushed = flush_younger_cdb_entries(state, instruction_id);
    const bool flushed_dcache_request =
        flush_younger_execution_units(state, instruction_id, current_unit_type, current_unit_index);

    std::vector<PhysRegNum> surviving_live_regs;
    surviving_live_regs.reserve(ReorderBuffer::MAX_ROB_ENTRIES);
    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        if (!state.reorder_buffer->is_entry_valid(static_cast<ROBEntry>(i))) {
            continue;
        }
        const auto live_entry = state.reorder_buffer->get_entry(static_cast<ROBEntry>(i));
        if (live_entry && live_entry->get_physical_dest() != 0) {
            surviving_live_regs.push_back(live_entry->get_physical_dest());
        }
    }

    state.register_rename->restore_checkpoint(checkpoint_it->second, surviving_live_regs);
    erase_younger_rename_checkpoints(state, instruction_id);
    state.rename_checkpoints.erase(instruction_id);
    if (state.branch_predictor && instruction->has_ras_checkpoint()) {
        state.branch_predictor->restoreRasCheckpoint(instruction->get_ras_checkpoint());
        state.branch_predictor->applyResolvedControlToSpeculativeRas(
            instruction_pc, decoded_info, instruction->is_jump());
    }

    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSHES);
    state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES, static_cast<uint64_t>(rob_flushed));
    if (decoded_info.opcode == Opcode::BRANCH) {
        state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_BRANCH_MISPREDICT);
        state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_BRANCH_MISPREDICT,
                                      static_cast<uint64_t>(rob_flushed));
        if (state.branch_predictor) {
            const BranchPredictor::BranchMeta* branch_meta =
                instruction->has_branch_predict_meta() ? &instruction->get_branch_predict_meta() : nullptr;
            state.branch_predictor->recover_after_branch_mispredict(
                instruction_pc, instruction->is_jump(), branch_meta);
            state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_RECOVERIES);
        }
    } else {
        state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_UNCONDITIONAL_REDIRECT);
        state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_UNCONDITIONAL_REDIRECT,
                                      static_cast<uint64_t>(rob_flushed));
        if (state.branch_predictor) {
            state.branch_predictor->on_pipeline_flush();
        }
    }

    if (flushed_dcache_request && state.l1d_cache) {
        state.l1d_cache->flushInFlight();
    }

    instruction->mark_control_recovered_early();
    LOGT(EXECUTE,
         "early control recovery: inst=%" PRId64 " pc=0x%" PRIx64
         " predicted_next=0x%" PRIx64 " actual_next=0x%" PRIx64
         " flushed_rob=%zu flushed_cdb=%zu",
         instruction_id, instruction_pc, predicted_next_pc, actual_next_pc, rob_flushed, cdb_flushed);
    return true;
}

size_t ExecuteStage::flush_younger_cdb_entries(CPUState& state, uint64_t instruction_id) {
    if (state.cdb_queue.empty()) {
        return 0;
    }

    std::queue<CommonDataBusEntry> kept_entries;
    size_t flushed = 0;
    while (!state.cdb_queue.empty()) {
        auto entry = state.cdb_queue.front();
        state.cdb_queue.pop();
        if (entry.valid && entry.instruction &&
            entry.instruction->get_instruction_id() > instruction_id) {
            flushed++;
            continue;
        }
        kept_entries.push(std::move(entry));
    }
    state.cdb_queue = std::move(kept_entries);
    return flushed;
}

bool ExecuteStage::flush_younger_execution_units(CPUState& state,
                                                 uint64_t instruction_id,
                                                 ExecutionUnitType current_unit_type,
                                                 size_t current_unit_index) {
    bool flushed_dcache_request = false;

    auto flush_container = [&](auto& units, ExecutionUnitType unit_type) {
        for (size_t i = 0; i < units.size(); ++i) {
            auto& other_unit = units[i];
            if (!other_unit.busy || !other_unit.instruction) {
                continue;
            }
            if (unit_type == current_unit_type && i == current_unit_index) {
                continue;
            }
            if (other_unit.instruction->get_instruction_id() <= instruction_id) {
                continue;
            }

            if ((unit_type == ExecutionUnitType::LOAD || unit_type == ExecutionUnitType::STORE) &&
                other_unit.dcache.request_sent) {
                flushed_dcache_request = true;
            }

            LOGT(EXECUTE, "flush younger execution unit inst=%" PRId64,
                 other_unit.instruction->get_instruction_id());
            state.reservation_station->release_execution_unit(unit_type, static_cast<int>(i));
            resetExecutionUnitState(other_unit);
        }
    };

    flush_container(state.alu_units, ExecutionUnitType::ALU);
    flush_container(state.branch_units, ExecutionUnitType::BRANCH);
    flush_container(state.load_units, ExecutionUnitType::LOAD);
    flush_container(state.store_units, ExecutionUnitType::STORE);
    return flushed_dcache_request;
}

void ExecuteStage::erase_younger_rename_checkpoints(CPUState& state, uint64_t instruction_id) {
    for (auto it = state.rename_checkpoints.begin(); it != state.rename_checkpoints.end();) {
        if (it->first > instruction_id) {
            it = state.rename_checkpoints.erase(it);
        } else {
            ++it;
        }
    }
}

bool ExecuteStage::try_recover_memory_order_violation(const DynamicInstPtr& store_instruction,
                                                      CPUState& state) {
    if (!store_instruction || !store_instruction->is_store_instruction() || !state.reorder_buffer) {
        return false;
    }

    const auto& store_memory = store_instruction->get_memory_info();
    if (!store_memory.address_ready || store_memory.memory_size == 0) {
        return false;
    }

    DynamicInstPtr violating_load = nullptr;
    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        if (!state.reorder_buffer->is_entry_valid(static_cast<ROBEntry>(i))) {
            continue;
        }

        auto candidate = state.reorder_buffer->get_entry(static_cast<ROBEntry>(i));
        if (!candidate || candidate->get_instruction_id() <= store_instruction->get_instruction_id()) {
            continue;
        }
        if (!candidate->is_load_instruction()) {
            continue;
        }

        const auto& load_memory = candidate->get_memory_info();
        if (!load_memory.speculated_past_addr_unknown_store || !load_memory.address_ready ||
            load_memory.memory_size == 0) {
            continue;
        }
        if (!candidate->is_executing() && !candidate->is_completed()) {
            continue;
        }
        if (!rangesOverlap(store_memory.memory_address,
                           store_memory.memory_size,
                           load_memory.memory_address,
                           load_memory.memory_size)) {
            continue;
        }

        violating_load = std::move(candidate);
        break;
    }

    if (!violating_load) {
        return false;
    }

    state.load_profiles[violating_load->get_pc()].speculated_addr_unknown_violation++;
    state.store_profiles[store_instruction->get_pc()].caused_order_violation++;
    state.trainLoadAddrUnknownPredictor(violating_load->get_pc(), false);

    uint64_t restart_pc = store_instruction->get_pc();
    const ROBEntry head_entry = state.reorder_buffer->get_head_entry();
    if (!state.reorder_buffer->is_empty() && state.reorder_buffer->is_entry_valid(head_entry)) {
        if (const auto head_inst = state.reorder_buffer->get_entry(head_entry)) {
            restart_pc = head_inst->get_pc();
        }
    }

    const uint64_t rob_used =
        static_cast<uint64_t>(ReorderBuffer::MAX_ROB_ENTRIES - state.reorder_buffer->get_free_entry_count());
    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSHES);
    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_OTHER);
    state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES, rob_used);
    state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_OTHER, rob_used);
    state.perf_counters.increment(PerfCounterId::MEMORY_ORDER_VIOLATION_RECOVERIES);

    state.pc = restart_pc;
    clearQueue(state.fetch_buffer);
    clearQueue(state.cdb_queue);
    state.reservation_station->flush_pipeline();
    state.reorder_buffer->flush_pipeline();
    state.register_rename->flush_pipeline();
    state.rename_checkpoints.clear();
    state.store_buffer->flush();
    state.resetExecutionUnits();
    state.reservation_valid = false;
    state.reservation_addr = 0;
    if (state.l1i_cache) {
        state.l1i_cache->flushInFlight();
    }
    if (state.l1d_cache) {
        state.l1d_cache->flushInFlight();
    }
    state.icache.reset();
    if (state.branch_predictor) {
        state.branch_predictor->on_pipeline_flush();
    }

    LOGT(EXECUTE,
         "memory order violation recovery: store inst=%" PRId64 " pc=0x%" PRIx64
         " addr=0x%" PRIx64 " load inst=%" PRId64 " pc=0x%" PRIx64 " addr=0x%" PRIx64
         " restart_pc=0x%" PRIx64,
         store_instruction->get_instruction_id(),
         store_instruction->get_pc(),
         store_memory.memory_address,
         violating_load->get_instruction_id(),
         violating_load->get_pc(),
         violating_load->get_memory_info().memory_address,
         restart_pc);
    return true;
}

void ExecuteStage::record_load_replay_bucket(const DynamicInstPtr& instruction, CPUState& state) {
    if (!instruction) {
        return;
    }

    const auto& memory_info = instruction->get_memory_info();
    const uint32_t replay_count = memory_info.replay_count;
    if (replay_count == 0) {
        state.perf_counters.increment(PerfCounterId::LOAD_REPLAY_BUCKET_0);
    } else if (replay_count == 1) {
        state.perf_counters.increment(PerfCounterId::LOAD_REPLAY_BUCKET_1);
    } else if (replay_count == 2) {
        state.perf_counters.increment(PerfCounterId::LOAD_REPLAY_BUCKET_2);
    } else if (replay_count == 3) {
        state.perf_counters.increment(PerfCounterId::LOAD_REPLAY_BUCKET_3);
    } else {
        state.perf_counters.increment(PerfCounterId::LOAD_REPLAY_BUCKET_4_PLUS);
    }
}

void ExecuteStage::record_load_replay_reason(const DynamicInstPtr& instruction,
                                             CPUState& state,
                                             PerfCounterId reason_counter_id) {
    state.perf_counters.increment(PerfCounterId::LOAD_REPLAYS);
    state.perf_counters.increment(reason_counter_id);

    if (!instruction) {
        return;
    }

    auto& memory_info = instruction->get_memory_info();
    switch (reason_counter_id) {
        case PerfCounterId::LOAD_REPLAYS_HOST_COMM:
            memory_info.replay_host_comm_count++;
            break;
        case PerfCounterId::LOAD_REPLAYS_ROB_STORE_AMO:
            memory_info.replay_rob_store_amo_count++;
            break;
        case PerfCounterId::LOAD_REPLAYS_ROB_STORE_ADDR_UNKNOWN:
            memory_info.replay_rob_store_addr_unknown_count++;
            break;
        case PerfCounterId::LOAD_REPLAYS_ROB_STORE_OVERLAP:
            memory_info.replay_rob_store_overlap_count++;
            break;
        case PerfCounterId::LOAD_REPLAYS_STORE_BUFFER_OVERLAP:
            memory_info.replay_store_buffer_overlap_count++;
            break;
        default:
            break;
    }
}

void ExecuteStage::flush() {
    LOGT(EXECUTE, "execute stage flushed");
}

void ExecuteStage::reset() {
    LOGT(EXECUTE, "execute stage reset");
}

void ExecuteStage::record_dcache_access_result(CPUState& state,
                                               CacheAccessType access_type,
                                               const CacheAccessResult& cache_result) {
    state.perf_counters.increment(PerfCounterId::CACHE_L1D_ACCESSES);
    if (access_type == CacheAccessType::Read) {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_READ_ACCESSES);
    } else {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_WRITE_ACCESSES);
    }

    if (cache_result.hit) {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_HITS);
    } else {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_MISSES);
    }

    if (cache_result.dirty_eviction) {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_DIRTY_EVICTIONS);
    }
}

bool ExecuteStage::start_or_wait_dcache_access(ExecutionUnit& unit,
                                               CPUState& state,
                                               CacheAccessType access_type,
                                               PerfCounterId stall_counter_id) {
    if (!state.l1d_cache) {
        unit.dcache.waiting = false;
        return true;
    }

    if (unit.dcache.request_sent) {
        unit.dcache.waiting = false;
        return true;
    }

    CacheAccessResult cache_result{};
    try {
        cache_result = state.l1d_cache->access(
            state.memory, unit.load_address, unit.load_size, access_type);
    } catch (const SimulatorException& e) {
        unit.has_exception = true;
        unit.exception_msg = e.what();
        unit.result = 0;
        unit.dcache.reset();
        return true;
    }
    if (cache_result.blocked) {
        unit.dcache.waiting = true;
        unit.remaining_cycles = 1;
        state.perf_counters.increment(stall_counter_id);
        return false;
    }

    record_dcache_access_result(state, access_type, cache_result);

    unit.dcache.request_sent = true;
    unit.dcache.waiting = true;

    const int extra_cycles = std::max(0, cache_result.latency_cycles - 1);
    if (extra_cycles > 0) {
        unit.remaining_cycles = extra_cycles;
        state.perf_counters.increment(stall_counter_id, static_cast<uint64_t>(extra_cycles));
        return false;
    }

    unit.dcache.waiting = false;
    return true;
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
                // FLW 需要nan-box到64位浮点寄存器；FLD 直接写入64位
                if (access_size == 4) {
                    unit.result = 0xFFFFFFFF00000000ULL | (forwarded_value & 0xFFFFFFFFULL);
                } else {
                    unit.result = forwarded_value;
                }
                LOGT(EXECUTE, "fp store-to-load forwarding hit: addr=0x%" PRIx64 " value=0x%" PRIx64,
                     addr, unit.result);
                memory_info.memory_value = unit.result;
                return LoadExecutionResult::Forwarded;
            }

            // 从Store Buffer获得转发数据，根据预解析的符号扩展信息处理
            if (inst.is_signed_load) {
            // 符号扩展Load指令：LB, LH, LW
                switch (access_size) {
                case 1: // LB
                        unit.result = static_cast<uint64_t>(static_cast<int8_t>(forwarded_value & 0xFF));
                        break;
                case 2: // LH
                        unit.result = static_cast<uint64_t>(static_cast<int16_t>(forwarded_value & 0xFFFF));
                        break;
                case 4: // LW
                        unit.result = static_cast<uint64_t>(static_cast<int32_t>(forwarded_value & 0xFFFFFFFF));
                        break;
                case 8: // LD (64位)
                    unit.result = forwarded_value;
                    break;
                    default:
                        unit.result = forwarded_value;
                        break;
                }
            } else {
            // 零扩展Load指令：LBU, LHU, LWU
                switch (access_size) {
                case 1: // LBU
                        unit.result = forwarded_value & 0xFF;
                        break;
                case 2: // LHU
                        unit.result = forwarded_value & 0xFFFF;
                        break;
                case 4: // LWU (RV64新增)
                        unit.result = forwarded_value & 0xFFFFFFFF;
                        break;
                case 8: // LD (64位)
                    unit.result = forwarded_value;
                    break;
                    default:
                        unit.result = forwarded_value;
                        break;
                }
            }

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

            record_dcache_access_result(state, CacheAccessType::Read, cache_result);

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

            if (inst.opcode == Opcode::LOAD_FP) {
                if (access_size == 4) {
                    unit.result = 0xFFFFFFFF00000000ULL | (raw_value & 0xFFFFFFFFULL);
                } else {
                    unit.result = raw_value;
                }
            } else if (inst.is_signed_load) {
                switch (access_size) {
                    case 1:
                        unit.result = static_cast<uint64_t>(static_cast<int8_t>(raw_value & 0xFF));
                        break;
                    case 2:
                        unit.result = static_cast<uint64_t>(static_cast<int16_t>(raw_value & 0xFFFF));
                        break;
                    case 4:
                        unit.result = static_cast<uint64_t>(static_cast<int32_t>(raw_value & 0xFFFFFFFF));
                        break;
                    case 8:
                    default:
                        unit.result = raw_value;
                        break;
                }
            } else {
                switch (access_size) {
                    case 1:
                        unit.result = raw_value & 0xFF;
                        break;
                    case 2:
                        unit.result = raw_value & 0xFFFF;
                        break;
                    case 4:
                        unit.result = raw_value & 0xFFFFFFFF;
                        break;
                    case 8:
                    default:
                        unit.result = raw_value;
                        break;
                }
            }

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
