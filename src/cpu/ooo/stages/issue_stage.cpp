#include "cpu/ooo/stages/issue_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"

namespace riscv {

IssueStage::IssueStage() {
    // 构造函数：初始化发射阶段
}

void IssueStage::execute(CPUState& state) {
    // 检查ROB中是否有待发射的指令
    if (state.reorder_buffer->is_empty()) {
        dprintf(ISSUE, "ROB为空，跳过发射");
        return;
    }
    
    // 遍历ROB，找到状态为ISSUED但还没有发射到保留站的指令
    // 这里简化实现：处理ROB中第一条状态为ISSUED的指令
    
    // 获取可以发射的指令
    auto dispatchable_entry = state.reorder_buffer->get_dispatchable_entry();
    if (!dispatchable_entry) {  // 没有可发射的指令
        dprintf(ISSUE, "没有可发射的指令");
        return;
    }
    
    if (!dispatchable_entry->is_allocated()) {
        dprintf(ISSUE, "ROB表项状态不正确");
        return;
    }
    
    dprintf(ISSUE, "尝试发射 Inst#%" PRId64 " (ROB[%d])", 
                        dispatchable_entry->get_instruction_id(), dispatchable_entry->get_rob_entry());
    
    // 检查保留站是否有空闲表项
    if (!state.reservation_station->has_free_entry()) {
        dprintf(ISSUE, "保留站已满，发射停顿");
        state.pipeline_stalls++;
        return;
    }
    
    // 进行寄存器重命名
    auto rename_result = state.register_rename->rename_instruction(dispatchable_entry->get_decoded_info());
    if (!rename_result.success) {
        dprintf(ISSUE, "寄存器重命名失败，发射停顿");
        state.pipeline_stalls++;
        return;
    }
    
    // 更新指令的重命名信息
    dispatchable_entry->set_physical_src1(rename_result.src1_reg);
    dispatchable_entry->set_physical_src2(rename_result.src2_reg);
    dispatchable_entry->set_physical_dest(rename_result.dest_reg);
    
    // 设置操作数就绪状态和值
    dispatchable_entry->set_src1_ready(rename_result.src1_ready, rename_result.src1_value);
    dispatchable_entry->set_src2_ready(rename_result.src2_ready, rename_result.src2_value);
    
    // 发射到保留站
    auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
    if (!issue_result.success) {
        dprintf(ISSUE, "保留站发射失败，回退重命名");
        state.register_rename->release_physical_register(rename_result.dest_reg);
        state.pipeline_stalls++;
        return;
    }
    
    dprintf(ISSUE, "Inst#%" PRId64 " 成功发射到保留站RS[%d]", 
                        dispatchable_entry->get_instruction_id(), issue_result.rs_entry);
    
    // 更新指令状态，标记为已发射到保留站
    dispatchable_entry->set_status(DynamicInst::Status::ISSUED);
}

void IssueStage::flush() {
    dprintf(ISSUE, "发射阶段已刷新");
}

void IssueStage::reset() {
    dprintf(ISSUE, "发射阶段已重置");
}

} // namespace riscv 