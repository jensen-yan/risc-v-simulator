#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/branch_predictor.h"
#include "cpu/ooo/dynamic_inst.h"
#include "core/csr_utils.h"
#include "core/instruction_executor.h"
#include "common/debug_types.h"

#include <algorithm>

namespace riscv {

namespace {
constexpr uint8_t kFenceIFunct3 = 0b001;
constexpr uint32_t kMcycleCsrAddr = 0xB00;
constexpr uint32_t kMinstretCsrAddr = 0xB02;
constexpr uint32_t kCycleCsrAddr = 0xC00;
constexpr uint32_t kInstretCsrAddr = 0xC02;

enum class JalrProfileKind {
    ReturnLike,
    CallLike,
    Other,
};

uint8_t atomicWidthToSize(Funct3 width) {
    switch (width) {
        case Funct3::LW:
            return 4;
        case Funct3::LD:
            return 8;
        default:
            throw IllegalInstructionException("A扩展仅支持W/D宽度");
    }
}

void writeAtomicMemoryValue(std::shared_ptr<Memory> memory, uint64_t addr, Funct3 width, uint64_t value) {
    switch (width) {
        case Funct3::LW:
            memory->writeWord(addr, static_cast<uint32_t>(value));
            return;
        case Funct3::LD:
            memory->write64(addr, value);
            return;
        default:
            throw IllegalInstructionException("A扩展仅支持W/D宽度");
    }
}

void syncBasicPerfCounters(CPUState& state) {
    // 与 in-order 保持一致：四个基础计数器统一跟随已提交指令数。
    const uint64_t retired = state.instruction_count;
    csr::write(state.csr_registers, kMcycleCsrAddr, retired);
    csr::write(state.csr_registers, kMinstretCsrAddr, retired);
    csr::write(state.csr_registers, kCycleCsrAddr, retired);
    csr::write(state.csr_registers, kInstretCsrAddr, retired);
}

bool isLinkRegister(RegNum reg) {
    return reg == 1 || reg == 5;
}

void writeCommittedCsr(CPUState& state, uint32_t csr_addr, uint64_t value) {
    if (state.cpu_interface) {
        state.cpu_interface->setCSR(csr_addr, value);
        return;
    }
    csr::write(state.csr_registers, csr_addr, value);
}

JalrProfileKind classifyJalrKind(const DecodedInstruction& decoded) {
    if (decoded.rd == 0 && decoded.imm == 0 && isLinkRegister(decoded.rs1)) {
        return JalrProfileKind::ReturnLike;
    }
    if (isLinkRegister(decoded.rd)) {
        return JalrProfileKind::CallLike;
    }
    return JalrProfileKind::Other;
}
}  // namespace

CommitStage::CommitStage() {
    // 构造函数：初始化提交阶段
}

void CommitStage::execute(CPUState& state) {
    const size_t effective_commit_width =
        (state.commit_width_override == 0)
            ? OOOPipelineConfig::COMMIT_WIDTH
            : std::min(OOOPipelineConfig::COMMIT_WIDTH, state.commit_width_override);

    state.perf_counters.increment(PerfCounterId::COMMIT_SLOTS, effective_commit_width);

    // 添加ROB状态调试信息
    size_t free_entries = state.reorder_buffer->get_free_entry_count();
    size_t used_entries = ReorderBuffer::MAX_ROB_ENTRIES - free_entries;
    LOGT(COMMIT, "rob state: used=%zu/%d, empty=%s, full=%s",
        used_entries, ReorderBuffer::MAX_ROB_ENTRIES,
        (state.reorder_buffer->is_empty() ? "yes" : "no"),
        (state.reorder_buffer->is_full() ? "yes" : "no"));
    
    // 添加ROB状态检查
    if (state.reorder_buffer->is_empty()) {
        LOGT(COMMIT, "rob empty, cannot commit");
        return;
    }
    
    // 检查头部指令的状态
    auto head_entry_id = state.reorder_buffer->get_head_entry();
    if (head_entry_id == ReorderBuffer::MAX_ROB_ENTRIES) {
        LOGT(COMMIT, "no valid head entry");
        return;
    }
    
    const auto& head_entry = state.reorder_buffer->get_entry(head_entry_id);
    const char* state_str;
    if (head_entry) {
        switch (head_entry->get_status()) {
            case DynamicInst::Status::ALLOCATED: state_str = "ALLOCATED"; break;
            case DynamicInst::Status::ISSUED: state_str = "ISSUED"; break;
            case DynamicInst::Status::EXECUTING: state_str = "EXECUTING"; break;
            case DynamicInst::Status::COMPLETED: state_str = "COMPLETED"; break;
            case DynamicInst::Status::RETIRED: state_str = "RETIRED"; break;
            default: state_str = "UNKNOWN"; break;
        }
    } else {
        state_str = "NULL";
    }
    
    if (head_entry) {
        LOGT(COMMIT, "inst=%" PRId64 " head rob[%d] state=%s completed=%s",
            head_entry->get_instruction_id(), head_entry_id, state_str,
            (head_entry->is_completed() ? "yes" : "no"));
    }
    
    if (!state.reorder_buffer->can_commit()) {
        LOGT(COMMIT, "head instruction not completed, cannot commit");
        return;
    }
    
    // 尝试提交指令
    size_t committed_this_cycle = 0;
    while (state.reorder_buffer->can_commit() &&
           committed_this_cycle < effective_commit_width) {
        auto commit_result = state.reorder_buffer->commit_instruction();
        if (!commit_result.success) {
            LOGW(COMMIT, "commit failed: %s", commit_result.error_message.c_str());
            break;
        }
        
        const auto& committed_inst = commit_result.instruction;
        const auto& decoded_info = committed_inst->get_decoded_info();
        auto make_flush_summary = [&](FlushReason reason) {
            PipelineTracer::FlushSummary summary;
            summary.triggered = true;
            switch (reason) {
                case FlushReason::BranchMispredict:
                    summary.reason = "branch_mispredict";
                    break;
                case FlushReason::UnconditionalRedirect:
                    summary.reason = "unconditional_redirect";
                    break;
                case FlushReason::Trap:
                    summary.reason = "trap";
                    break;
                case FlushReason::Mret:
                    summary.reason = "mret";
                    break;
                case FlushReason::FenceI:
                    summary.reason = "fencei";
                    break;
                case FlushReason::Exception:
                    summary.reason = "exception";
                    break;
                case FlushReason::Other:
                default:
                    summary.reason = "other";
                    break;
            }
            summary.flushed_rob_entries =
                static_cast<uint64_t>(ReorderBuffer::MAX_ROB_ENTRIES - state.reorder_buffer->get_free_entry_count());
            summary.fetch_buffer_dropped = state.fetch_buffer.size();
            return summary;
        };
        PipelineTracer::FlushSummary flush_summary;

        committed_inst->set_retire_cycle(state.cycle_count);

        // 检查是否有异常
        if (committed_inst->has_exception()) {
            if (state.pipeline_tracer) {
                state.pipeline_tracer->recordCommittedInstruction(committed_inst);
            }
            LOGE(COMMIT, "commit exceptional instruction: %s", committed_inst->get_exception_message().c_str());
            handle_exception(state, committed_inst->get_exception_message(), committed_inst->get_pc());
            break;
        }

        if (committed_inst->has_trap()) {
            flush_summary = make_flush_summary(FlushReason::Trap);
            state.instruction_count++;
            state.perf_counters.increment(PerfCounterId::INSTRUCTIONS_RETIRED);
            enter_machine_trap(state,
                               committed_inst->get_pc(),
                               committed_inst->get_trap_cause(),
                               committed_inst->get_trap_tval());
            flush_summary.has_redirect_pc = true;
            flush_summary.redirect_pc = state.pc;
            syncBasicPerfCounters(state);
            state.perf_counters.increment(PerfCounterId::COMMIT_UTILIZED_SLOTS);
            committed_this_cycle++;

            if (state.pipeline_tracer) {
                state.pipeline_tracer->recordCommittedInstruction(committed_inst, flush_summary);
            }

            if (state.cpu_interface && state.cpu_interface->isDiffTestEnabled()) {
                LOGT(DIFFTEST, "inst=%" PRId64 " [COMMIT_TRACK] commit count=%" PRId64,
                     committed_inst->get_instruction_id(), state.instruction_count);
                state.cpu_interface->performDiffTestWithCommittedPC(committed_inst->get_pc());
                LOGT(COMMIT, "run difftest comparison");
            }
            break;
        }

        try {
            if (decoded_info.opcode == Opcode::STORE || decoded_info.opcode == Opcode::STORE_FP) {
                const auto& memory_info = committed_inst->get_memory_info();
                if (!memory_info.address_ready || !memory_info.is_store) {
                    handle_exception(state, "store commit missing memory info", committed_inst->get_pc());
                    break;
                }

                const uint8_t store_size = memory_info.memory_size != 0
                    ? memory_info.memory_size
                    : decoded_info.memory_access_size;

                if (state.l1d_cache) {
                    state.l1d_cache->commitStore(
                        state.memory, memory_info.memory_address, store_size, memory_info.memory_value);
                } else {
                    if (decoded_info.opcode == Opcode::STORE_FP) {
                        InstructionExecutor::storeFPToMemory(
                            state.memory, memory_info.memory_address, memory_info.memory_value, decoded_info.funct3);
                    } else {
                        InstructionExecutor::storeToMemory(
                            state.memory, memory_info.memory_address, memory_info.memory_value, decoded_info.funct3);
                    }
                }

                state.reservation_valid = false;
                state.perf_counters.increment(PerfCounterId::STORES_COMMITTED);
                LOGT(COMMIT, "inst=%" PRId64 " commit store addr=0x%" PRIx64 " value=0x%" PRIx64,
                     committed_inst->get_instruction_id(), memory_info.memory_address, memory_info.memory_value);
            } else if (decoded_info.opcode == Opcode::AMO) {
                if (!committed_inst->has_atomic_execute_info()) {
                    handle_exception(state, "amo commit missing execute info", committed_inst->get_pc());
                    break;
                }

                const auto& atomic_info = committed_inst->get_atomic_execute_info();
                if (atomic_info.acquire_reservation) {
                    state.reservation_valid = true;
                    state.reservation_addr = atomic_info.virtual_address;
                }
                if (atomic_info.release_reservation) {
                    state.reservation_valid = false;
                }
                if (atomic_info.do_store) {
                    if (state.l1d_cache) {
                        const uint8_t store_size = atomicWidthToSize(atomic_info.width);
                        state.l1d_cache->commitStore(
                            state.memory, atomic_info.physical_address, store_size, atomic_info.store_value);
                    } else {
                        writeAtomicMemoryValue(
                            state.memory,
                            atomic_info.physical_address,
                            atomic_info.width,
                            atomic_info.store_value);
                    }
                    LOGT(COMMIT, "inst=%" PRId64 " commit amo store addr=0x%" PRIx64 " value=0x%" PRIx64,
                         committed_inst->get_instruction_id(),
                         atomic_info.physical_address,
                         atomic_info.store_value);
                }
                state.perf_counters.increment(PerfCounterId::AMOS_COMMITTED);
            }
        } catch (const SimulatorException& e) {
            handle_exception(state, e.what(), committed_inst->get_pc());
            break;
        }

        bool wrote_integer_reg = false;
        if (InstructionExecutor::isFloatingPointInstruction(decoded_info)) {
            DynamicInst::FpExecuteInfo fp_info{};
            if (decoded_info.opcode == Opcode::LOAD_FP) {
                fp_info.value = committed_inst->get_result();
                fp_info.write_fp_reg = true;
            } else if (decoded_info.opcode == Opcode::STORE_FP) {
                // 无寄存器写回
            } else {
                if (!committed_inst->has_fp_execute_info()) {
                    LOGE(COMMIT, "missing fp execute info at commit, pc=0x%" PRIx64,
                         committed_inst->get_pc());
                    handle_exception(state, "missing fp execute info at commit", committed_inst->get_pc());
                    break;
                }
                fp_info = committed_inst->get_fp_execute_info();
            }

            if (fp_info.fflags != 0) {
                csr::write(state.csr_registers,
                           csr::kFflags,
                           csr::read(state.csr_registers, csr::kFflags) | fp_info.fflags);
            }

            if (fp_info.write_int_reg && decoded_info.rd != 0) {
                const uint64_t int_result = fp_info.value;
                state.arch_registers[decoded_info.rd] = int_result;
                if (committed_inst->get_physical_dest() != 0) {
                    state.register_rename->commit_instruction(committed_inst->get_logical_dest(),
                                                             committed_inst->get_physical_dest());
                }
                state.register_rename->update_architecture_register(decoded_info.rd, int_result);
                wrote_integer_reg = true;
                LOGT(COMMIT, "inst=%" PRId64 " x%d = 0x%" PRIx64,
                    committed_inst->get_instruction_id(), decoded_info.rd, int_result);
            } else if (fp_info.write_fp_reg) {
                state.arch_fp_registers[decoded_info.rd] = fp_info.value;
                LOGT(COMMIT, "inst=%" PRId64 " f%d = 0x%016" PRIx64,
                    committed_inst->get_instruction_id(), decoded_info.rd, fp_info.value);
            } else {
                LOGT(COMMIT, "inst=%" PRId64 " (no destination register)",
                    committed_inst->get_instruction_id());
            }
        } else {
            // 提交到架构寄存器
            if (decoded_info.rd != 0) {  // x0寄存器不能写入
                state.arch_registers[decoded_info.rd] = committed_inst->get_result();
                wrote_integer_reg = true;
                LOGT(COMMIT, "inst=%" PRId64 " x%d = 0x%" PRIx64,
                    committed_inst->get_instruction_id(),
                    decoded_info.rd,
                    committed_inst->get_result());
            } else {
                LOGT(COMMIT, "inst=%" PRId64 " (no destination register)",
                    committed_inst->get_instruction_id());
            }

            // 释放物理寄存器
            state.register_rename->commit_instruction(committed_inst->get_logical_dest(),
                                                     committed_inst->get_physical_dest());

            // 确保架构寄存器状态与寄存器重命名模块同步
            // 这是为了确保DiffTest比较时状态一致
            if (wrote_integer_reg) {
                state.register_rename->update_architecture_register(decoded_info.rd,
                                                                  committed_inst->get_result());
            }
        }
        
        state.instruction_count++;
        state.perf_counters.increment(PerfCounterId::INSTRUCTIONS_RETIRED);
        state.perf_counters.increment(PerfCounterId::COMMIT_UTILIZED_SLOTS);
        committed_this_cycle++;
        
        // Store Buffer清理：提交指令时，清除该指令及之前的Store条目
        // 这确保Store指令提交到内存后，相应的Store Buffer条目被清除
        state.store_buffer->retire_stores_before(committed_inst->get_instruction_id());
        state.rename_checkpoints.erase(committed_inst->get_instruction_id());

        if (committed_inst->is_load_instruction()) {
            const auto& memory_info = committed_inst->get_memory_info();
            auto& profile = state.load_profiles[committed_inst->get_pc()];
            profile.executions++;
            profile.replay_total += memory_info.replay_count;
            if (memory_info.replay_count != 0) {
                profile.replayed_loads++;
            }
            profile.replay_host_comm += memory_info.replay_host_comm_count;
            profile.replay_rob_store_amo += memory_info.replay_rob_store_amo_count;
            profile.replay_rob_store_addr_unknown += memory_info.replay_rob_store_addr_unknown_count;
            profile.replay_rob_store_overlap += memory_info.replay_rob_store_overlap_count;
            profile.replay_store_buffer_overlap += memory_info.replay_store_buffer_overlap_count;
            if (memory_info.speculated_past_addr_unknown_store) {
                profile.speculated_addr_unknown++;
                state.trainLoadAddrUnknownPredictor(committed_inst->get_pc(), true);
            }
            if (memory_info.blocked_by_addr_unknown_pair) {
                profile.blocked_addr_unknown_pair++;
            }

            switch (memory_info.load_final_source) {
                case DynamicInst::MemoryInfo::LoadFinalSource::ForwardedFull:
                    profile.forwarded_full++;
                    break;
                case DynamicInst::MemoryInfo::LoadFinalSource::ForwardedPartial:
                    profile.forwarded_partial++;
                    break;
                case DynamicInst::MemoryInfo::LoadFinalSource::FromMemory:
                    profile.from_memory++;
                    break;
                case DynamicInst::MemoryInfo::LoadFinalSource::None:
                    break;
            }
        }

        if (committed_inst->is_store_instruction()) {
            const auto& memory_info = committed_inst->get_memory_info();
            auto& profile = state.store_profiles[committed_inst->get_pc()];
            profile.executions++;
            profile.forwarded_full += memory_info.caused_forwarded_full_count;
            profile.forwarded_partial += memory_info.caused_forwarded_partial_count;
            profile.blocked_rob_addr_unknown += memory_info.caused_rob_addr_unknown_block_count;
            profile.blocked_rob_overlap += memory_info.caused_rob_overlap_block_count;
            profile.blocked_store_buffer_overlap +=
                memory_info.caused_store_buffer_overlap_block_count;
        }

        // ====== 控制流：commit仅在预测错时redirect/flush ======
        const bool is_control_flow =
            (decoded_info.opcode == Opcode::BRANCH ||
             decoded_info.opcode == Opcode::JAL ||
             decoded_info.opcode == Opcode::JALR);

        bool need_redirect_flush = false;
        uint64_t redirect_pc = 0;
        FlushReason flush_reason = FlushReason::Other;

        if (is_control_flow) {
            const uint64_t instruction_pc = committed_inst->get_pc();
            const uint64_t fallthrough = instruction_pc + (decoded_info.is_compressed ? 2ULL : 4ULL);
            const uint64_t actual_next_pc = committed_inst->is_jump()
                                                ? committed_inst->get_jump_target()
                                                : fallthrough;
            const uint64_t predicted_next_pc = committed_inst->has_predicted_next_pc()
                                                   ? committed_inst->get_predicted_next_pc()
                                                   : fallthrough;
            const bool correct = (predicted_next_pc == actual_next_pc);

            if (correct) {
                state.perf_counters.increment(PerfCounterId::PREDICTOR_CONTROL_CORRECT);
            } else {
                state.perf_counters.increment(PerfCounterId::PREDICTOR_CONTROL_INCORRECT);
            }

            // 统计：真实控制流改变（taken branch + jump）
            if (committed_inst->is_jump()) {
                state.perf_counters.increment(PerfCounterId::CONTROL_REDIRECTS);
                if (decoded_info.opcode == Opcode::JAL || decoded_info.opcode == Opcode::JALR) {
                    state.perf_counters.increment(PerfCounterId::UNCONDITIONAL_REDIRECTS);
                }
            }

            // 条件分支：预测对/错、以及BRANCH_MISPREDICTS口径统一到commit
            if (decoded_info.opcode == Opcode::BRANCH) {
                const BranchPredictor::BranchMeta* branch_meta = nullptr;
                if (committed_inst->has_branch_predict_meta()) {
                    branch_meta = &committed_inst->get_branch_predict_meta();
                }

                auto& profile = state.branch_profiles[instruction_pc];
                profile.executions++;
                if (committed_inst->is_jump()) {
                    profile.taken++;
                }
                if (predicted_next_pc != fallthrough) {
                    profile.predicted_taken++;
                }
                if (!correct) {
                    profile.mispredicts++;
                }

                if (branch_meta && branch_meta->valid) {
                    const bool actual_taken = committed_inst->is_jump();
                    const bool local_correct = (branch_meta->local_pred_taken == actual_taken);
                    const bool global_correct = (branch_meta->global_pred_taken == actual_taken);
                    const bool chooser_correct = correct;

                    if (local_correct) {
                        profile.local_correct++;
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_LOCAL_CORRECT);
                    } else {
                        profile.local_incorrect++;
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_LOCAL_INCORRECT);
                    }

                    if (global_correct) {
                        profile.global_correct++;
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_GLOBAL_CORRECT);
                    } else {
                        profile.global_incorrect++;
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_GLOBAL_INCORRECT);
                    }

                    if (branch_meta->chooser_use_global) {
                        profile.chooser_selected_global++;
                    } else {
                        profile.chooser_selected_local++;
                    }

                    if (chooser_correct) {
                        profile.chooser_correct++;
                    } else {
                        profile.chooser_incorrect++;
                    }

                    if (local_correct && global_correct) {
                        profile.both_correct++;
                    } else if (!local_correct && !global_correct) {
                        profile.both_incorrect++;
                    } else {
                        const bool chosen_component_correct =
                            branch_meta->chooser_use_global ? global_correct : local_correct;
                        if (!chosen_component_correct) {
                            profile.chooser_misses++;
                        }
                    }

                    if (branch_meta->loop_override_used) {
                        profile.loop_override_used++;
                        if (chooser_correct) {
                            profile.loop_override_correct++;
                            state.perf_counters.increment(PerfCounterId::PREDICTOR_LOOP_CORRECT);
                        } else {
                            profile.loop_override_incorrect++;
                            state.perf_counters.increment(PerfCounterId::PREDICTOR_LOOP_INCORRECT);
                        }
                    }
                }

