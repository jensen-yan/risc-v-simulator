#include "cpu/ooo/stages/fetch_stage.h"
#include "common/debug_types.h"
#include <fmt/format.h>

namespace riscv {

FetchStage::FetchStage() {
    // 构造函数：初始化取指阶段
}

void FetchStage::execute(CPUState& state) {
    // 如果已经停机，不再取指
    if (state.halted) {
        dprintf(FETCH, "CPU已停机，跳过取指");
        return;
    }
    
    // 如果取指缓冲区有空间，取指令
    if (state.fetch_buffer.size() < MAX_FETCH_BUFFER_SIZE) {
        try {
            Instruction raw_inst = state.memory->fetchInstruction(state.pc);
            
            // 如果指令为0，可能表明程序结束，但不要立即停机
            // 要等待流水线中的指令全部完成提交
            if (raw_inst == 0) {
                dprintf(FETCH, "取指到空指令(0x0)，停止取指但等待流水线清空");
                
                // 检查是否还有未完成的指令
                if (state.reorder_buffer->is_empty() && 
                    state.fetch_buffer.empty() && 
                    state.cdb_queue.empty()) {
                    state.halted = true;
                    dprintf(FETCH, "流水线已清空，程序结束");
                }
                return;
            }
            
            FetchedInstruction fetched;
            fetched.pc = state.pc;
            fetched.instruction = raw_inst;
            
            // 检查是否为压缩指令
            if ((raw_inst & 0x03) != 0x03) {
                fetched.is_compressed = true;
                state.pc += 2;
                dprintf(FETCH, "取指令: pc = 0x%x data = 0x%x (压缩指令，PC+2)", 
                    fetched.pc, raw_inst);
            } else {
                fetched.is_compressed = false;
                state.pc += 4;
                dprintf(FETCH, "取指令: pc = 0x%x data = 0x%x (正常指令，PC+4)", 
                    fetched.pc, raw_inst);
            }
            
            state.fetch_buffer.push(fetched);
            
        } catch (const MemoryException& e) {
            // 取指失败，停止取指但等待流水线清空
            dprintf(FETCH, "取指失败，停止取指但等待流水线清空: %s", e.what());
            
            // 检查是否还有未完成的指令
            if (state.reorder_buffer->is_empty() && 
                state.fetch_buffer.empty() && 
                state.cdb_queue.empty()) {
                state.halted = true;
                dprintf(FETCH, "流水线已清空，程序结束");
            }
            return;
        }
    } else {
        dprintf(FETCH, "取指缓冲区已满(大小=%zu)，跳过取指", state.fetch_buffer.size());
    }
    
    // 每个周期结束时检查是否应该停机
    // 如果没有更多指令可取，且流水线为空，则停机
    if (state.pc >= state.memory->getSize() || 
        (state.reorder_buffer->is_empty() && 
         state.fetch_buffer.empty() && 
         state.cdb_queue.empty())) {
        
        // 检查是否还有任何正在执行的指令
        bool has_busy_units = false;
        for (const auto& unit : state.alu_units) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : state.branch_units) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : state.load_units) {
            if (unit.busy) has_busy_units = true;
        }
        for (const auto& unit : state.store_units) {
            if (unit.busy) has_busy_units = true;
        }
        
        if (!has_busy_units && state.reorder_buffer->is_empty()) {
            state.halted = true;
            dprintf(FETCH, "所有指令完成，CPU停机");
        }
    }
}

void FetchStage::flush() {
    // 刷新取指阶段状态（例如：清空预取缓冲区等）
    // 在简单实现中，取指缓冲区的清空由主控制器处理
    dprintf(FETCH, "取指阶段已刷新");
}

void FetchStage::reset() {
    // 重置取指阶段到初始状态
    dprintf(FETCH, "取指阶段已重置");
}

} // namespace riscv 