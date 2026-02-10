#include "cpu/ooo/stages/issue_stage.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/debug_types.h"
#include "core/instruction_executor.h"

namespace riscv {

IssueStage::IssueStage() {
    // 构造函数：初始化发射阶段
}

void IssueStage::execute(CPUState& state) {
    // 检查ROB中是否有待发射的指令
    if (state.reorder_buffer->is_empty()) {
        LOGT(ISSUE, "rob empty, skip issue");
        return;
    }
    
    // 遍历ROB，找到状态为ISSUED但还没有发射到保留站的指令
    // 这里简化实现：处理ROB中第一条状态为ISSUED的指令
    
    // 获取可以发射的指令
    auto dispatchable_entry = state.reorder_buffer->get_dispatchable_entry();
    if (!dispatchable_entry) {  // 没有可发射的指令
        LOGT(ISSUE, "no dispatchable instruction");
        return;
    }
    
    if (!dispatchable_entry->is_allocated()) {
        LOGW(ISSUE, "unexpected rob entry status");
        return;
    }
    
    LOGT(ISSUE, "try issue inst=%" PRId64 " (rob[%d])",
        dispatchable_entry->get_instruction_id(), dispatchable_entry->get_rob_entry());

    const auto head_entry = state.reorder_buffer->get_head_entry();
    if (head_entry != ReorderBuffer::MAX_ROB_ENTRIES) {
        auto head_inst = state.reorder_buffer->get_entry(head_entry);
        if (head_inst &&
            InstructionExecutor::isFloatingPointInstruction(head_inst->get_decoded_info()) &&
            head_entry != dispatchable_entry->get_rob_entry()) {
            LOGT(ISSUE, "fp instruction at ROB head, block younger issue");
            state.pipeline_stalls++;
            return;
        }
    }

    // CSR 指令按程序顺序执行，避免 CSR 读写乱序导致状态不一致
    const auto& decoded_info = dispatchable_entry->get_decoded_info();
    if (decoded_info.opcode == Opcode::SYSTEM &&
        InstructionExecutor::isCsrInstruction(decoded_info)) {
        if (head_entry != dispatchable_entry->get_rob_entry()) {
            LOGT(ISSUE, "csr instruction waits for ROB head commit");
            state.pipeline_stalls++;
            return;
        }
    }
    
    // 检查保留站是否有空闲表项
    if (!state.reservation_station->has_free_entry()) {
        LOGT(ISSUE, "reservation station full, issue stalled");
        state.pipeline_stalls++;
        return;
    }

    // 浮点相关指令保持顺序执行，直接使用架构寄存器值，避免与整数重命名状态耦合。
    if (InstructionExecutor::isFloatingPointInstruction(decoded_info)) {
        if (head_entry != dispatchable_entry->get_rob_entry()) {
            LOGT(ISSUE, "fp instruction waits for ROB head commit");
            state.pipeline_stalls++;
            return;
        }
        const bool fp_write_int = InstructionExecutor::isFPIntegerDestination(decoded_info);
        if (fp_write_int) {
            // 写整数寄存器的浮点指令需要走整数重命名链路，保证后继整数指令读取到最新值。
            auto rename_result = state.register_rename->rename_instruction(dispatchable_entry->get_decoded_info());
            if (!rename_result.success) {
                LOGT(ISSUE, "rename failed for fp-int instruction");
                state.pipeline_stalls++;
                return;
            }

            dispatchable_entry->set_physical_src1(0);
            dispatchable_entry->set_physical_src2(0);
            dispatchable_entry->set_physical_dest(rename_result.dest_reg);
            dispatchable_entry->set_src1_ready(true, state.arch_registers[decoded_info.rs1]);
            dispatchable_entry->set_src2_ready(true, state.arch_registers[decoded_info.rs2]);

            auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
            if (!issue_result.success) {
                LOGT(ISSUE, "rs issue failed for fp-int instruction");
                state.register_rename->release_physical_register(rename_result.dest_reg);
                state.pipeline_stalls++;
                return;
            }

            LOGT(ISSUE, "issued fp-int inst=%" PRId64 " to rs[%d]",
                 dispatchable_entry->get_instruction_id(), issue_result.rs_entry);
            dispatchable_entry->set_status(DynamicInst::Status::ISSUED);
            return;
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
                LOGT(ISSUE, "rs issue failed for fp instruction");
                state.pipeline_stalls++;
                return;
            }

            LOGT(ISSUE, "issued fp inst=%" PRId64 " to rs[%d]",
                 dispatchable_entry->get_instruction_id(), issue_result.rs_entry);
            dispatchable_entry->set_status(DynamicInst::Status::ISSUED);
            return;
        }
    }
    
    // 进行寄存器重命名
    auto rename_result = state.register_rename->rename_instruction(dispatchable_entry->get_decoded_info());
    if (!rename_result.success) {
        LOGT(ISSUE, "rename failed, issue stalled");
        state.pipeline_stalls++;
        return;
    }
    
    // 更新指令的重命名信息
    dispatchable_entry->set_physical_src1(rename_result.src1_reg);
    dispatchable_entry->set_physical_src2(rename_result.src2_reg);
    dispatchable_entry->set_physical_dest(rename_result.dest_reg);
    
    // 设置操作数就绪状态和值
    dispatchable_entry->set_src1_ready(rename_result.src1_ready, rename_result.src1_value);
    dispatchable_entry->set_src2_ready(rename_result.src2_ready, rename_result.src2_value);
    
    // 发射到保留站
    auto issue_result = state.reservation_station->issue_instruction(dispatchable_entry);
    if (!issue_result.success) {
        LOGT(ISSUE, "rs issue failed, rollback rename");
        state.register_rename->release_physical_register(rename_result.dest_reg);
        state.pipeline_stalls++;
        return;
    }
    
    LOGT(ISSUE, "issued inst=%" PRId64 " to rs[%d]",
        dispatchable_entry->get_instruction_id(), issue_result.rs_entry);
    
    // 更新指令状态，标记为已发射到保留站
    dispatchable_entry->set_status(DynamicInst::Status::ISSUED);
}

void IssueStage::flush() {
    LOGT(ISSUE, "issue stage flushed");
}

void IssueStage::reset() {
    LOGT(ISSUE, "issue stage reset");
}

} // namespace riscv 