                if (correct) {
                    state.perf_counters.increment(PerfCounterId::PREDICTOR_BHT_CORRECT);
                } else {
                    state.perf_counters.increment(PerfCounterId::PREDICTOR_BHT_INCORRECT);
                    state.recordBranchMispredict();
                }
            }

            // JALR：预测错次数
            if (decoded_info.opcode == Opcode::JALR && !correct) {
                state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_MISPREDICTS);

                const auto jalr_kind = classifyJalrKind(decoded_info);
                switch (jalr_kind) {
                    case JalrProfileKind::ReturnLike:
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_RETURN_MISPREDICTS);
                        break;
                    case JalrProfileKind::CallLike:
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_CALL_MISPREDICTS);
                        break;
                    case JalrProfileKind::Other:
                        state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_OTHER_MISPREDICTS);
                        break;
                }

                if (predicted_next_pc == fallthrough) {
                    state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_FALLTHROUGH_MISPREDICTS);
                } else {
                    state.perf_counters.increment(PerfCounterId::PREDICTOR_JALR_WRONG_TARGET_MISPREDICTS);
                }
            }

            if (decoded_info.opcode == Opcode::JALR) {
                auto& profile = state.jalr_profiles[instruction_pc];
                profile.executions++;
                if (!correct) {
                    profile.mispredicts++;
                    if (predicted_next_pc == fallthrough) {
                        profile.predicted_fallthrough++;
                    } else {
                        profile.wrong_target++;
                    }
                }

                switch (classifyJalrKind(decoded_info)) {
                    case JalrProfileKind::ReturnLike:
                        profile.return_like++;
                        break;
                    case JalrProfileKind::CallLike:
                        profile.call_like++;
                        break;
                    case JalrProfileKind::Other:
                        profile.other++;
                        break;
                }
            }

            // Commit阶段训练预测器（flush不应清空预测器状态）
            if (state.branch_predictor) {
                const bool actual_taken = committed_inst->is_jump();
                const BranchPredictor::BranchMeta* branch_meta = nullptr;
                if (decoded_info.opcode == Opcode::BRANCH && committed_inst->has_branch_predict_meta()) {
                    branch_meta = &committed_inst->get_branch_predict_meta();
                }
                state.branch_predictor->update(instruction_pc, decoded_info, actual_taken, actual_next_pc, branch_meta);

                if (!correct && decoded_info.opcode == Opcode::BRANCH &&
                    !committed_inst->is_control_recovered_early()) {
                    state.branch_predictor->recover_after_branch_mispredict(instruction_pc, actual_taken, branch_meta);
                    state.perf_counters.increment(PerfCounterId::PREDICTOR_TOURNAMENT_RECOVERIES);
                }
            }

            if (!correct && !committed_inst->is_control_recovered_early()) {
                need_redirect_flush = true;
                redirect_pc = actual_next_pc;
                flush_reason = (decoded_info.opcode == Opcode::BRANCH)
                                   ? FlushReason::BranchMispredict
                                   : FlushReason::UnconditionalRedirect;
                flush_summary = make_flush_summary(flush_reason);
                flush_summary.has_redirect_pc = true;
                flush_summary.redirect_pc = redirect_pc;
                LOGT(COMMIT,
                     "inst=%" PRId64 " control-flow mispredict: pc=0x%" PRIx64
                     " predicted_next=0x%" PRIx64 " actual_next=0x%" PRIx64 " -> redirect+flush",
                     committed_inst->get_instruction_id(), instruction_pc, predicted_next_pc, actual_next_pc);
            } else if (!correct) {
                LOGT(COMMIT,
                     "inst=%" PRId64 " control-flow mispredict already recovered early: pc=0x%" PRIx64
                     " predicted_next=0x%" PRIx64 " actual_next=0x%" PRIx64,
                     committed_inst->get_instruction_id(), instruction_pc, predicted_next_pc, actual_next_pc);
            } else {
                LOGT(COMMIT,
                     "inst=%" PRId64 " control-flow correct: pc=0x%" PRIx64
                     " next=0x%" PRIx64,
                     committed_inst->get_instruction_id(), instruction_pc, actual_next_pc);
            }
        }
        
        bool should_stop_commit = false;
        // 处理系统/特权指令
        if (decoded_info.opcode == Opcode::SYSTEM) {
            const auto& sys_inst = decoded_info;

            if (InstructionExecutor::isCsrInstruction(sys_inst)) {
                const uint32_t csr_addr = static_cast<uint32_t>(sys_inst.imm) & 0xFFFU;
                const auto csr_result = InstructionExecutor::executeCsrInstruction(
                    sys_inst, committed_inst->get_src1_value(), csr::read(state.csr_registers, csr_addr));
                writeCommittedCsr(state, csr_addr, csr_result.write_value);
                LOGT(COMMIT, "inst=%" PRId64 " commit csr[0x%03x]: old=0x%" PRIx64 ", new=0x%" PRIx64,
                     committed_inst->get_instruction_id(), csr_addr,
                     csr_result.read_value, csr_result.write_value);
            } else if (InstructionExecutor::isSystemCall(sys_inst)) {
                // ECALL
                const bool enters_trap = csr::machineTrapVectorBase(state.csr_registers) != 0;
                if (enters_trap) {
                    flush_summary = make_flush_summary(FlushReason::Trap);
                }
                should_stop_commit = handle_ecall(state, committed_inst->get_pc());
                if (enters_trap && should_stop_commit) {
                    flush_summary.has_redirect_pc = true;
                    flush_summary.redirect_pc = state.pc;
                }
            } else if (InstructionExecutor::isBreakpoint(sys_inst)) {
                // EBREAK
                flush_summary = make_flush_summary(FlushReason::Trap);
                should_stop_commit = handle_ebreak(state, committed_inst->get_pc());
                if (should_stop_commit) {
                    flush_summary.has_redirect_pc = true;
                    flush_summary.redirect_pc = state.pc;
                }
            } else if (InstructionExecutor::isMachineReturn(sys_inst)) {
                // MRET
                flush_summary = make_flush_summary(FlushReason::Mret);
                should_stop_commit = handle_mret(state);
                if (should_stop_commit) {
                    flush_summary.has_redirect_pc = true;
                    flush_summary.redirect_pc = state.pc;
                }
            }
        } else if (decoded_info.opcode == Opcode::MISC_MEM &&
                   static_cast<uint8_t>(decoded_info.funct3) == kFenceIFunct3) {
            flush_summary = make_flush_summary(FlushReason::FenceI);
            should_stop_commit = handle_fencei(state, committed_inst->get_pc(), decoded_info.is_compressed);
            if (should_stop_commit) {
                flush_summary.has_redirect_pc = true;
                flush_summary.redirect_pc = state.pc;
            }
        }

        // 提交后同步基础性能计数器，保证CSR读值与顺序核一致。
        syncBasicPerfCounters(state);

        // DiffTest: 在提交阶段所有体系结构状态更新完成后再做比较
        if (state.cpu_interface && state.cpu_interface->isDiffTestEnabled()) {
            LOGT(DIFFTEST, "inst=%" PRId64 " [COMMIT_TRACK] commit count=%" PRId64,
                committed_inst->get_instruction_id(), state.instruction_count);
            state.cpu_interface->performDiffTestWithCommittedPC(committed_inst->get_pc());
            LOGT(COMMIT, "run difftest comparison");
        }

        if (state.pipeline_tracer) {
            state.pipeline_tracer->recordCommittedInstruction(committed_inst, flush_summary);
        }

        if (need_redirect_flush) {
            state.pc = redirect_pc;
            flush_pipeline_after_commit(state, flush_reason);
            break;
        }

        if (should_stop_commit || state.halted) {
            break;
        }
        
        // 如果没有更多指令可提交，跳出循环
        if (!commit_result.has_more) {
            LOGT(COMMIT, "no more instruction can be committed");
            break;
        }
    }
}

