#include "cpu/ooo/stages/dispatch_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include "core/instruction_executor.h"

namespace riscv {

namespace {

constexpr uint8_t kFenceIFunct3 = 0b001;
constexpr uint8_t kFpFunct5Shift = 2;

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

uint8_t fpFunct5(const DecodedInstruction& decoded_info) {
    return static_cast<uint8_t>(static_cast<uint8_t>(decoded_info.funct7) >> kFpFunct5Shift);
}

RegisterFileKind fpSource1Kind(const DecodedInstruction& decoded_info) {
    if (decoded_info.opcode == Opcode::LOAD_FP || decoded_info.opcode == Opcode::STORE_FP) {
        return RegisterFileKind::Integer;
    }
    if (decoded_info.opcode == Opcode::FMADD || decoded_info.opcode == Opcode::FMSUB ||
        decoded_info.opcode == Opcode::FNMSUB || decoded_info.opcode == Opcode::FNMADD) {
        return RegisterFileKind::FloatingPoint;
    }
    if (decoded_info.opcode != Opcode::OP_FP) {
        return RegisterFileKind::None;
    }
    switch (fpFunct5(decoded_info)) {
        case 0x1A:
        case 0x1E:
            return RegisterFileKind::Integer;
        default:
            return RegisterFileKind::FloatingPoint;
    }
}

RegisterFileKind fpSource2Kind(const DecodedInstruction& decoded_info) {
    if (decoded_info.opcode == Opcode::STORE_FP) {
        return RegisterFileKind::FloatingPoint;
    }
    if (decoded_info.opcode == Opcode::FMADD || decoded_info.opcode == Opcode::FMSUB ||
        decoded_info.opcode == Opcode::FNMSUB || decoded_info.opcode == Opcode::FNMADD) {
        return RegisterFileKind::FloatingPoint;
    }
    if (decoded_info.opcode != Opcode::OP_FP) {
        return RegisterFileKind::None;
    }
    switch (fpFunct5(decoded_info)) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x14:
            return RegisterFileKind::FloatingPoint;
        default:
            return RegisterFileKind::None;
    }
}

RegisterFileKind fpSource3Kind(const DecodedInstruction& decoded_info) {
    switch (decoded_info.opcode) {
        case Opcode::FMADD:
        case Opcode::FMSUB:
        case Opcode::FNMSUB:
        case Opcode::FNMADD:
            return RegisterFileKind::FloatingPoint;
        default:
            return RegisterFileKind::None;
    }
}

RegisterFileKind fpDestinationKind(const DecodedInstruction& decoded_info) {
    if (decoded_info.opcode == Opcode::STORE_FP) {
        return RegisterFileKind::None;
    }
    return InstructionExecutor::isFPIntegerDestination(decoded_info)
               ? RegisterFileKind::Integer
               : RegisterFileKind::FloatingPoint;
}

void applyLookupToSrc1(const RegisterRenameUnit::SourceLookupResult& lookup,
                       const DynamicInstPtr& inst,
                       RegisterFileKind kind) {
    inst->set_physical_src1_kind(kind);
    inst->set_physical_src1(lookup.reg);
    inst->set_src1_ready(lookup.ready, lookup.value);
}

void applyLookupToSrc2(const RegisterRenameUnit::SourceLookupResult& lookup,
                       const DynamicInstPtr& inst,
                       RegisterFileKind kind) {
    inst->set_physical_src2_kind(kind);
    inst->set_physical_src2(lookup.reg);
    inst->set_src2_ready(lookup.ready, lookup.value);
}

void applyLookupToSrc3(const RegisterRenameUnit::SourceLookupResult& lookup,
                       const DynamicInstPtr& inst,
                       RegisterFileKind kind) {
    inst->set_physical_src3_kind(kind);
    inst->set_physical_src3(lookup.reg);
    inst->set_src3_ready(lookup.ready, lookup.value);
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

        if (!context.reservationStationHasFreeEntry()) {
            if (dispatched_this_cycle == 0) {
                LOGT(DISPATCH, "reservation station full, dispatch stalled");
                context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_RS_FULL);
            }
            break;
        }

        bool dispatched = false;

