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
    state.perf_counters.increment(PerfCounterId::DECODE_SLOTS, OOOPipelineConfig::DECODE_WIDTH);

    if (state.fetch_buffer.empty()) {
        LOGT(DECODE, "fetch buffer empty, skip decode");
        return;
    }

    size_t decoded_this_cycle = 0;

    for (size_t slot = 0; slot < OOOPipelineConfig::DECODE_WIDTH; ++slot) {
        if (state.fetch_buffer.empty()) {
            break;
        }
        if (!state.reorder_buffer->has_free_entry()) {
            if (decoded_this_cycle == 0) {
                LOGT(DECODE, "rob full, decode stalled");
                state.recordPipelineStall(PerfCounterId::STALL_DECODE_ROB_FULL);
            }
            break;
        }

        FetchedInstruction fetched = state.fetch_buffer.front();
        state.fetch_buffer.pop();

        uint64_t instruction_id = ++state.global_instruction_id;

        DecodedInstruction decoded;
        if (fetched.is_compressed) {
            decoded = state.decoder.decodeCompressed(static_cast<uint16_t>(fetched.instruction), state.enabled_extensions);
            LOGT(DECODE, "slot=%zu compressed instruction decoded", slot);
        } else {
            decoded = state.decoder.decode(fetched.instruction, state.enabled_extensions);
            LOGT(DECODE, "slot=%zu normal instruction decoded", slot);
        }

        DynamicInstPtr dynamic_inst = state.reorder_buffer->allocate_entry(decoded, fetched.pc, instruction_id);
        if (!dynamic_inst) {
            state.recordPipelineStall(PerfCounterId::STALL_DECODE_ROB_FULL);
            break;
        }

        state.perf_counters.increment(PerfCounterId::DECODED_INSTRUCTIONS);
        decoded_this_cycle++;

        dynamic_inst->set_fetch_cycle(fetched.fetch_cycle);
        dynamic_inst->set_decode_cycle(state.cycle_count);
        dynamic_inst->set_predicted_next_pc(fetched.predicted_next_pc);
        if (fetched.has_branch_meta) {
            dynamic_inst->set_branch_predict_meta(fetched.branch_meta);
        }

        LOGT(DECODE, "slot=%zu allocated rob[%d], pc=0x%" PRIx64 ", inst=%" PRId64,
             slot, dynamic_inst->get_rob_entry(), fetched.pc, instruction_id);
    }

    state.perf_counters.increment(PerfCounterId::DECODE_UTILIZED_SLOTS, decoded_this_cycle);
}

void DecodeStage::flush() {
    LOGT(DECODE, "decode stage flushed");
}

void DecodeStage::reset() {
    LOGT(DECODE, "decode stage reset");
}

} // namespace riscv 
