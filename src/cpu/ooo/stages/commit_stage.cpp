#include "cpu/ooo/stages/commit_stage.h"
#include "core/instruction_executor.h"
#include "system/syscall_handler.h"
#include "common/debug_types.h"
#include <iostream>
#include <sstream>

namespace riscv {

CommitStage::CommitStage() {
    // 构造函数：初始化提交阶段
}

void CommitStage::execute(CPUState& state) {
    print_stage_activity("开始提交阶段", state.cycle_count, state.pc);
    
    // 添加ROB状态调试信息
    size_t free_entries = state.reorder_buffer->get_free_entry_count();
    size_t used_entries = ReorderBuffer::MAX_ROB_ENTRIES - free_entries;
    print_stage_activity(
        std::string("ROB状态: used=") + std::to_string(used_entries) + 
        "/" + std::to_string(ReorderBuffer::MAX_ROB_ENTRIES) + 
        ", empty=" + (state.reorder_buffer->is_empty() ? "是" : "否") + 
        ", full=" + (state.reorder_buffer->is_full() ? "是" : "否"),
        state.cycle_count, state.pc);
    
    // 添加ROB状态检查
    if (state.reorder_buffer->is_empty()) {
        print_stage_activity("ROB为空，无法提交", state.cycle_count, state.pc);
        return;
    }
    
    // 检查头部指令的状态
    auto head_entry_id = state.reorder_buffer->get_head_entry();
    if (head_entry_id == ReorderBuffer::MAX_ROB_ENTRIES) {
        print_stage_activity("没有有效的头部表项", state.cycle_count, state.pc);
        return;
    }
    
    const auto& head_entry = state.reorder_buffer->get_entry(head_entry_id);
    std::string state_str;
    switch (head_entry.state) {
        case ReorderBufferEntry::State::ALLOCATED: state_str = "ALLOCATED"; break;
        case ReorderBufferEntry::State::ISSUED: state_str = "ISSUED"; break;
        case ReorderBufferEntry::State::EXECUTING: state_str = "EXECUTING"; break;
        case ReorderBufferEntry::State::COMPLETED: state_str = "COMPLETED"; break;
        case ReorderBufferEntry::State::RETIRED: state_str = "RETIRED"; break;
    }
    
    print_stage_activity("头部指令 ROB[" + std::to_string(head_entry_id) + 
                        "] Inst#" + std::to_string(head_entry.instruction_id) + 
                        " 状态: " + state_str + 
                        " 结果准备: " + (head_entry.result_ready ? "是" : "否"), 
                        state.cycle_count, state.pc);
    
    if (!state.reorder_buffer->can_commit()) {
        print_stage_activity("头部指令尚未完成，无法提交", state.cycle_count, state.pc);
        return;
    }
    
    // 尝试提交指令
    while (state.reorder_buffer->can_commit()) {
        auto commit_result = state.reorder_buffer->commit_instruction();
        if (!commit_result.success) {
            print_stage_activity("提交失败: " + commit_result.error_message, 
                                state.cycle_count, state.pc);
            break;
        }
        
        const auto& committed_inst = commit_result.instruction;
        
        // 检查是否有异常
        if (committed_inst.has_exception) {
            print_stage_activity("提交异常指令: " + committed_inst.exception_msg, 
                                state.cycle_count, state.pc);
            handle_exception(state, committed_inst.exception_msg, committed_inst.pc);
            break;
        }
        
        // 提交到架构寄存器
        if (committed_inst.instruction.rd != 0) {  // x0寄存器不能写入
            state.arch_registers[committed_inst.instruction.rd] = committed_inst.result;
            std::string msg = std::format("Inst#{} PC=0x{:x} x{} = 0x{:x}", 
                committed_inst.instruction_id, committed_inst.pc, 
                committed_inst.instruction.rd, committed_inst.result);
            print_stage_activity(msg, state.cycle_count, state.pc);
        } else {
            std::string msg = std::format("Inst#{} PC=0x{:x} (无目标寄存器)", 
                committed_inst.instruction_id, committed_inst.pc);
            print_stage_activity(msg, state.cycle_count, state.pc);
        }
        
        // 释放物理寄存器
        state.register_rename->commit_instruction(committed_inst.logical_dest, 
                                                 committed_inst.physical_dest);
        
        // 确保架构寄存器状态与寄存器重命名模块同步
        // 这是为了确保DiffTest比较时状态一致
        if (committed_inst.instruction.rd != 0) {
            state.register_rename->update_architecture_register(committed_inst.instruction.rd, 
                                                              committed_inst.result);
        }
        
        state.instruction_count++;
        
        // 更新最近提交指令的PC
        state.last_committed_pc = committed_inst.pc;
        
        // DiffTest: 当乱序CPU提交一条指令时，同步执行参考CPU并比较状态
        if (state.cpu_interface && state.cpu_interface->isDiffTestEnabled()) {
            dprintf(DIFFTEST, "[COMMIT_TRACK] 提交指令: PC=0x%x, 指令ID=%llu, 指令计数=%llu", 
                    committed_inst.pc, committed_inst.instruction_id, state.instruction_count);
            state.cpu_interface->performDiffTest();
            print_stage_activity("执行DiffTest状态比较", state.cycle_count, committed_inst.pc);
        }
        
        // 处理跳转指令：只有is_jump=true的指令才会改变PC
        if (committed_inst.is_jump) {
            state.pc = committed_inst.jump_target;
            std::stringstream ss;
            ss << "Inst#" << committed_inst.instruction_id << " 跳转到 0x" 
               << std::hex << committed_inst.jump_target;
            print_stage_activity(ss.str(), state.cycle_count, state.pc);
            
            // 跳转指令提交后，刷新流水线中错误推测的指令
            flush_pipeline_after_commit(state);
        }
        
        // 处理系统调用
        if (committed_inst.instruction.opcode == Opcode::SYSTEM) {
            if (InstructionExecutor::isSystemCall(committed_inst.instruction)) {
                // ECALL
                handle_ecall(state, committed_inst.pc);
            } else if (InstructionExecutor::isBreakpoint(committed_inst.instruction)) {
                // EBREAK
                handle_ebreak(state);
            }
        }
        
        // 如果没有更多指令可提交，跳出循环
        if (!commit_result.has_more) {
            print_stage_activity("没有更多指令可提交", state.cycle_count, state.pc);
            break;
        }
    }
}

void CommitStage::flush() {
    // 刷新提交阶段状态
    print_stage_activity("提交阶段已刷新", 0, 0);
}

void CommitStage::reset() {
    // 重置提交阶段到初始状态
    print_stage_activity("提交阶段已重置", 0, 0);
}

void CommitStage::handle_ecall(CPUState& state, uint32_t instruction_pc) {
    // 处理系统调用
    std::stringstream ss;
    ss << "检测到ECALL系统调用，指令PC=0x" << std::hex << instruction_pc;
    print_stage_activity(ss.str(), state.cycle_count, instruction_pc);
    
    // 添加调试：显示关键寄存器值
    std::stringstream debug_ss;
    debug_ss << "ECALL调试: a7(x17)=" << std::dec << state.arch_registers[17] 
             << ", a0(x10)=" << state.arch_registers[10] 
             << ", a1(x11)=" << state.arch_registers[11] 
             << ", 指令PC=0x" << std::hex << instruction_pc;
    print_stage_activity(debug_ss.str(), state.cycle_count, instruction_pc);
    
    // 调用系统调用处理器
    if (state.syscall_handler && state.cpu_interface) {
        print_stage_activity("调用系统调用处理器", state.cycle_count, instruction_pc);
        bool should_halt = state.syscall_handler->handleSyscall(state.cpu_interface);
        if (should_halt) {
            state.halted = true;
            print_stage_activity("系统调用处理完成，程序停机", state.cycle_count, instruction_pc);
        } else {
            print_stage_activity("系统调用处理完成，继续执行", state.cycle_count, instruction_pc);
        }
    } else {
        // 降级处理：如果没有系统调用处理器，直接停机
        print_stage_activity("警告：缺少系统调用处理器，直接停机", state.cycle_count, instruction_pc);
        state.halted = true;
    }
}

void CommitStage::handle_ebreak(CPUState& state) {
    print_stage_activity("遇到断点指令，停止执行", state.cycle_count, state.pc);
    state.halted = true;
}

void CommitStage::handle_exception(CPUState& state, const std::string& exception_msg, uint32_t pc) {
    std::cerr << "异常: " << exception_msg << ", PC=0x" << std::hex << pc << std::dec << std::endl;
    // 异常处理会导致流水线刷新，这需要在主控制器中处理
    state.halted = true;
}

void CommitStage::flush_pipeline_after_commit(CPUState& state) {
    print_stage_activity("跳转指令已提交，开始刷新流水线", state.cycle_count, state.pc);
    
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
    
    // 6. 重置所有执行单元（安全，因为当前指令已提交）
    reset_execution_units(state);
    
    print_stage_activity("流水线刷新完成，重新开始取指", state.cycle_count, state.pc);
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

void CommitStage::print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(get_stage_name(), activity, cycle, pc);
}

} // namespace riscv 