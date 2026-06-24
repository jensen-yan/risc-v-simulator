#include "cpu/ooo/stages/dispatch_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include "core/instruction_executor.h"

namespace riscv {

namespace {

constexpr uint8_t kFenceIFunct3 = 0b001;

bool isSerializingControlInstruction(const DecodedInstruction& decoded_info) {
    return (decoded_info.opcode == Opcode::SYSTEM &&
            InstructionExecutor::isTrapLikeSystemInstruction(decoded_info)) ||
           (decoded_info.opcode == Opcode::MISC_MEM &&
            static_cast<uint8_t>(decoded_info.funct3) == kFenceIFunct3);
}

bool hasOlderInflightSerializingInstruction(ReorderBuffer& reorder_buffer, uint64_t instruction_id) {
    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        const auto rob_entry = static_cast<ROBEntry>(i);
        if (!reorder_buffer.is_entry_valid(rob_entry)) {
            continue;
        }

        auto entry = reorder_buffer.get_entry(rob_entry);
        if (!entry || entry->is_retired() || entry->get_instruction_id() >= instruction_id) {
            continue;
        }

        if (isSerializingControlInstruction(entry->get_decoded_info())) {
            return true;
        }
    }
    return false;
}

}  // namespace

DispatchStage::DispatchStage() {
    // 构造函数：初始化派发阶段
}

bool DispatchStage::Context::hasOlderInflightSerializingInstruction(uint64_t instruction_id) const {
    return riscv::hasOlderInflightSerializingInstruction(*state_.reorder_buffer, instruction_id);
}

void DispatchStage::execute(Context& context) {
    context.incrementCounter(PerfCounterId::DISPATCH_SLOTS, OOOPipelineConfig::DISPATCH_WIDTH);

    if (context.reorderBufferEmpty()) {
        LOGT(DISPATCH, "rob empty, skip dispatch");
        return;
    }

    size_t dispatched_this_cycle = 0;

    for (size_t slot = 0; slot < OOOPipelineConfig::DISPATCH_WIDTH; ++slot) {
        auto dispatchable_entry = context.dispatchableRobEntry();
        if (!dispatchable_entry) {
            if (dispatched_this_cycle == 0) {
                LOGT(DISPATCH, "no dispatchable instruction");
                context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_NO_DISPATCHABLE);
            }
            break;
        }

        if (!dispatchable_entry->is_allocated()) {
            LOGW(DISPATCH, "unexpected rob entry status");
            break;
        }

        LOGT(DISPATCH, "try dispatch slot=%zu inst=%" PRId64 " (rob[%d])",
             slot, dispatchable_entry->get_instruction_id(), dispatchable_entry->get_rob_entry());

        const auto& decoded_info = dispatchable_entry->get_decoded_info();
        const auto head_entry = context.robHeadEntry();
        const bool is_serializing_control = isSerializingControlInstruction(decoded_info);

        if (context.hasOlderInflightSerializingInstruction(dispatchable_entry->get_instruction_id())) {
            if (dispatched_this_cycle == 0) {
                LOGT(DISPATCH, "older serializing control instruction blocks younger dispatch");
                context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_NO_DISPATCHABLE);
            }
            break;
        }

        if (decoded_info.opcode == Opcode::SYSTEM &&
            InstructionExecutor::isCsrInstruction(decoded_info) &&
            head_entry != dispatchable_entry->get_rob_entry()) {
            if (dispatched_this_cycle == 0) {
                LOGT(DISPATCH, "csr instruction waits for ROB head commit");
                context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_CSR_HEAD_BLOCKED);
            }
            break;
        }

        if (is_serializing_control && head_entry != dispatchable_entry->get_rob_entry()) {
            if (dispatched_this_cycle == 0) {
                LOGT(DISPATCH, "serializing control instruction waits for ROB head commit");
                context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_NO_DISPATCHABLE);
            }
            break;
        }

        const bool save_rename_checkpoint =
            decoded_info.opcode == Opcode::BRANCH || decoded_info.opcode == Opcode::JALR;
        const auto admission_result =
            context.admitInstruction(dispatchable_entry, save_rename_checkpoint);
        if (!admission_result.admitted()) {
            if (dispatched_this_cycle == 0) {
                switch (admission_result.status) {
                    case DispatchAdmission::Status::ReservationStationFull:
                        LOGT(DISPATCH, "reservation station full, dispatch stalled");
                        context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_RS_FULL);
                        break;
                    case DispatchAdmission::Status::RenameStall:
                        LOGT(DISPATCH, "rename failed, dispatch stalled");
                        context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_RENAME_FAIL);
                        break;
                    case DispatchAdmission::Status::InvalidInstruction:
                        LOGW(DISPATCH, "invalid instruction during dispatch admission");
                        context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_NO_DISPATCHABLE);
                        break;
                    case DispatchAdmission::Status::Admitted:
                        break;
                }
            }
            break;
        }

        LOGT(DISPATCH, "dispatched slot=%zu inst=%" PRId64 " to rs[%d]",
             slot,
             dispatchable_entry->get_instruction_id(),
             static_cast<int>(admission_result.rs_entry));

        context.incrementCounter(PerfCounterId::DISPATCHED_INSTRUCTIONS);
        dispatched_this_cycle++;

        if (is_serializing_control) {
            LOGT(DISPATCH, "serializing control inst=%" PRId64 " dispatched, stop younger dispatch",
                 dispatchable_entry->get_instruction_id());
            break;
        }
    }

    context.incrementCounter(PerfCounterId::DISPATCH_UTILIZED_SLOTS, dispatched_this_cycle);
}

} // namespace riscv 
