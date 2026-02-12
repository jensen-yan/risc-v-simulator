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
        LOGT(FETCH, "cpu halted, skip fetch");
        return;
    }
    
    // 如果取指缓冲区有空间，取指令
    if (state.fetch_buffer.size() < MAX_FETCH_BUFFER_SIZE) {
        try {
            Instruction raw_inst = state.memory->fetchInstruction(state.pc);
            
            // 如果指令为0，可能表明程序结束，但不要立即停机
            // 要等待流水线中的指令全部完成提交
            if (raw_inst == 0) {
                LOGT(FETCH, "fetched zero instruction, stop fetching and wait for pipeline drain");
                
                // 检查是否还有未完成的指令
                if (state.reorder_buffer->is_empty() && 
                    state.fetch_buffer.empty() && 
                    state.cdb_queue.empty()) {
                    state.halted = true;
                    LOGT(FETCH, "pipeline drained, program finished");
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
                LOGT(FETCH, "fetch: pc=0x%" PRIx64 " inst=0x%" PRIx32 " (compressed, pc+2)",
                    fetched.pc, raw_inst);
            } else {
                fetched.is_compressed = false;
                state.pc += 4;
                LOGT(FETCH, "fetch: pc=0x%" PRIx64 " inst=0x%" PRIx32 " (normal, pc+4)",
                    fetched.pc, raw_inst);
            }
            
            state.fetch_buffer.push(fetched);
            state.perf_counters.increment(PerfCounterId::FETCHED_INSTRUCTIONS);
            
        } catch (const MemoryException& e) {
            // 取指失败，停止取指但等待流水线清空
            LOGW(FETCH, "fetch failed, stop fetching and wait for pipeline drain: %s", e.what());
            
            // 检查是否还有未完成的指令
            if (state.reorder_buffer->is_empty() && 
                state.fetch_buffer.empty() && 
                state.cdb_queue.empty()) {
                state.halted = true;
                LOGT(FETCH, "pipeline drained, program finished");
            }
            return;
        }
    } else {
        LOGT(FETCH, "fetch buffer full(size=%zu), skip fetch", state.fetch_buffer.size());
        state.recordPipelineStall(PerfCounterId::STALL_FETCH_BUFFER_FULL);
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
            LOGT(FETCH, "all instructions completed, halt cpu");
        }
    }
}

void FetchStage::flush() {
    // 刷新取指阶段状态（例如：清空预取缓冲区等）
    // 在简单实现中，取指缓冲区的清空由主控制器处理
    LOGT(FETCH, "fetch stage flushed");
}

void FetchStage::reset() {
    // 重置取指阶段到初始状态
    LOGT(FETCH, "fetch stage reset");
}

} // namespace riscv 
