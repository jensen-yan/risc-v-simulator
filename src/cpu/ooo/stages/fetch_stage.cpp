#include "cpu/ooo/stages/fetch_stage.h"
#include "common/debug_types.h"
#include <iostream>
#include <sstream>

namespace riscv {

FetchStage::FetchStage() {
    // 构造函数：初始化取指阶段
}

void FetchStage::execute(CPUState& state) {
    print_stage_activity("开始取指阶段", state.cycle_count, state.pc);
    
    // 如果已经停机，不再取指
    if (state.halted) {
        print_stage_activity("CPU已停机，跳过取指", state.cycle_count, state.pc);
        return;
    }
    
    // 如果取指缓冲区有空间，取指令
    if (state.fetch_buffer.size() < MAX_FETCH_BUFFER_SIZE) {
        try {
            Instruction raw_inst = state.memory->fetchInstruction(state.pc);
            
            // 如果指令为0，可能表明程序结束，但不要立即停机
            // 要等待流水线中的指令全部完成提交
            if (raw_inst == 0) {
                print_stage_activity("取指到空指令(0x0)，停止取指但等待流水线清空", 
                                    state.cycle_count, state.pc);
                
                // 检查是否还有未完成的指令
                if (state.reorder_buffer->is_empty() && 
                    state.fetch_buffer.empty() && 
                    state.cdb_queue.empty()) {
                    state.halted = true;
                    print_stage_activity("流水线已清空，程序结束", state.cycle_count, state.pc);
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
                std::stringstream ss;
                ss << "取指令: pc = 0x" << std::hex << fetched.pc << " data = 0x" << std::hex << raw_inst << " (压缩指令，PC+2)";
                print_stage_activity(ss.str(), state.cycle_count, fetched.pc);
            } else {
                fetched.is_compressed = false;
                state.pc += 4;
                std::stringstream ss;
                ss << "取指令: pc = 0x" << std::hex << fetched.pc << " data = 0x" << std::hex << raw_inst << " (正常指令，PC+4)";
                print_stage_activity(ss.str(), state.cycle_count, fetched.pc);
            }
            
            state.fetch_buffer.push(fetched);
            
        } catch (const MemoryException& e) {
            // 取指失败，停止取指但等待流水线清空
            print_stage_activity("取指失败，停止取指但等待流水线清空: " + std::string(e.what()), 
                                state.cycle_count, state.pc);
            
            // 检查是否还有未完成的指令
            if (state.reorder_buffer->is_empty() && 
                state.fetch_buffer.empty() && 
                state.cdb_queue.empty()) {
                state.halted = true;
                print_stage_activity("流水线已清空，程序结束", state.cycle_count, state.pc);
            }
            return;
        }
    } else {
        print_stage_activity("取指缓冲区已满(大小=" + std::to_string(state.fetch_buffer.size()) + 
                           ")，跳过取指", state.cycle_count, state.pc);
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
            print_stage_activity("所有指令完成，CPU停机", state.cycle_count, state.pc);
        }
    }
}

void FetchStage::flush() {
    // 刷新取指阶段状态（例如：清空预取缓冲区等）
    // 在简单实现中，取指缓冲区的清空由主控制器处理
    print_stage_activity("取指阶段已刷新", 0, 0);
}

void FetchStage::reset() {
    // 重置取指阶段到初始状态
    print_stage_activity("取指阶段已重置", 0, 0);
}

void FetchStage::print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(get_stage_name(), activity, cycle, pc);
}

} // namespace riscv 