#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/commit_control_flow_effects.h"
#include "cpu/ooo/commit_memory_effects.h"
#include "cpu/ooo/commit_register_effects.h"
#include "cpu/ooo/commit_retire_effects.h"
#include "cpu/ooo/commit_system_effects.h"
#include "cpu/ooo/dynamic_inst.h"
#include "core/csr_utils.h"
#include "common/debug_types.h"

#include <algorithm>

namespace riscv {

namespace {
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
    size_t store_memory_ports_used = 0;
    while (context.canCommit() &&
           committed_this_cycle < effective_commit_width) {
        const auto next_head_entry_id = context.robHeadEntry();
        const auto next_commit_inst = context.robEntry(next_head_entry_id);
        if (CommitMemoryEffects::usesStoreMemoryPort(next_commit_inst) &&
            store_memory_ports_used >= OOOPipelineConfig::STORE_COMMIT_WIDTH) {
            state.recordPipelineStall(PerfCounterId::STALL_COMMIT_STORE_PORT_BUSY);
            LOGT(COMMIT,
                 "store memory commit ports exhausted: inst=%" PRId64
                 " rob[%d] used=%zu/%zu",
                 next_commit_inst->get_instruction_id(),
                 next_head_entry_id,
                 store_memory_ports_used,
                 OOOPipelineConfig::STORE_COMMIT_WIDTH);
            break;
        }

        auto commit_result = context.commitInstruction();
        if (!commit_result.success) {
            LOGW(COMMIT, "commit failed: %s", commit_result.error_message.c_str());
            break;
        }
        
        const auto& committed_inst = commit_result.instruction;
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
            CommitSystemEffects::enterMachineTrap(state,
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
            if (memory_effect.used_store_memory_port) {
                ++store_memory_ports_used;
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
        
        const auto system_effect = CommitSystemEffects::apply(state, committed_inst);
        if (system_effect.has_flush_summary) {
            flush_summary = make_flush_summary(system_effect.flush_reason);
            if (system_effect.has_redirect_pc) {
                flush_summary.has_redirect_pc = true;
                flush_summary.redirect_pc = system_effect.redirect_pc;
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
            CommitSystemEffects::flushPipelineAfterCommit(state, control_flow_effect.flush_reason);
            break;
        }

        if (system_effect.should_stop_commit || state.halted) {
            break;
        }
        
        // 如果没有更多指令可提交，跳出循环
        if (!commit_result.has_more) {
            LOGT(COMMIT, "no more instruction can be committed");
            break;
        }
    }
}

void CommitStage::handle_exception(CPUState& state, const std::string& exception_msg, uint64_t pc) {
    LOGE(COMMIT, "exception: %s, pc=0x%" PRIx64, exception_msg.c_str(), pc);
    // 异常处理会导致流水线刷新，这需要在主控制器中处理
    state.last_halt_pc = pc;
    state.last_halt_message = exception_msg;
    state.halted = true;
}

} // namespace riscv 
