#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 译码阶段实现
 * 负责从取指缓冲区中取指令，进行译码并分配ROB表项
 */
class DecodeStage : public PipelineStage {
public:
    DecodeStage();
    virtual ~DecodeStage() = default;

    class Context {
    public:
        explicit Context(CPUState& state) : state_(state) {}

        bool fetchBufferEmpty() const { return state_.fetch_buffer.empty(); }
        FetchedInstruction popFetchedInstruction();

        bool hasFreeRobEntry() const { return state_.reorder_buffer->has_free_entry(); }
        DynamicInstPtr allocateRobEntry(const DecodedInstruction& decoded,
                                        uint64_t pc,
                                        uint64_t instruction_id) {
            return state_.reorder_buffer->allocate_entry(decoded, pc, instruction_id);
        }

        uint64_t nextInstructionId() { return ++state_.global_instruction_id; }
        uint64_t cycleCount() const { return state_.cycle_count; }

        DecodedInstruction decodeInstruction(Instruction instruction) {
            return state_.decoder.decode(instruction, state_.enabled_extensions);
        }
        DecodedInstruction decodeCompressedInstruction(uint16_t instruction) {
            return state_.decoder.decodeCompressed(instruction, state_.enabled_extensions);
        }

        void incrementCounter(PerfCounterId id, uint64_t amount = 1) {
            state_.perf_counters.increment(id, amount);
        }
        void recordPipelineStall(PerfCounterId reason) {
            state_.recordPipelineStall(reason);
        }

    private:
        CPUState& state_;
    };

    void execute(Context& context);
    const char* get_stage_name() const override { return "DECODE"; }
};

} // namespace riscv
