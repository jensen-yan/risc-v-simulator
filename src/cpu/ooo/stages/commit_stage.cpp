#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/commit_control_flow_effects.h"
#include "cpu/ooo/commit_memory_effects.h"
#include "cpu/ooo/commit_register_effects.h"
#include "cpu/ooo/commit_retire_effects.h"
#include "cpu/ooo/dynamic_inst.h"
#include "core/csr_utils.h"
#include "core/instruction_executor.h"
#include "common/debug_types.h"

#include <algorithm>

namespace riscv {

namespace {
constexpr uint8_t kFenceIFunct3 = 0b001;
constexpr uint32_t kMstatusCsrAddr = 0x300;
constexpr uint32_t kMcycleCsrAddr = 0xB00;
constexpr uint32_t kMinstretCsrAddr = 0xB02;
constexpr uint32_t kCycleCsrAddr = 0xC00;
constexpr uint32_t kInstretCsrAddr = 0xC02;

void syncBasicPerfCounters(CPUState& state) {
    // 与 in-order 保持一致：四个基础计数器统一跟随已提交指令数。
    const uint64_t retired = state.instruction_count;
    csr::write(state.csr_registers, kMcycleCsrAddr, retired);
    csr::write(state.csr_registers, kMinstretCsrAddr, retired);
    csr::write(state.csr_registers, kCycleCsrAddr, retired);
    csr::write(state.csr_registers, kInstretCsrAddr, retired);
}

void writeCommittedCsr(CPUState& state, uint32_t csr_addr, uint64_t value) {
    if (state.cpu_interface) {
        state.cpu_interface->setCSR(csr_addr, value);
        return;
    }
    csr::write(state.csr_registers, csr_addr, value);
}

}  // namespace

CommitStage::CommitStage() {
    // 构造函数：初始化提交阶段
}

size_t CommitStage::Context::effectiveCommitWidth() const {
    return (state_.commit_width_override == 0)
        ? OOOPipelineConfig::COMMIT_WIDTH
        : std::min(OOOPipelineConfig::COMMIT_WIDTH, state_.commit_width_override);
}

size_t CommitStage::Context::reorderBufferUsedEntryCount() const {
    return ReorderBuffer::MAX_ROB_ENTRIES - reorderBufferFreeEntryCount();
}

void CommitStage::execute(Context& context) {
    CPUState& state = context.stateForLegacyCommitInternals();
    const size_t effective_commit_width = context.effectiveCommitWidth();

    context.incrementCounter(PerfCounterId::COMMIT_SLOTS, effective_commit_width);

    // 添加ROB状态调试信息
    size_t used_entries = context.reorderBufferUsedEntryCount();
    LOGT(COMMIT, "rob state: used=%zu/%d, empty=%s, full=%s",
        used_entries, ReorderBuffer::MAX_ROB_ENTRIES,
        (context.reorderBufferEmpty() ? "yes" : "no"),
        (context.reorderBufferFull() ? "yes" : "no"));
    
    // 添加ROB状态检查
    if (context.reorderBufferEmpty()) {
        LOGT(COMMIT, "rob empty, cannot commit");
        return;
    }
    
    // 检查头部指令的状态
    auto head_entry_id = context.robHeadEntry();
    if (head_entry_id == ReorderBuffer::MAX_ROB_ENTRIES) {
        LOGT(COMMIT, "no valid head entry");
        return;
    }
    
    const auto& head_entry = context.robEntry(head_entry_id);
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
    
    if (!context.canCommit()) {
        LOGT(COMMIT, "head instruction not completed, cannot commit");
        return;
    }
    
    // 尝试提交指令
    size_t committed_this_cycle = 0;
    while (context.canCommit() &&
           committed_this_cycle < effective_commit_width) {
        auto commit_result = context.commitInstruction();
        if (!commit_result.success) {
            LOGW(COMMIT, "commit failed: %s", commit_result.error_message.c_str());
            break;
        }
        
        const auto& committed_inst = commit_result.instruction;
        const auto& decoded_info = committed_inst->get_decoded_info();
        auto make_flush_summary = [&](OooRecovery::Reason reason) {
            PipelineTracer::FlushSummary summary;
            summary.triggered = true;
            summary.reason = OooRecovery::reasonName(reason);
            summary.flushed_rob_entries =
                static_cast<uint64_t>(ReorderBuffer::MAX_ROB_ENTRIES - state.reorder_buffer->get_free_entry_count());
            summary.fetch_buffer_dropped = state.fetch_buffer.size();
            return summary;
        };
        PipelineTracer::FlushSummary flush_summary;

        committed_inst->set_retire_cycle(context.cycleCount());

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
            flush_summary = make_flush_summary(OooRecovery::Reason::Trap);
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
            const auto memory_effect = CommitMemoryEffects::apply(state, committed_inst);
            if (!memory_effect.success) {
                handle_exception(state, memory_effect.error_message, committed_inst->get_pc());
                break;
            }
        } catch (const SimulatorException& e) {
            handle_exception(state, e.what(), committed_inst->get_pc());
            break;
        }

        const auto register_effect = CommitRegisterEffects::apply(state, committed_inst);
        if (!register_effect.success) {
            handle_exception(state, register_effect.error_message, committed_inst->get_pc());
            break;
        }
        
        state.instruction_count++;
        state.perf_counters.increment(PerfCounterId::INSTRUCTIONS_RETIRED);
        state.perf_counters.increment(PerfCounterId::COMMIT_UTILIZED_SLOTS);
        committed_this_cycle++;
        
        CommitRetireEffects::afterInstructionRetired(state, committed_inst);

        const auto control_flow_effect = CommitControlFlowEffects::apply(state, committed_inst);
        if (control_flow_effect.needs_redirect_flush) {
            flush_summary = make_flush_summary(control_flow_effect.flush_reason);
            flush_summary.has_redirect_pc = true;
            flush_summary.redirect_pc = control_flow_effect.redirect_pc;
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
                    flush_summary = make_flush_summary(OooRecovery::Reason::Trap);
                }
                should_stop_commit = handle_ecall(state, committed_inst->get_pc());
                if (enters_trap && should_stop_commit) {
                    flush_summary.has_redirect_pc = true;
                    flush_summary.redirect_pc = state.pc;
                }
            } else if (InstructionExecutor::isBreakpoint(sys_inst)) {
                // EBREAK
                flush_summary = make_flush_summary(OooRecovery::Reason::Trap);
                should_stop_commit = handle_ebreak(state, committed_inst->get_pc());
                if (should_stop_commit) {
                    flush_summary.has_redirect_pc = true;
                    flush_summary.redirect_pc = state.pc;
                }
            } else if (InstructionExecutor::isMachineReturn(sys_inst)) {
                // MRET
                flush_summary = make_flush_summary(OooRecovery::Reason::Mret);
                should_stop_commit = handle_mret(state);
                if (should_stop_commit) {
                    flush_summary.has_redirect_pc = true;
                    flush_summary.redirect_pc = state.pc;
                }
            }
        } else if (decoded_info.opcode == Opcode::MISC_MEM &&
                   static_cast<uint8_t>(decoded_info.funct3) == kFenceIFunct3) {
            flush_summary = make_flush_summary(OooRecovery::Reason::FenceI);
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

        if (control_flow_effect.needs_redirect_flush) {
            state.pc = control_flow_effect.redirect_pc;
            flush_pipeline_after_commit(state, control_flow_effect.flush_reason);
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
    uint64_t mstatus = csr::read(state.csr_registers, kMstatusCsrAddr);
    const auto restored_mode = applyMretPrivilegeMode(mstatus);
    if (!restored_mode.has_value()) {
        throw IllegalInstructionException("MRET mstatus.MPP 非法");
    }
    writeCommittedCsr(state, kMstatusCsrAddr, mstatus);
    if (state.cpu_interface) {
        state.cpu_interface->setPrivilegeMode(*restored_mode);
    } else if (state.privilege_state) {
        state.privilege_state->setMode(*restored_mode);
    }
    state.pc = csr::read(state.csr_registers, csr::kMepc);
    flush_pipeline_after_commit(state, OooRecovery::Reason::Mret);
    return true;
}

bool CommitStage::handle_fencei(CPUState& state, uint64_t instruction_pc, bool is_compressed) {
    const uint64_t next_pc = instruction_pc + (is_compressed ? 2ULL : 4ULL);
    LOGT(COMMIT, "detected FENCE.I at pc=0x%" PRIx64 ", refetch from 0x%" PRIx64, instruction_pc, next_pc);
    state.pc = next_pc;
    flush_pipeline_after_commit(state, OooRecovery::Reason::FenceI);
    return true;
}

void CommitStage::enter_machine_trap(CPUState& state,
                                     uint64_t instruction_pc,
                                     uint64_t cause,
                                     uint64_t tval) {
    PrivilegeMode current_mode = PrivilegeMode::MACHINE;
    if (state.cpu_interface) {
        current_mode = state.cpu_interface->getPrivilegeMode();
    } else if (state.privilege_state) {
        current_mode = state.privilege_state->getMode();
    }

    state.pc = csr::enterMachineTrap(state.csr_registers, instruction_pc, cause, tval, current_mode);
    if (state.cpu_interface) {
        state.cpu_interface->setPrivilegeMode(PrivilegeMode::MACHINE);
        state.cpu_interface->setCSR(csr::kMstatus, csr::read(state.csr_registers, csr::kMstatus));
    } else if (state.privilege_state) {
        state.privilege_state->setMode(PrivilegeMode::MACHINE);
        state.privilege_state->setMstatus(csr::read(state.csr_registers, csr::kMstatus));
    }
    flush_pipeline_after_commit(state, OooRecovery::Reason::Trap);
}

void CommitStage::handle_exception(CPUState& state, const std::string& exception_msg, uint64_t pc) {
    LOGE(COMMIT, "exception: %s, pc=0x%" PRIx64, exception_msg.c_str(), pc);
    // 异常处理会导致流水线刷新，这需要在主控制器中处理
    state.last_halt_pc = pc;
    state.last_halt_message = exception_msg;
    state.halted = true;
}

void CommitStage::flush_pipeline_after_commit(CPUState& state, OooRecovery::Reason reason) {
    LOGT(COMMIT, "serializing event committed, start pipeline flush");
    OooRecovery::FullPipelineRequest request;
    request.reason = reason;
    request.clear_reservation = true;
    request.reset_execution_units = true;
    OooRecovery::recoverFullPipeline(state, request);
    LOGT(COMMIT, "pipeline flush completed, restart fetch");
}

} // namespace riscv 
