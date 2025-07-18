#include "cpu/ooo/stages/decode_stage.h"
#include "common/debug_types.h"

namespace riscv {

DecodeStage::DecodeStage() {
    // 构造函数：初始化译码阶段
}

void DecodeStage::execute(CPUState& state) {
    print_stage_activity("开始译码阶段", state.cycle_count, state.pc);
    
    // 如果取指缓冲区为空，无法译码
    if (state.fetch_buffer.empty()) {
        print_stage_activity("取指缓冲区为空，跳过译码", state.cycle_count, state.pc);
        return;
    }
    
    // 如果ROB已满，无法译码
    if (!state.reorder_buffer->has_free_entry()) {
        print_stage_activity("ROB已满，译码停顿", state.cycle_count, state.pc);
        state.pipeline_stalls++;
        return;
    }
    
    // 取出一条指令进行译码
    FetchedInstruction fetched = state.fetch_buffer.front();
    state.fetch_buffer.pop();
    
    // 分配全局指令序号
    uint64_t instruction_id = ++state.global_instruction_id;
    
    // 解码指令
    DecodedInstruction decoded;
    if (fetched.is_compressed) {
        decoded = state.decoder.decodeCompressed(static_cast<uint16_t>(fetched.instruction), state.enabled_extensions);
        print_stage_activity("压缩指令译码完成", state.cycle_count, state.pc);
    } else {
        decoded = state.decoder.decode(fetched.instruction, state.enabled_extensions);
        print_stage_activity("普通指令译码完成", state.cycle_count, state.pc);
    }
    
    // 分配ROB表项
    auto rob_result = state.reorder_buffer->allocate_entry(decoded, fetched.pc);
    if (!rob_result.success) {
        // ROB分配失败，放回取指缓冲区
        state.fetch_buffer.push(fetched);
        print_stage_activity("ROB分配失败，指令放回缓冲区", state.cycle_count, state.pc);
        state.pipeline_stalls++;
        return;
    }
    
    // 设置指令序号
    state.reorder_buffer->set_instruction_id(rob_result.rob_entry, instruction_id);
    
    std::string msg = std::format("分配到ROB[{}] PC=0x{:x} 指令ID={}", 
        rob_result.rob_entry, fetched.pc, instruction_id);
    print_stage_activity(msg, state.cycle_count, state.pc);
    
    // 继续到发射阶段的处理将在issue_stage中完成
    // 这里我们需要一个中间缓冲区来存储译码后的指令
    // 简化实现：直接在issue_stage中处理
}

void DecodeStage::flush() {
    print_stage_activity("译码阶段已刷新", 0, 0);
}

void DecodeStage::reset() {
    print_stage_activity("译码阶段已重置", 0, 0);
}

void DecodeStage::print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(get_stage_name(), activity, cycle, pc);
}

} // namespace riscv 