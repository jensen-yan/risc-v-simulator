#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "core/instruction_executor.h"
#include "system/syscall_handler.h"
#include "common/debug_types.h"
#include <iostream>
#include <fmt/format.h>

namespace riscv {

CommitStage::CommitStage() {
    // 构造函数：初始化提交阶段
}

void CommitStage::execute(CPUState& state) {
    // 添加ROB状态调试信息
    size_t free_entries = state.reorder_buffer->get_free_entry_count();
    size_t used_entries = ReorderBuffer::MAX_ROB_ENTRIES - free_entries;
    dprintf(COMMIT, "ROB状态: used=%zu/%d, empty=%s, full=%s",
        used_entries, ReorderBuffer::MAX_ROB_ENTRIES,
        (state.reorder_buffer->is_empty() ? "是" : "否"),
        (state.reorder_buffer->is_full() ? "是" : "否"));
    
    // 添加ROB状态检查
    if (state.reorder_buffer->is_empty()) {
        dprintf(COMMIT, "ROB为空，无法提交");
        return;
    }
    
    // 检查头部指令的状态
    auto head_entry_id = state.reorder_buffer->get_head_entry();
    if (head_entry_id == ReorderBuffer::MAX_ROB_ENTRIES) {
        dprintf(COMMIT, "没有有效的头部表项");
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
        dprintf(COMMIT, "头部指令 ROB[%d] Inst#%lu 状态: %s 结果准备: %s",
                head_entry_id, head_entry->get_instruction_id(), state_str,
                (head_entry->is_completed() ? "是" : "否"));
    }
    
    if (!state.reorder_buffer->can_commit()) {
        dprintf(COMMIT, "头部指令尚未完成，无法提交");
        return;
    }
    
    // 尝试提交指令
    while (state.reorder_buffer->can_commit()) {
        auto commit_result = state.reorder_buffer->commit_instruction();
        if (!commit_result.success) {
            dprintf(COMMIT, "提交失败: %s", commit_result.error_message.c_str());
            break;
        }
        
        const auto& committed_inst = commit_result.instruction;
        
        // 检查是否有异常
        if (committed_inst->has_exception()) {
            dprintf(COMMIT, "提交异常指令: %s", committed_inst->get_exception_message().c_str());
            handle_exception(state, committed_inst->get_exception_message(), committed_inst->get_pc());
            break;
        }
        
        // 提交到架构寄存器
        if (committed_inst->get_decoded_info().rd != 0) {  // x0寄存器不能写入
            state.arch_registers[committed_inst->get_decoded_info().rd] = committed_inst->get_result();
            dprintf(COMMIT, "Inst#%lu PC=0x%x x%d = 0x%x", 
                committed_inst->get_instruction_id(), committed_inst->get_pc(), 
                committed_inst->get_decoded_info().rd, committed_inst->get_result());
        } else {
            dprintf(COMMIT, "Inst#%lu PC=0x%x (无目标寄存器)", 
                committed_inst->get_instruction_id(), committed_inst->get_pc());
        }
        
        // 释放物理寄存器
        state.register_rename->commit_instruction(committed_inst->get_logical_dest(), 
                                                 committed_inst->get_physical_dest());
        
        // 确保架构寄存器状态与寄存器重命名模块同步
        // 这是为了确保DiffTest比较时状态一致
        if (committed_inst->get_decoded_info().rd != 0) {
            state.register_rename->update_architecture_register(committed_inst->get_decoded_info().rd, 
                                                              committed_inst->get_result());
        }
        
        state.instruction_count++;
        
        // Store Buffer清理：提交指令时，清除该指令及之前的Store条目
        // 这确保Store指令提交到内存后，相应的Store Buffer条目被清除
        state.store_buffer->retire_stores_before(committed_inst->get_instruction_id());
        
        // DiffTest: 当乱序CPU提交一条指令时，同步执行参考CPU并比较状态
        if (state.cpu_interface && state.cpu_interface->isDiffTestEnabled()) {
            dprintf(DIFFTEST, "[COMMIT_TRACK] 提交指令: PC=0x%x, 指令ID=%lu, 指令计数=%lu", 
                    committed_inst->get_pc(), committed_inst->get_instruction_id(), state.instruction_count);
            // 使用提交指令的PC进行DiffTest
            state.cpu_interface->performDiffTestWithCommittedPC(committed_inst->get_pc());
            dprintf(COMMIT, "执行DiffTest状态比较");
        }
        
        // 处理跳转指令：只有is_jump=true的指令才会改变PC
        if (committed_inst->is_jump()) {
            state.pc = committed_inst->get_jump_target();
            dprintf(COMMIT, "Inst#%lu 跳转到 0x%x", 
               committed_inst->get_instruction_id(), committed_inst->get_jump_target());
            
            // 跳转指令提交后，刷新流水线中错误推测的指令
            flush_pipeline_after_commit(state);
        }
        
        // 处理系统调用
        if (committed_inst->get_decoded_info().opcode == Opcode::SYSTEM) {
            if (InstructionExecutor::isSystemCall(committed_inst->get_decoded_info())) {
                // ECALL
                handle_ecall(state, committed_inst->get_pc());
            } else if (InstructionExecutor::isBreakpoint(committed_inst->get_decoded_info())) {
                // EBREAK
                handle_ebreak(state);
            }
        }
        
        // 如果没有更多指令可提交，跳出循环
        if (!commit_result.has_more) {
            dprintf(COMMIT, "没有更多指令可提交");
            break;
        }
    }
}

void CommitStage::flush() {
    // 刷新提交阶段状态
    dprintf(COMMIT, "提交阶段已刷新");
}

void CommitStage::reset() {
    // 重置提交阶段到初始状态
    dprintf(COMMIT, "提交阶段已重置");
}

void CommitStage::handle_ecall(CPUState& state, uint32_t instruction_pc) {
    // 处理系统调用
    dprintf(COMMIT, "检测到ECALL系统调用，指令PC=0x%x", instruction_pc);
    
    // 添加调试：显示关键寄存器值
    dprintf(COMMIT, "ECALL调试: a7(x17)=%d, a0(x10)=%d, a1(x11)=%d, 指令PC=0x%x",
             state.arch_registers[17], state.arch_registers[10], 
             state.arch_registers[11], instruction_pc);
    
    // 调用系统调用处理器
    if (state.syscall_handler && state.cpu_interface) {
        dprintf(COMMIT, "调用系统调用处理器");
        bool should_halt = state.syscall_handler->handleSyscall(state.cpu_interface);
        if (should_halt) {
            state.halted = true;
            dprintf(COMMIT, "系统调用处理完成，程序停机");
        } else {
            dprintf(COMMIT, "系统调用处理完成，继续执行");
        }
    } else {
        // 降级处理：如果没有系统调用处理器，直接停机
        dprintf(COMMIT, "警告：缺少系统调用处理器，直接停机");
        state.halted = true;
    }
}

void CommitStage::handle_ebreak(CPUState& state) {
    dprintf(COMMIT, "遇到断点指令，停止执行");
    state.halted = true;
}

void CommitStage::handle_exception(CPUState& state, const std::string& exception_msg, uint32_t pc) {
    std::cerr << "异常: " << exception_msg << ", PC=0x" << std::hex << pc << std::dec << std::endl;
    // 异常处理会导致流水线刷新，这需要在主控制器中处理
    state.halted = true;
}

void CommitStage::flush_pipeline_after_commit(CPUState& state) {
    dprintf(COMMIT, "跳转指令已提交，开始刷新流水线");
    
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
    
    dprintf(COMMIT, "流水线刷新完成，重新开始取指");
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