void CommitStage::flush() {
    // 刷新提交阶段状态
    LOGT(COMMIT, "commit stage flushed");
}

void CommitStage::reset() {
    // 重置提交阶段到初始状态
    LOGT(COMMIT, "commit stage reset");
}

bool CommitStage::handle_ecall(CPUState& state, uint64_t instruction_pc) {
    // 处理系统调用
    LOGT(COMMIT, "detected ECALL at pc=0x%" PRIx64, instruction_pc);
    
    // 添加调试：显示关键寄存器值
    LOGT(COMMIT, "ecall args: a7(x17)=%" PRIx64 ", a0(x10)=%" PRIx64 ", a1(x11)=%" PRIx64 ", pc=0x%" PRIx64,
             state.arch_registers[17], state.arch_registers[10], 
             state.arch_registers[11], instruction_pc);
    
    if (csr::machineTrapVectorBase(state.csr_registers) != 0) {
        enter_machine_trap(state, instruction_pc, csr::kMachineEcallCause, 0);
        return true;
    }

    // 未设置 trap 向量时，兼容用户态程序的宿主 syscall 行为
    if (state.syscall_handler && state.cpu_interface) {
        LOGT(COMMIT, "invoke syscall handler");
        bool should_halt = state.syscall_handler->handleSyscall(state.cpu_interface);
        if (should_halt) {
            state.halted = true;
            LOGT(COMMIT, "syscall handling finished, halt program");
            return true;
        } else {
            LOGT(COMMIT, "syscall handling finished, continue execution");
            return false;
        }
    } else {
        // 降级处理：如果没有系统调用处理器，直接停机
        LOGW(COMMIT, "missing syscall handler, halt directly");
        state.halted = true;
        return true;
    }
}

