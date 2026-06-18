#include "cpu/ooo/stages/writeback_stage.h"
#include "common/debug_types.h"

namespace riscv {

WritebackStage::WritebackStage() {
    // 构造函数：初始化写回阶段
}

void WritebackStage::execute(Context& context) {
    size_t used_writeback_ports = 0;
    while (!context.completionFabricEmpty() &&
           used_writeback_ports < OOOPipelineConfig::WRITEBACK_WIDTH) {
        CompletionEvent completion_event = context.popCompletionEvent();
        ++used_writeback_ports;
        
        // 检查指令是否有效
        if (!completion_event.instruction) {
            LOGW(WRITEBACK, "invalid completion event, skip");
            continue;
        }
        
        auto instruction = completion_event.instruction;
        auto rob_entry = instruction->get_rob_entry();
        auto phys_dest = instruction->get_physical_dest();
        auto dest_kind = instruction->get_physical_dest_kind();
        auto result = instruction->get_result();
        
        LOGT(WRITEBACK, "completion writeback: rob[%d] p%d = 0x%" PRIx64,
                rob_entry, static_cast<int>(phys_dest), result);

        context.incrementCounter(PerfCounterId::WRITEBACKS);
        
        // 更新保留站中的操作数
        context.updateWaitingOperands(completion_event);
        
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
            LOGT(WRITEBACK, "stale completion event, skip update: rob[%d] current=%p event=%p",
                   rob_entry,
                   static_cast<const void*>(rob_instruction.get()),
                   static_cast<const void*>(instruction.get()));
        }
        
        LOGT(WRITEBACK, "rob[%d] status updated to COMPLETED", rob_entry);
    }
    
    if (context.completionFabricEmpty()) {
        LOGT(WRITEBACK, "completion fabric empty, no writeback");
    } else {
        LOGT(WRITEBACK, "completion fabric keeps %zu events after using %zu/%zu writeback ports",
             context.completionFabricSize(),
             used_writeback_ports,
             OOOPipelineConfig::WRITEBACK_WIDTH);
    }
}

} // namespace riscv 
