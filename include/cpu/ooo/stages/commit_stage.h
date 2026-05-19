#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 提交阶段实现
 * 负责按程序顺序提交完成的指令，维护精确异常语义
 */
class CommitStage : public PipelineStage {
public:
    CommitStage();
    virtual ~CommitStage() = default;

    class Context {
    public:
        explicit Context(CPUState& state) : state_(state) {}

        size_t effectiveCommitWidth() const;
        void incrementCounter(PerfCounterId id, uint64_t amount = 1) {
            state_.perf_counters.increment(id, amount);
        }

        size_t reorderBufferFreeEntryCount() const {
            return state_.reorder_buffer->get_free_entry_count();
        }
        size_t reorderBufferUsedEntryCount() const;
        bool reorderBufferEmpty() const { return state_.reorder_buffer->is_empty(); }
        bool reorderBufferFull() const { return state_.reorder_buffer->is_full(); }
        ROBEntry robHeadEntry() const { return state_.reorder_buffer->get_head_entry(); }
        DynamicInstPtr robEntry(ROBEntry entry) const { return state_.reorder_buffer->get_entry(entry); }
        bool canCommit() const { return state_.reorder_buffer->can_commit(); }
        ReorderBuffer::CommitResult commitInstruction() {
            return state_.reorder_buffer->commit_instruction();
        }
        uint64_t cycleCount() const { return state_.cycle_count; }

        // CommitStage still coordinates ordered retire effects and final
        // DiffTest/tracer emission. Keep the wide access explicit until the
        // remaining commit orchestration has narrower adapters.
        CPUState& stateForLegacyCommitInternals() { return state_; }

    private:
        CPUState& state_;
    };

    void execute(Context& context);
    const char* get_stage_name() const override { return "COMMIT"; }

private:
    // 异常处理
    void handle_exception(CPUState& state, const std::string& exception_msg, uint64_t pc);
};

} // namespace riscv 
