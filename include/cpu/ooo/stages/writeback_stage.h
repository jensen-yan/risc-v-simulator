#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 写回阶段实现
 * 负责处理CDB队列中的写回请求，更新保留站、寄存器重命名和ROB状态
 */
class WritebackStage : public PipelineStage {
public:
    WritebackStage();
    virtual ~WritebackStage() = default;

    class Context {
    public:
        explicit Context(CPUState& state) : state_(state) {}

        bool cdbQueueEmpty() const { return state_.cdb_queue.empty(); }
        CommonDataBusEntry popCdbEntry() {
            auto entry = state_.cdb_queue.front();
            state_.cdb_queue.pop();
            return entry;
        }
        void incrementCounter(PerfCounterId id, uint64_t amount = 1) {
            state_.perf_counters.increment(id, amount);
        }
        void updateWaitingOperands(const CommonDataBusEntry& cdb_entry) {
            state_.reservation_station->update_operands(cdb_entry, state_.store_buffer.get());
        }
        void updatePhysicalRegister(RegisterFileKind kind,
                                    PhysRegNum reg,
                                    uint64_t value,
                                    ROBEntry rob_entry) {
            state_.register_rename->update_physical_register(kind, reg, value, rob_entry);
        }
        DynamicInstPtr robEntry(ROBEntry rob_entry) const {
            return state_.reorder_buffer->get_entry(rob_entry);
        }
        void completeRobEntry(const DynamicInstPtr& instruction,
                              uint64_t result,
                              bool has_exception,
                              const std::string& exception_msg,
                              bool is_jump,
                              uint64_t jump_target) {
            state_.reorder_buffer->update_entry(
                instruction, result, has_exception, exception_msg, is_jump, jump_target);
        }

    private:
        CPUState& state_;
    };

    void execute(Context& context);
    const char* get_stage_name() const override { return "WRITEBACK"; }

private:

};

} // namespace riscv
