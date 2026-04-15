#include "cpu/ooo/stages/issue_stage.h"
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

bool hasOlderInflightSerializingInstruction(const CPUState& state, uint64_t instruction_id) {
    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        const auto rob_entry = static_cast<ROBEntry>(i);
        if (!state.reorder_buffer->is_entry_valid(rob_entry)) {
            continue;
        }

        auto entry = state.reorder_buffer->get_entry(rob_entry);
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

IssueStage::IssueStage() {
    // 构造函数：初始化发射阶段
}

void IssueStage::execute(CPUState& state) {
    state.perf_counters.increment(PerfCounterId::ISSUE_SLOTS, OOOPipelineConfig::ISSUE_WIDTH);

    if (state.reorder_buffer->is_empty()) {
        LOGT(ISSUE, "rob empty, skip issue");
        return;
    }

    size_t issued_this_cycle = 0;

    for (size_t slot = 0; slot < OOOPipelineConfig::ISSUE_WIDTH; ++slot) {
        auto dispatchable_entry = state.reorder_buffer->get_dispatchable_entry();
        if (!dispatchable_entry) {
            if (issued_this_cycle == 0) {
                LOGT(ISSUE, "no dispatchable instruction");
                state.recordPipelineStall(PerfCounterId::STALL_ISSUE_NO_DISPATCHABLE);
            }
            break;
        }

        if (!dispatchable_entry->is_allocated()) {
            LOGW(ISSUE, "unexpected rob entry status");
            break;
        }

        LOGT(ISSUE, "try issue slot=%zu inst=%" PRId64 " (rob[%d])",
             slot, dispatchable_entry->get_instruction_id(), dispatchable_entry->get_rob_entry());

        const auto& decoded_info = dispatchable_entry->get_decoded_info();
        const auto head_entry = state.reorder_buffer->get_head_entry();
        const bool is_serializing_control = isSerializingControlInstruction(decoded_info);

        if (hasOlderInflightSerializingInstruction(state, dispatchable_entry->get_instruction_id())) {
            if (issued_this_cycle == 0) {
                LOGT(ISSUE, "older serializing control instruction blocks younger issue");
                state.recordPipelineStall(PerfCounterId::STALL_ISSUE_NO_DISPATCHABLE);
            }
            break;
        }

        if (decoded_info.opcode == Opcode::SYSTEM &&
            InstructionExecutor::isCsrInstruction(decoded_info) &&
            head_entry != dispatchable_entry->get_rob_entry()) {
            if (issued_this_cycle == 0) {
                LOGT(ISSUE, "csr instruction waits for ROB head commit");
                state.recordPipelineStall(PerfCounterId::STALL_ISSUE_CSR_HEAD_BLOCKED);
            }
            break;
        }

        if (is_serializing_control && head_entry != dispatchable_entry->get_rob_entry()) {
            if (issued_this_cycle == 0) {
                LOGT(ISSUE, "serializing control instruction waits for ROB head commit");
                state.recordPipelineStall(PerfCounterId::STALL_ISSUE_NO_DISPATCHABLE);
            }
            break;
        }

        if (!state.reservation_station->has_free_entry()) {
            if (issued_this_cycle == 0) {
                LOGT(ISSUE, "reservation station full, issue stalled");
                state.recordPipelineStall(PerfCounterId::STALL_ISSUE_RS_FULL);
            }
            break;
        }

        bool issued = false;

        if (InstructionExecutor::isFloatingPointInstruction(decoded_info)) {
            const auto src1_kind = fpSource1Kind(decoded_info);
            const auto src2_kind = fpSource2Kind(decoded_info);
            const auto src3_kind = fpSource3Kind(decoded_info);
            const auto dst_kind = fpDestinationKind(decoded_info);

            const auto src1 = state.register_rename->lookup_source(src1_kind, decoded_info.rs1);
            const auto src2 = state.register_rename->lookup_source(src2_kind, decoded_info.rs2);
            const auto src3 = state.register_rename->lookup_source(src3_kind, decoded_info.rs3);
            const auto dest = state.register_rename->allocate_destination(dst_kind, decoded_info.rd);
            if (!dest.success) {
                if (issued_this_cycle == 0) {
                    LOGT(ISSUE, "rename failed for fp instruction");
                    state.recordPipelineStall(PerfCounterId::STALL_ISSUE_RENAME_FAIL);
                }
                break;
            }

            applyLookupToSrc1(src1, dispatchable_entry, src1_kind);
            applyLookupToSrc2(src2, dispatchable_entry, src2_kind);
            applyLookupToSrc3(src3, dispatchable_entry, src3_kind);
            dispatchable_entry->set_physical_dest_kind(dst_kind);
            dispatchable_entry->set_physical_dest(dest.dest_reg);

            auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
            if (!issue_result.success) {
                state.register_rename->release_physical_register(dst_kind, dest.dest_reg);
                if (issued_this_cycle == 0) {
                    LOGT(ISSUE, "rs issue failed for fp instruction");
                    state.recordPipelineStall(PerfCounterId::STALL_ISSUE_RS_FULL);
                }
                break;
            }

            LOGT(ISSUE, "issued slot=%zu fp inst=%" PRId64 " to rs[%d]",
                 slot, dispatchable_entry->get_instruction_id(), issue_result.rs_entry);
            state.store_buffer->publish_ready_store(dispatchable_entry);
            issued = true;
        } else {
            auto rename_result = state.register_rename->rename_instruction(dispatchable_entry->get_decoded_info());
            if (!rename_result.success) {
                if (issued_this_cycle == 0) {
                    LOGT(ISSUE, "rename failed, issue stalled");
                    state.recordPipelineStall(PerfCounterId::STALL_ISSUE_RENAME_FAIL);
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

            auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
            if (!issue_result.success) {
                state.register_rename->release_physical_register(RegisterFileKind::Integer,
                                                                 rename_result.dest_reg);
                if (issued_this_cycle == 0) {
                    LOGT(ISSUE, "rs issue failed, rollback rename");
                    state.recordPipelineStall(PerfCounterId::STALL_ISSUE_RS_FULL);
                }
                break;
            }

            LOGT(ISSUE, "issued slot=%zu inst=%" PRId64 " to rs[%d]",
                 slot, dispatchable_entry->get_instruction_id(), issue_result.rs_entry);
            state.store_buffer->publish_ready_store(dispatchable_entry);
            issued = true;
        }

        if (!issued) {
            break;
        }

        state.perf_counters.increment(PerfCounterId::ISSUED_INSTRUCTIONS);
        dispatchable_entry->set_issue_cycle(state.cycle_count);
        dispatchable_entry->set_status(DynamicInst::Status::ISSUED);
        if (decoded_info.opcode == Opcode::BRANCH || decoded_info.opcode == Opcode::JALR) {
            state.rename_checkpoints[dispatchable_entry->get_instruction_id()] =
                state.register_rename->capture_checkpoint();
        }
        issued_this_cycle++;

        if (is_serializing_control) {
            LOGT(ISSUE, "serializing control inst=%" PRId64 " issued, stop younger issue",
                 dispatchable_entry->get_instruction_id());
            break;
        }
    }

    state.perf_counters.increment(PerfCounterId::ISSUE_UTILIZED_SLOTS, issued_this_cycle);
}

void IssueStage::flush() {
    LOGT(ISSUE, "issue stage flushed");
}

void IssueStage::reset() {
    LOGT(ISSUE, "issue stage reset");
}

} // namespace riscv 
