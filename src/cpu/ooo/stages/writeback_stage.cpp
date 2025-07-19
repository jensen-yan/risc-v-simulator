#include "cpu/ooo/stages/writeback_stage.h"
#include "common/debug_types.h"
#include <fmt/format.h>

namespace riscv {

WritebackStage::WritebackStage() {
    // 构造函数：初始化写回阶段
}

void WritebackStage::execute(CPUState& state) {
    // 处理CDB队列中的写回请求
    while (!state.cdb_queue.empty()) {
        CommonDataBusEntry cdb_entry = state.cdb_queue.front();
        state.cdb_queue.pop();
        
        dprintf(WRITEBACK, "CDB writeback: ROB[%d] p%d = 0x%x",
                                        cdb_entry.rob_entry, 
                                        static_cast<int>(cdb_entry.dest_reg), 
                                        cdb_entry.value);
        
        // 更新保留站中的操作数
        state.reservation_station->update_operands(cdb_entry);
        
        // 更新寄存器重命名映射
        state.register_rename->update_physical_register(cdb_entry.dest_reg, cdb_entry.value, cdb_entry.rob_entry);
        
        // 更新ROB表项
        state.reorder_buffer->update_entry(cdb_entry.rob_entry, cdb_entry.value, false, "",
                                     cdb_entry.is_jump, cdb_entry.jump_target);
        
        dprintf(WRITEBACK, "ROB[%d] status updated to COMPLETED", cdb_entry.rob_entry);
    }
    
    if (state.cdb_queue.empty()) {
        dprintf(WRITEBACK, "CDB队列为空，无写回操作");
    }
}

void WritebackStage::flush() {
    dprintf(WRITEBACK, "写回阶段已刷新");
}

void WritebackStage::reset() {
    dprintf(WRITEBACK, "写回阶段已重置");
}

} // namespace riscv 