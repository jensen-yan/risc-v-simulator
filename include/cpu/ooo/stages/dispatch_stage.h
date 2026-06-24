#pragma once

#include "cpu/ooo/dispatch_admission.h"
#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 派发阶段实现
 * 负责从 ROB 中获取待派发指令，进行寄存器重命名，并派发到保留站。
 */
class DispatchStage : public PipelineStage {
public:
    DispatchStage();
    virtual ~DispatchStage() = default;

    class Context {
    public:
        explicit Context(CPUState& state) : state_(state) {}

        void incrementCounter(PerfCounterId id, uint64_t amount = 1) {
            state_.perf_counters.increment(id, amount);
        }
        void recordPipelineStall(PerfCounterId reason) {
            state_.recordPipelineStall(reason);
        }

        bool reorderBufferEmpty() const { return state_.reorder_buffer->is_empty(); }
        DynamicInstPtr dispatchableRobEntry() const {
            return state_.reorder_buffer->get_dispatchable_entry();
        }
        ROBEntry robHeadEntry() const { return state_.reorder_buffer->get_head_entry(); }
        bool hasOlderInflightSerializingInstruction(uint64_t instruction_id) const;

        DispatchAdmission::Result admitInstruction(
            const DynamicInstPtr& instruction,
            bool save_rename_checkpoint) {
            DispatchAdmission admission(*state_.register_rename,
                                        *state_.reservation_station,
                                        *state_.store_buffer,
                                        state_.rename_checkpoints);
            return admission.tryAdmit(instruction,
                                      state_.cycle_count,
                                      save_rename_checkpoint);
        }

    private:
        CPUState& state_;
    };

    void execute(Context& context);
    const char* get_stage_name() const override { return "DISPATCH"; }

private:
};

} // namespace riscv
