#include "cpu/ooo/stages/decode_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include "core/decoder.h"
#include <fmt/format.h>

namespace riscv {

DecodeStage::DecodeStage() {
    // 构造函数：初始化译码阶段
}

void DecodeStage::execute(CPUState& state) {
    // 如果取指缓冲区为空，无法译码
    if (state.fetch_buffer.empty()) {
        LOGT(DECODE, "fetch buffer empty, skip decode");
        return;
    }
    
    // 如果ROB已满，无法译码
    if (!state.reorder_buffer->has_free_entry()) {
        LOGT(DECODE, "rob full, decode stalled");
        state.recordPipelineStall(PerfCounterId::STALL_DECODE_ROB_FULL);
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
        LOGT(DECODE, "compressed instruction decoded");
    } else {
        decoded = state.decoder.decode(fetched.instruction, state.enabled_extensions);
        LOGT(DECODE, "normal instruction decoded");
    }
    
    // 分配ROB表项 (使用新的DynamicInst接口)
    DynamicInstPtr dynamic_inst = state.reorder_buffer->allocate_entry(decoded, fetched.pc, instruction_id);
    if (!dynamic_inst) {
        // ROB分配失败，放回取指缓冲区
        state.fetch_buffer.push(fetched);
        LOGT(DECODE, "rob allocation failed, push instruction back to fetch buffer");
        state.recordPipelineStall(PerfCounterId::STALL_DECODE_ROB_FULL);
        return;
    }

    state.perf_counters.increment(PerfCounterId::DECODED_INSTRUCTIONS);
    
    LOGT(DECODE, "allocated rob[%d], pc=0x%" PRIx64 ", inst=%" PRId64,
        dynamic_inst->get_rob_entry(), fetched.pc, instruction_id);
    
    // 继续到发射阶段的处理将在issue_stage中完成
    // 这里我们需要一个中间缓冲区来存储译码后的指令
    // 简化实现：直接在issue_stage中处理
}

void DecodeStage::flush() {
    LOGT(DECODE, "decode stage flushed");
}

void DecodeStage::reset() {
    LOGT(DECODE, "decode stage reset");
}

} // namespace riscv 
