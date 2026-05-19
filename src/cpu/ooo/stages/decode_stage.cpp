#include "cpu/ooo/stages/decode_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include "core/decoder.h"
#include <fmt/format.h>

namespace riscv {

DecodeStage::DecodeStage() {
    // 构造函数：初始化译码阶段
}

FetchedInstruction DecodeStage::Context::popFetchedInstruction() {
    FetchedInstruction fetched = state_.fetch_buffer.front();
    state_.fetch_buffer.pop();
    return fetched;
}

void DecodeStage::execute(Context& context) {
    context.incrementCounter(PerfCounterId::DECODE_SLOTS, OOOPipelineConfig::DECODE_WIDTH);

    if (context.fetchBufferEmpty()) {
        LOGT(DECODE, "fetch buffer empty, skip decode");
        return;
    }

    size_t decoded_this_cycle = 0;

    for (size_t slot = 0; slot < OOOPipelineConfig::DECODE_WIDTH; ++slot) {
        if (context.fetchBufferEmpty()) {
            break;
        }
        if (!context.hasFreeRobEntry()) {
            if (decoded_this_cycle == 0) {
                LOGT(DECODE, "rob full, decode stalled");
                context.recordPipelineStall(PerfCounterId::STALL_DECODE_ROB_FULL);
            }
            break;
        }

        FetchedInstruction fetched = context.popFetchedInstruction();

        uint64_t instruction_id = context.nextInstructionId();

        DecodedInstruction decoded;
        if (fetched.is_compressed) {
            decoded = context.decodeCompressedInstruction(static_cast<uint16_t>(fetched.instruction));
            LOGT(DECODE, "slot=%zu compressed instruction decoded", slot);
        } else {
            decoded = context.decodeInstruction(fetched.instruction);
            LOGT(DECODE, "slot=%zu normal instruction decoded", slot);
        }

        DynamicInstPtr dynamic_inst = context.allocateRobEntry(decoded, fetched.pc, instruction_id);
        if (!dynamic_inst) {
            context.recordPipelineStall(PerfCounterId::STALL_DECODE_ROB_FULL);
            break;
        }

        context.incrementCounter(PerfCounterId::DECODED_INSTRUCTIONS);
        decoded_this_cycle++;

        dynamic_inst->set_fetch_cycle(fetched.fetch_cycle);
        dynamic_inst->set_decode_cycle(context.cycleCount());
        dynamic_inst->set_predicted_next_pc(fetched.predicted_next_pc);
        if (fetched.has_branch_meta) {
            dynamic_inst->set_branch_predict_meta(fetched.branch_meta);
        }
        if (fetched.has_ras_checkpoint) {
            dynamic_inst->set_ras_checkpoint(fetched.ras_checkpoint);
        }

        LOGT(DECODE, "slot=%zu allocated rob[%d], pc=0x%" PRIx64 ", inst=%" PRId64,
             slot, dynamic_inst->get_rob_entry(), fetched.pc, instruction_id);
    }

    context.incrementCounter(PerfCounterId::DECODE_UTILIZED_SLOTS, decoded_this_cycle);
}

} // namespace riscv 