bool CommitStage::handle_ebreak(CPUState& state, uint64_t instruction_pc) {
    enter_machine_trap(state, instruction_pc, csr::kBreakpointCause, instruction_pc);
    return true;
}

bool CommitStage::handle_mret(CPUState& state) {
    state.pc = csr::read(state.csr_registers, csr::kMepc);
    flush_pipeline_after_commit(state, FlushReason::Mret);
    return true;
}

bool CommitStage::handle_fencei(CPUState& state, uint64_t instruction_pc, bool is_compressed) {
    const uint64_t next_pc = instruction_pc + (is_compressed ? 2ULL : 4ULL);
    LOGT(COMMIT, "detected FENCE.I at pc=0x%" PRIx64 ", refetch from 0x%" PRIx64, instruction_pc, next_pc);
    state.pc = next_pc;
    flush_pipeline_after_commit(state, FlushReason::FenceI);
    return true;
}

void CommitStage::enter_machine_trap(CPUState& state,
                                     uint64_t instruction_pc,
                                     uint64_t cause,
                                     uint64_t tval) {
    state.pc = csr::enterMachineTrap(state.csr_registers, instruction_pc, cause, tval);
    flush_pipeline_after_commit(state, FlushReason::Trap);
}

void CommitStage::handle_exception(CPUState& state, const std::string& exception_msg, uint64_t pc) {
    LOGE(COMMIT, "exception: %s, pc=0x%" PRIx64, exception_msg.c_str(), pc);
    // 异常处理会导致流水线刷新，这需要在主控制器中处理
    state.last_halt_pc = pc;
    state.last_halt_message = exception_msg;
    state.halted = true;
}

