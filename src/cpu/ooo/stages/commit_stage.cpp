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
            print_stage_activity("Inst#" + std::to_string(committed_inst.instruction_id) + 
                                " x" + std::to_string(committed_inst.instruction.rd) + 
                                " = 0x" + std::to_string(committed_inst.result), 
                                state.cycle_count, state.pc);
        } else {
            print_stage_activity("Inst#" + std::to_string(committed_inst.instruction_id) + 
                                " (无目标寄存器)", state.cycle_count, state.pc);
        }
        
        // 释放物理寄存器
        state.register_rename->commit_instruction(committed_inst.logical_dest, 
                                                 committed_inst.physical_dest);
        
        state.instruction_count++;
        
        // 处理跳转指令：只有is_jump=true的指令才会改变PC
        if (committed_inst.is_jump) {
            state.pc = committed_inst.jump_target;
            std::stringstream ss;
            ss << "Inst#" << committed_inst.instruction_id << " 跳转到 0x" 
               << std::hex << committed_inst.jump_target;
            print_stage_activity(ss.str(), state.cycle_count, state.pc);
        }
        
        // 处理系统调用
        if (committed_inst.instruction.opcode == Opcode::SYSTEM) {
            if (InstructionExecutor::isSystemCall(committed_inst.instruction)) {
                // ECALL
                handle_ecall(state);
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

void CommitStage::handle_ecall(CPUState& state) {
    // 处理系统调用
    // TODO: 需要设计更好的接口来传递CPU引用
    // 暂时简化处理：只是标记需要处理系统调用，由主控制器处理
    print_stage_activity("检测到ECALL系统调用，标记待处理", state.cycle_count, state.pc);
    
    // 简化处理：假设是exit系统调用
    state.halted = true;
    print_stage_activity("系统调用导致程序终止", state.cycle_count, state.pc);
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

void CommitStage::print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(get_stage_name(), activity, cycle, pc);
}

} // namespace riscv 