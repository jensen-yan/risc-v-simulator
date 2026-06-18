#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 取指阶段实现
 * 负责从内存中取指令并放入取指缓冲区
 */
class FetchStage : public PipelineStage {
public:
    FetchStage();
    virtual ~FetchStage() = default;

    class Context {
    public:
        explicit Context(CPUState& state) : state_(state) {}

        bool isHalted() const { return state_.halted; }
        void setHalted(bool halted) { state_.halted = halted; }

        uint64_t pc() const { return state_.pc; }
        void setPc(uint64_t pc) { state_.pc = pc; }
        uint64_t cycleCount() const { return state_.cycle_count; }
        uint32_t enabledExtensions() const { return state_.enabled_extensions; }

        bool fetchBufferEmpty() const { return state_.fetch_buffer.empty(); }
        size_t fetchBufferSize() const { return state_.fetch_buffer.size(); }
        void pushFetchedInstruction(const FetchedInstruction& instruction) {
            state_.fetch_buffer.push(instruction);
        }

        bool completionFabricEmpty() const { return state_.completion_fabric.empty(); }
        bool reorderBufferEmpty() const { return state_.reorder_buffer->is_empty(); }
        bool anyExecutionUnitBusy() const;

        bool hasRedirectStall() const { return state_.hasRedirectStall(); }
        bool advanceRedirectStallCycle() { return state_.advanceRedirectStallCycle(); }
        uint64_t remainingRedirectStallCycles() const {
            return state_.remainingRedirectStallCycles();
        }

        bool hasIcacheMissWait() const { return state_.icache.hasMissWait(); }
        bool advanceIcacheMissWaitCycle() { return state_.icache.advanceMissWaitCycle(); }
        int remainingIcacheWaitCycles() const { return state_.icache.remainingWaitCycles(); }
        bool consumePendingIcacheIfMatch(uint64_t pc, Instruction& instruction_out) {
            return state_.icache.consumeIfMatch(pc, instruction_out);
        }
        void startIcacheMissWait(uint64_t pc, Instruction instruction, int latency_cycles) {
            state_.icache.startMissWait(pc, instruction, latency_cycles);
        }
        void resetIcache() { state_.icache.reset(); }

        Address translateInstructionAddress(Address virtual_address, size_t size) const;
        std::shared_ptr<Memory> memory() const { return state_.memory; }
        NonBlockingCache* l1iCache() const { return state_.l1i_cache.get(); }
        Decoder& decoder() { return state_.decoder; }
        BranchPredictor* branchPredictor() const { return state_.branch_predictor.get(); }

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
    const char* get_stage_name() const override { return "FETCH"; }

private:
    static constexpr size_t MAX_FETCH_BUFFER_SIZE = OOOPipelineConfig::FETCH_BUFFER_SIZE;
};

} // namespace riscv 
