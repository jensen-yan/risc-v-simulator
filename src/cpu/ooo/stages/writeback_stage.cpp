#include "cpu/ooo/stages/writeback_stage.h"
#include "common/debug_types.h"

namespace riscv {

WritebackStage::WritebackStage() {
    // 构造函数：初始化写回阶段
}

void WritebackStage::execute(Context& context) {
    size_t used_writeback_ports = 0;
    while (!context.cdbQueueEmpty() &&
           used_writeback_ports < OOOPipelineConfig::WRITEBACK_WIDTH) {
        CommonDataBusEntry cdb_entry = context.popCdbEntry();
        ++used_writeback_ports;
        
        // 检查指令是否有效
        if (!cdb_entry.instruction) {
            LOGW(WRITEBACK, "invalid cdb entry, skip");
            continue;
        }
        
        auto instruction = cdb_entry.instruction;
        auto rob_entry = instruction->get_rob_entry();
        auto phys_dest = instruction->get_physical_dest();
        auto dest_kind = instruction->get_physical_dest_kind();
        auto result = instruction->get_result();
        
        LOGT(WRITEBACK, "cdb writeback: rob[%d] p%d = 0x%" PRIx64,
                rob_entry, static_cast<int>(phys_dest), result);

        context.incrementCounter(PerfCounterId::WRITEBACKS);
        
        // 更新保留站中的操作数
        context.updateWaitingOperands(cdb_entry);
        
        // 更新寄存器重命名映射
        context.updatePhysicalRegister(dest_kind, phys_dest, result, rob_entry);
        
        // 更新ROB表项（使用DynamicInst指针直接验证）
        auto rob_instruction = context.robEntry(rob_entry);
        if (rob_instruction && rob_instruction == instruction) {
            // 指令指针匹配，安全更新
            context.completeRobEntry(instruction,
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
    
    if (context.cdbQueueEmpty()) {
        LOGT(WRITEBACK, "cdb queue empty, no writeback");
    } else {
        LOGT(WRITEBACK, "cdb queue keeps %zu entries after using %zu/%zu writeback ports",
             context.cdbQueueSize(),
             used_writeback_ports,
             OOOPipelineConfig::WRITEBACK_WIDTH);
    }
}

} // namespace riscv 
