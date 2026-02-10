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
        
        // 检查指令是否有效
        if (!cdb_entry.instruction) {
            LOGW(WRITEBACK, "invalid cdb entry, skip");
            continue;
        }
        
        auto instruction = cdb_entry.instruction;
        auto rob_entry = instruction->get_rob_entry();
        auto phys_dest = instruction->get_physical_dest();
        auto result = instruction->get_result();
        
        LOGT(WRITEBACK, "cdb writeback: rob[%d] p%d = 0x%" PRIx64,
                rob_entry, static_cast<int>(phys_dest), result);
        
        // 更新保留站中的操作数
        state.reservation_station->update_operands(cdb_entry);
        
        // 更新寄存器重命名映射
        state.register_rename->update_physical_register(phys_dest, result, rob_entry);
        
        // 更新ROB表项（使用DynamicInst指针直接验证）
        auto rob_instruction = state.reorder_buffer->get_entry(rob_entry);
        if (rob_instruction && rob_instruction == instruction) {
            // 指令指针匹配，安全更新
            state.reorder_buffer->update_entry(instruction,
                                               result,
                                               instruction->has_exception(),
                                               instruction->get_exception_message(),
                                               instruction->is_jump(),
                                               instruction->get_jump_target());
        } else {
            LOGT(WRITEBACK, "stale cdb entry, skip update: rob[%d] current=%p cdb=%p",
                   rob_entry,
                   static_cast<const void*>(rob_instruction.get()),
                   static_cast<const void*>(instruction.get()));
        }
        
        LOGT(WRITEBACK, "rob[%d] status updated to COMPLETED", rob_entry);
    }
    
    if (state.cdb_queue.empty()) {
        LOGT(WRITEBACK, "cdb queue empty, no writeback");
    }
}

void WritebackStage::flush() {
    LOGT(WRITEBACK, "writeback stage flushed");
}

void WritebackStage::reset() {
    LOGT(WRITEBACK, "writeback stage reset");
}

} // namespace riscv 