        if (InstructionExecutor::isFloatingPointInstruction(decoded_info)) {
            const auto src1_kind = fpSource1Kind(decoded_info);
            const auto src2_kind = fpSource2Kind(decoded_info);
            const auto src3_kind = fpSource3Kind(decoded_info);
            const auto dst_kind = fpDestinationKind(decoded_info);

            const auto src1 = context.lookupSource(src1_kind, decoded_info.rs1);
            const auto src2 = context.lookupSource(src2_kind, decoded_info.rs2);
            const auto src3 = context.lookupSource(src3_kind, decoded_info.rs3);
            const auto dest = context.allocateDestination(dst_kind, decoded_info.rd);
            if (!dest.success) {
                if (dispatched_this_cycle == 0) {
                    LOGT(DISPATCH, "rename failed for fp instruction");
                    context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_RENAME_FAIL);
                }
                break;
            }

            applyLookupToSrc1(src1, dispatchable_entry, src1_kind);
            applyLookupToSrc2(src2, dispatchable_entry, src2_kind);
            applyLookupToSrc3(src3, dispatchable_entry, src3_kind);
            dispatchable_entry->set_physical_dest_kind(dst_kind);
            dispatchable_entry->set_physical_dest(dest.dest_reg);

            auto dispatch_result = context.dispatchToReservationStation(dispatchable_entry);
            if (!dispatch_result.success) {
                context.releasePhysicalRegister(dst_kind, dest.dest_reg);
                if (dispatched_this_cycle == 0) {
                    LOGT(DISPATCH, "rs dispatch failed for fp instruction");
                    context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_RS_FULL);
                }
                break;
            }

            LOGT(DISPATCH, "dispatched slot=%zu fp inst=%" PRId64 " to rs[%d]",
                 slot, dispatchable_entry->get_instruction_id(), dispatch_result.rs_entry);
            context.publishReadyStore(dispatchable_entry);
            dispatched = true;
        } else {
            auto rename_result = context.renameInstruction(dispatchable_entry->get_decoded_info());
            if (!rename_result.success) {
                if (dispatched_this_cycle == 0) {
                    LOGT(DISPATCH, "rename failed, dispatch stalled");
                    context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_RENAME_FAIL);
                }
                break;
            }

            dispatchable_entry->set_physical_src1(rename_result.src1_reg);
            dispatchable_entry->set_physical_src2(rename_result.src2_reg);
            dispatchable_entry->set_physical_src1_kind(RegisterFileKind::Integer);
            dispatchable_entry->set_physical_src2_kind(RegisterFileKind::Integer);
            dispatchable_entry->set_physical_dest(rename_result.dest_reg);
            dispatchable_entry->set_physical_dest_kind(
                rename_result.dest_reg != 0 ? RegisterFileKind::Integer : RegisterFileKind::None);
            dispatchable_entry->set_physical_src3(0);
            dispatchable_entry->set_physical_src3_kind(RegisterFileKind::None);
            dispatchable_entry->set_src3_ready(true, 0);
            dispatchable_entry->set_src1_ready(rename_result.src1_ready, rename_result.src1_value);
            dispatchable_entry->set_src2_ready(rename_result.src2_ready, rename_result.src2_value);

            auto dispatch_result = context.dispatchToReservationStation(dispatchable_entry);
            if (!dispatch_result.success) {
                context.releasePhysicalRegister(RegisterFileKind::Integer, rename_result.dest_reg);
                if (dispatched_this_cycle == 0) {
                    LOGT(DISPATCH, "rs dispatch failed, rollback rename");
                    context.recordPipelineStall(PerfCounterId::STALL_DISPATCH_RS_FULL);
                }
                break;
            }

            LOGT(DISPATCH, "dispatched slot=%zu inst=%" PRId64 " to rs[%d]",
                 slot, dispatchable_entry->get_instruction_id(), dispatch_result.rs_entry);
            context.publishReadyStore(dispatchable_entry);
            dispatched = true;
        }

        if (!dispatched) {
            break;
        }

        context.incrementCounter(PerfCounterId::DISPATCHED_INSTRUCTIONS);
        dispatchable_entry->set_dispatch_cycle(context.cycleCount());
        dispatchable_entry->set_status(DynamicInst::Status::DISPATCHED);
        if (decoded_info.opcode == Opcode::BRANCH || decoded_info.opcode == Opcode::JALR) {
            context.saveRenameCheckpoint(dispatchable_entry->get_instruction_id(),
                                         context.captureRenameCheckpoint());
        }
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
