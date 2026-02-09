#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "core/instruction_executor.h"
#include "system/syscall_handler.h"
#include "common/debug_types.h"
#include <iostream>
#include <fmt/format.h>

namespace riscv {

namespace {
constexpr uint32_t kFflagsCsr = 0x001;
constexpr uint32_t kFrmCsr = 0x002;
constexpr uint32_t kFcsrCsr = 0x003;

uint64_t readCsrWithAlias(const std::array<uint64_t, 4096>& csr, uint32_t addr) {
    if (addr == kFflagsCsr) {
        return csr[kFcsrCsr] & 0x1FU;
    }
    if (addr == kFrmCsr) {
        return (csr[kFcsrCsr] >> 5) & 0x7U;
    }
    return csr[addr];
}

void writeCsrWithAlias(std::array<uint64_t, 4096>& csr, uint32_t addr, uint64_t value) {
    if (addr == kFflagsCsr) {
        const uint64_t fflags = value & 0x1FU;
        csr[kFflagsCsr] = fflags;
        csr[kFcsrCsr] = (csr[kFcsrCsr] & ~0x1FU) | fflags;
        return;
    }
    if (addr == kFrmCsr) {
        const uint64_t frm = value & 0x7U;
        csr[kFrmCsr] = frm;
        csr[kFcsrCsr] = (csr[kFcsrCsr] & ~0xE0U) | (frm << 5);
        return;
    }
    if (addr == kFcsrCsr) {
        const uint64_t fcsr = value & 0xFFU;
        csr[kFcsrCsr] = fcsr;
        csr[kFflagsCsr] = fcsr & 0x1FU;
        csr[kFrmCsr] = (fcsr >> 5) & 0x7U;
        return;
    }
    csr[addr] = value;
}
}  // namespace

CommitStage::CommitStage() {
    // 构造函数：初始化提交阶段
}

void CommitStage::execute(CPUState& state) {
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
    while (state.reorder_buffer->can_commit()) {
        auto commit_result = state.reorder_buffer->commit_instruction();
        if (!commit_result.success) {
            LOGW(COMMIT, "commit failed: %s", commit_result.error_message.c_str());
            break;
        }
        
        const auto& committed_inst = commit_result.instruction;
        const auto& decoded_info = committed_inst->get_decoded_info();
        
        // 检查是否有异常
        if (committed_inst->has_exception()) {
            LOGE(COMMIT, "commit exceptional instruction: %s", committed_inst->get_exception_message().c_str());
            handle_exception(state, committed_inst->get_exception_message(), committed_inst->get_pc());
            break;
        }

        bool wrote_integer_reg = false;
        if (decoded_info.opcode == Opcode::OP_FP) {
            const auto fp_result = InstructionExecutor::executeFPOperation(
                decoded_info,
                state.arch_fp_registers[decoded_info.rs1],
                state.arch_fp_registers[decoded_info.rs2],
                state.arch_registers[decoded_info.rs1]);

            if (fp_result.write_int_reg && decoded_info.rd != 0) {
                const uint64_t int_result = committed_inst->get_result();
                state.arch_registers[decoded_info.rd] = int_result;
                wrote_integer_reg = true;
                LOGT(COMMIT, "inst=%" PRId64 " x%d = 0x%" PRIx64,
                    committed_inst->get_instruction_id(),
                    decoded_info.rd,
                    int_result);

                // 写整数寄存器的OP_FP指令仍使用整数重命名链路。
                state.register_rename->commit_instruction(committed_inst->get_logical_dest(),
                                                         committed_inst->get_physical_dest());
                state.register_rename->update_architecture_register(decoded_info.rd, int_result);
            } else if (fp_result.write_fp_reg) {
                state.arch_fp_registers[decoded_info.rd] = static_cast<uint32_t>(fp_result.value);
                LOGT(COMMIT, "inst=%" PRId64 " f%d = 0x%08" PRIx32,
                    committed_inst->get_instruction_id(),
                    decoded_info.rd,
                    static_cast<uint32_t>(fp_result.value));
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
        
        // Store Buffer清理：提交指令时，清除该指令及之前的Store条目
        // 这确保Store指令提交到内存后，相应的Store Buffer条目被清除
        state.store_buffer->retire_stores_before(committed_inst->get_instruction_id());
        
        // 处理跳转指令：只有is_jump=true的指令才会改变PC
        if (committed_inst->is_jump()) {
            state.pc = committed_inst->get_jump_target();
            LOGT(COMMIT, "inst=%" PRId64 " jump to 0x%" PRIx64,
                committed_inst->get_instruction_id(), committed_inst->get_jump_target());
            
            // 跳转指令提交后，刷新流水线中错误推测的指令
            flush_pipeline_after_commit(state);
        }
        
        // 处理系统调用
        if (decoded_info.opcode == Opcode::SYSTEM) {
            const auto& sys_inst = decoded_info;

            if (InstructionExecutor::isCsrInstruction(sys_inst)) {
                const uint32_t csr_addr = static_cast<uint32_t>(sys_inst.imm) & 0xFFFU;
                const auto csr_result = InstructionExecutor::executeCsrInstruction(
                    sys_inst, committed_inst->get_src1_value(), readCsrWithAlias(state.csr_registers, csr_addr));
                writeCsrWithAlias(state.csr_registers, csr_addr, csr_result.write_value);
                LOGT(COMMIT, "inst=%" PRId64 " commit csr[0x%03x]: old=0x%" PRIx64 ", new=0x%" PRIx64,
                     committed_inst->get_instruction_id(), csr_addr,
                     csr_result.read_value, csr_result.write_value);
            } else if (InstructionExecutor::isSystemCall(sys_inst)) {
                // ECALL
                handle_ecall(state, committed_inst->get_pc());
            } else if (InstructionExecutor::isBreakpoint(sys_inst)) {
                // EBREAK
                handle_ebreak(state);
            }
        }

        // DiffTest: 在提交阶段所有体系结构状态更新完成后再做比较
        if (state.cpu_interface && state.cpu_interface->isDiffTestEnabled()) {
            LOGT(DIFFTEST, "inst=%" PRId64 " [COMMIT_TRACK] commit count=%" PRId64,
                committed_inst->get_instruction_id(), state.instruction_count);
            state.cpu_interface->performDiffTestWithCommittedPC(committed_inst->get_pc());
            LOGT(COMMIT, "run difftest comparison");
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

void CommitStage::handle_ecall(CPUState& state, uint64_t instruction_pc) {
    // 处理系统调用
    LOGT(COMMIT, "detected ECALL at pc=0x%" PRIx64, instruction_pc);
    
    // 添加调试：显示关键寄存器值
    LOGT(COMMIT, "ecall args: a7(x17)=%" PRIx64 ", a0(x10)=%" PRIx64 ", a1(x11)=%" PRIx64 ", pc=0x%" PRIx64,
             state.arch_registers[17], state.arch_registers[10], 
             state.arch_registers[11], instruction_pc);
    
    // 调用系统调用处理器
    if (state.syscall_handler && state.cpu_interface) {
        LOGT(COMMIT, "invoke syscall handler");
        bool should_halt = state.syscall_handler->handleSyscall(state.cpu_interface);
        if (should_halt) {
            state.halted = true;
            LOGT(COMMIT, "syscall handling finished, halt program");
        } else {
            LOGT(COMMIT, "syscall handling finished, continue execution");
        }
    } else {
        // 降级处理：如果没有系统调用处理器，直接停机
        LOGW(COMMIT, "missing syscall handler, halt directly");
        state.halted = true;
    }
}

void CommitStage::handle_ebreak(CPUState& state) {
    LOGT(COMMIT, "encountered EBREAK, halt execution");
    state.halted = true;
}

void CommitStage::handle_exception(CPUState& state, const std::string& exception_msg, uint64_t pc) {
    LOGE(COMMIT, "exception: %s, pc=0x%" PRIx64, exception_msg.c_str(), pc);
    // 异常处理会导致流水线刷新，这需要在主控制器中处理
    state.halted = true;
}

void CommitStage::flush_pipeline_after_commit(CPUState& state) {
    LOGT(COMMIT, "jump committed, start pipeline flush");
    
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
    
    // 5. 清空CDB队列（安全，因为当前指令已提交）
    while (!state.cdb_queue.empty()) {
        state.cdb_queue.pop();
    }
    
    // 6. 清空Store Buffer（刷新时清除所有推测性Store）
    state.store_buffer->flush();
    
    // 7. 重置所有执行单元（安全，因为当前指令已提交）
    reset_execution_units(state);
    
    LOGT(COMMIT, "pipeline flush completed, restart fetch");
}

void CommitStage::reset_execution_units(CPUState& state) {
    // 重置所有执行单元
    for (auto& unit : state.alu_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    for (auto& unit : state.branch_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    for (auto& unit : state.load_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
    
    for (auto& unit : state.store_units) {
        unit.busy = false;
        unit.remaining_cycles = 0;
        unit.has_exception = false;
        unit.is_jump = false;
        unit.jump_target = 0;
    }
}

} // namespace riscv 
