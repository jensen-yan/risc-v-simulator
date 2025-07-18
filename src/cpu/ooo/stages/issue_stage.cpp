#include "cpu/ooo/stages/issue_stage.h"
#include "common/debug_types.h"

namespace riscv {

IssueStage::IssueStage() {
    // 构造函数：初始化发射阶段
}

void IssueStage::execute(CPUState& state) {
    print_stage_activity("开始发射阶段", state.cycle_count, state.pc);
    
    // 检查ROB中是否有待发射的指令
    if (state.reorder_buffer->is_empty()) {
        print_stage_activity("ROB为空，跳过发射", state.cycle_count, state.pc);
        return;
    }
    
    // 遍历ROB，找到状态为ISSUED但还没有发射到保留站的指令
    // 这里简化实现：处理ROB中第一条状态为ISSUED的指令
    
    // 获取可以发射的指令
    auto dispatchable_entry = state.reorder_buffer->get_dispatchable_entry();
    if (dispatchable_entry == ReorderBuffer::MAX_ROB_ENTRIES) {  // 没有可发射的指令
        print_stage_activity("没有可发射的指令", state.cycle_count, state.pc);
        return;
    }
    
    const auto& rob_entry = state.reorder_buffer->get_entry(dispatchable_entry);
    if (!rob_entry.valid || rob_entry.state != ReorderBufferEntry::State::ALLOCATED) {
        print_stage_activity("ROB表项状态不正确", state.cycle_count, state.pc);
        return;
    }
    
    print_stage_activity("尝试发射 Inst#" + std::to_string(rob_entry.instruction_id) + 
                        " (ROB[" + std::to_string(dispatchable_entry) + "])", 
                        state.cycle_count, state.pc);
    
    // 检查保留站是否有空闲表项
    if (!state.reservation_station->has_free_entry()) {
        print_stage_activity("保留站已满，发射停顿", state.cycle_count, state.pc);
        state.pipeline_stalls++;
        return;
    }
    
    // 进行寄存器重命名
    auto rename_result = state.register_rename->rename_instruction(rob_entry.instruction);
    if (!rename_result.success) {
        print_stage_activity("寄存器重命名失败，发射停顿", state.cycle_count, state.pc);
        state.pipeline_stalls++;
        return;
    }
    
    // 准备保留站表项
    ReservationStationEntry rs_entry;
    rs_entry.instruction = rob_entry.instruction;
    rs_entry.instruction_id = rob_entry.instruction_id;
    rs_entry.src1_reg = rename_result.src1_reg;
    rs_entry.src2_reg = rename_result.src2_reg;
    rs_entry.dest_reg = rename_result.dest_reg;
    rs_entry.pc = rob_entry.pc;
    rs_entry.rob_entry = dispatchable_entry;
    rs_entry.valid = true;
    
    // 检查操作数是否准备好
    rs_entry.src1_ready = rename_result.src1_ready;
    rs_entry.src2_ready = rename_result.src2_ready;
    
    // 获取操作数值
    rs_entry.src1_value = rename_result.src1_value;
    rs_entry.src2_value = rename_result.src2_value;
    
    // 将重命名结果更新到ROB表项
    state.reorder_buffer->set_physical_register(dispatchable_entry, rename_result.dest_reg);
    
    // 发射到保留站
    auto issue_result = state.reservation_station->issue_instruction(rs_entry);
    if (!issue_result.success) {
        print_stage_activity("保留站发射失败，回退重命名", state.cycle_count, state.pc);
        state.register_rename->release_physical_register(rename_result.dest_reg);
        state.pipeline_stalls++;
        return;
    }
    
    print_stage_activity("Inst#" + std::to_string(rob_entry.instruction_id) + 
                        " 成功发射到保留站RS[" + std::to_string(issue_result.rs_entry) + "]", 
                        state.cycle_count, state.pc);
    
    // 更新ROB表项状态，标记为已发射到保留站
    state.reorder_buffer->mark_as_dispatched(dispatchable_entry);
}

void IssueStage::flush() {
    print_stage_activity("发射阶段已刷新", 0, 0);
}

void IssueStage::reset() {
    print_stage_activity("发射阶段已重置", 0, 0);
}

void IssueStage::print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(get_stage_name(), activity, cycle, pc);
}

} // namespace riscv 