void CommitStage::flush_pipeline_after_commit(CPUState& state, FlushReason reason) {
    LOGT(COMMIT, "serializing event committed, start pipeline flush");

    const uint64_t rob_used =
        static_cast<uint64_t>(ReorderBuffer::MAX_ROB_ENTRIES - state.reorder_buffer->get_free_entry_count());

    state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSHES);
    switch (reason) {
        case FlushReason::BranchMispredict:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_BRANCH_MISPREDICT);
            state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_BRANCH_MISPREDICT, rob_used);
            break;
        case FlushReason::UnconditionalRedirect:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_UNCONDITIONAL_REDIRECT);
            state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_UNCONDITIONAL_REDIRECT, rob_used);
            break;
        case FlushReason::Trap:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_TRAP);
            state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_TRAP, rob_used);
            break;
        case FlushReason::Mret:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_MRET);
            state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_MRET, rob_used);
            break;
        case FlushReason::FenceI:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_FENCEI);
            state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_FENCEI, rob_used);
            break;
        case FlushReason::Exception:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_EXCEPTION);
            state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_EXCEPTION, rob_used);
            break;
        case FlushReason::Other:
        default:
            state.perf_counters.increment(PerfCounterId::PIPELINE_FLUSH_OTHER);
            state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES_OTHER, rob_used);
            break;
    }

    state.perf_counters.increment(PerfCounterId::ROB_FLUSHED_ENTRIES, rob_used);

    if (state.branch_predictor && reason != FlushReason::BranchMispredict) {
        // 分支误预测由recover_after_branch_mispredict负责回滚；其它flush统一丢弃投机历史。
        state.branch_predictor->on_pipeline_flush();
    }
    
    // 1. 清空取指缓冲区（错误推测的指令）
    while (!state.fetch_buffer.empty()) {
        state.fetch_buffer.pop();
    }
    
    // 2. 刷新保留站中所有未执行的指令
    state.reservation_station->flush_pipeline();
    
    // 3. 刷新ROB中所有未提交的指令
    state.reorder_buffer->flush_pipeline();
    
    // 4. 关键修复：调用flush_pipeline方法而不是重新创建对象
    // 这样可以保留已提交的架构状态，只清除推测性重命名
    state.register_rename->flush_pipeline();
    state.rename_checkpoints.clear();
    
    // 5. 清空CDB队列（安全，因为当前指令已提交）
    while (!state.cdb_queue.empty()) {
        state.cdb_queue.pop();
    }
    
    // 6. 清空Store Buffer（刷新时清除所有推测性Store）
    state.store_buffer->flush();

    // 6.5 清空cache在途请求。FENCE.I 需要额外失效 I$ 行状态。
    if (state.l1i_cache) {
        if (reason == FlushReason::FenceI) {
            state.l1i_cache->reset();
        } else {
            state.l1i_cache->flushInFlight();
        }
    }
    if (state.l1d_cache) {
        state.l1d_cache->flushInFlight();
    }
    state.icache.reset();

    // 7. 清除LR/SC预留状态，避免被冲刷的推测性LR残留可见状态。
    state.reservation_valid = false;
    state.reservation_addr = 0;
    
    // 8. 重置所有执行单元（安全，因为当前指令已提交）
    state.resetExecutionUnits();
    
    LOGT(COMMIT, "pipeline flush completed, restart fetch");
}

} // namespace riscv 
