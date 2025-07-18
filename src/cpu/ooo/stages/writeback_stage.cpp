#include "cpu/ooo/stages/writeback_stage.h"
#include "common/debug_types.h"
#include <format>

namespace riscv {

WritebackStage::WritebackStage() {
    // 构造函数：初始化写回阶段
}

void WritebackStage::execute(CPUState& state) {
    print_stage_activity("开始写回阶段", state.cycle_count, state.pc);
    
    // 处理CDB队列中的写回请求
    while (!state.cdb_queue.empty()) {
        CommonDataBusEntry cdb_entry = state.cdb_queue.front();
        state.cdb_queue.pop();
        
        std::string msg = std::format("CDB writeback: ROB[{}] p{} = 0x{:x}", 
                                       cdb_entry.rob_entry, 
                                       static_cast<int>(cdb_entry.dest_reg), 
                                       cdb_entry.value);
        print_stage_activity(msg, state.cycle_count, state.pc);
        
        // 更新保留站中的操作数
        state.reservation_station->update_operands(cdb_entry);
        
        // 更新寄存器重命名映射
        state.register_rename->update_physical_register(cdb_entry.dest_reg, cdb_entry.value, cdb_entry.rob_entry);
        
        // 更新ROB表项
        state.reorder_buffer->update_entry(cdb_entry.rob_entry, cdb_entry.value, false, "",
                                     cdb_entry.is_jump, cdb_entry.jump_target);
        
        print_stage_activity(std::format("ROB[{}] status updated to COMPLETED", cdb_entry.rob_entry), 
                            state.cycle_count, state.pc);
    }
    
    if (state.cdb_queue.empty()) {
        print_stage_activity("CDB队列为空，无写回操作", state.cycle_count, state.pc);
    }
}

void WritebackStage::flush() {
    print_stage_activity("写回阶段已刷新", 0, 0);
}

void WritebackStage::reset() {
    print_stage_activity("写回阶段已重置", 0, 0);
}

void WritebackStage::print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(get_stage_name(), activity, cycle, pc);
}

} // namespace riscv 