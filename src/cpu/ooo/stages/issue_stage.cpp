#include "cpu/ooo/stages/issue_stage.h"
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

        const auto head_entry = state.reorder_buffer->get_head_entry();
        if (head_entry != ReorderBuffer::MAX_ROB_ENTRIES) {
            auto head_inst = state.reorder_buffer->get_entry(head_entry);
            if (head_inst &&
                InstructionExecutor::isFloatingPointInstruction(head_inst->get_decoded_info()) &&
                head_entry != dispatchable_entry->get_rob_entry()) {
                if (issued_this_cycle == 0) {
                    LOGT(ISSUE, "fp instruction at ROB head, block younger issue");
                    state.recordPipelineStall(PerfCounterId::STALL_ISSUE_FP_HEAD_BLOCKED);
                }
                break;
            }
        }

        const auto& decoded_info = dispatchable_entry->get_decoded_info();
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
            if (head_entry != dispatchable_entry->get_rob_entry()) {
                if (issued_this_cycle == 0) {
                    LOGT(ISSUE, "fp instruction waits for ROB head commit");
                    state.recordPipelineStall(PerfCounterId::STALL_ISSUE_FP_HEAD_BLOCKED);
                }
                break;
            }

            const bool fp_write_int = InstructionExecutor::isFPIntegerDestination(decoded_info);
            if (fp_write_int) {
                auto rename_result = state.register_rename->rename_instruction(dispatchable_entry->get_decoded_info());
                if (!rename_result.success) {
                    if (issued_this_cycle == 0) {
                        LOGT(ISSUE, "rename failed for fp-int instruction");
                        state.recordPipelineStall(PerfCounterId::STALL_ISSUE_RENAME_FAIL);
                    }
                    break;
                }

                dispatchable_entry->set_physical_src1(0);
                dispatchable_entry->set_physical_src2(0);
                dispatchable_entry->set_physical_dest(rename_result.dest_reg);
                dispatchable_entry->set_src1_ready(true, state.arch_registers[decoded_info.rs1]);
                dispatchable_entry->set_src2_ready(true, state.arch_registers[decoded_info.rs2]);

                auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
                if (!issue_result.success) {
                    state.register_rename->release_physical_register(rename_result.dest_reg);
                    if (issued_this_cycle == 0) {
                        LOGT(ISSUE, "rs issue failed for fp-int instruction");
                        state.recordPipelineStall(PerfCounterId::STALL_ISSUE_RS_FULL);
                    }
                    break;
                }

                LOGT(ISSUE, "issued slot=%zu fp-int inst=%" PRId64 " to rs[%d]",
                     slot, dispatchable_entry->get_instruction_id(), issue_result.rs_entry);
                issued = true;
            } else {
                dispatchable_entry->set_physical_src1(0);
                dispatchable_entry->set_physical_src2(0);
                dispatchable_entry->set_physical_dest(0);

                uint64_t src1_value = state.arch_registers[decoded_info.rs1];
                uint64_t src2_value = state.arch_registers[decoded_info.rs2];
                if (decoded_info.opcode == Opcode::STORE_FP) {
                    src2_value = state.arch_fp_registers[decoded_info.rs2];
                }
                dispatchable_entry->set_src1_ready(true, src1_value);
                dispatchable_entry->set_src2_ready(true, src2_value);

                auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
                if (!issue_result.success) {
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
            }
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
            dispatchable_entry->set_physical_dest(rename_result.dest_reg);
            dispatchable_entry->set_src1_ready(rename_result.src1_ready, rename_result.src1_value);
            dispatchable_entry->set_src2_ready(rename_result.src2_ready, rename_result.src2_value);

            auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
            if (!issue_result.success) {
                state.register_rename->release_physical_register(rename_result.dest_reg);
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
