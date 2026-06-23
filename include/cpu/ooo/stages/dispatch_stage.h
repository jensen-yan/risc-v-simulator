#pragma once

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

        bool reservationStationHasFreeEntry() const {
            return state_.reservation_station->has_free_entry();
        }
        ReservationStation::DispatchResult dispatchToReservationStation(
            const DynamicInstPtr& instruction) {
            return state_.reservation_station->dispatch_instruction(instruction);
        }

        RegisterRenameUnit::RenameResult renameInstruction(const DecodedInstruction& decoded) {
            return state_.register_rename->rename_instruction(decoded);
        }
        void releasePhysicalRegister(RegisterFileKind kind, PhysRegNum reg) {
            state_.register_rename->release_physical_register(kind, reg);
        }
        RegisterRenameUnit::Checkpoint captureRenameCheckpoint() const {
            return state_.register_rename->capture_checkpoint();
        }
        void saveRenameCheckpoint(uint64_t instruction_id,
                                  const RegisterRenameUnit::Checkpoint& checkpoint) {
            state_.rename_checkpoints[instruction_id] = checkpoint;
        }

        void publishReadyStore(const DynamicInstPtr& instruction) {
            state_.store_buffer->publish_ready_store(instruction);
        }
        uint64_t cycleCount() const { return state_.cycle_count; }

    private:
        CPUState& state_;
    };

    void execute(Context& context);
    const char* get_stage_name() const override { return "DISPATCH"; }

private:
};

} // namespace riscv
