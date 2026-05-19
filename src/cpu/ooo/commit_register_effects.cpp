#include "cpu/ooo/commit_register_effects.h"

#include "common/debug_types.h"
#include "core/csr_utils.h"
#include "core/instruction_executor.h"

#include <utility>

namespace riscv {

namespace {

CommitRegisterEffects::Result failedResult(std::string error_message) {
    CommitRegisterEffects::Result result;
    result.success = false;
    result.applied = false;
    result.error_message = std::move(error_message);
    return result;
}

} // namespace

CommitRegisterEffects::Result CommitRegisterEffects::applyInteger(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    const auto& decoded_info = instruction->get_decoded_info();
    const bool writes_integer_reg = decoded_info.rd != 0;

    if (writes_integer_reg) {
        state.arch_registers[decoded_info.rd] = instruction->get_result();
        state.register_rename->update_architecture_register(
            RegisterFileKind::Integer,
            decoded_info.rd,
            instruction->get_result());
        LOGT(COMMIT, "inst=%" PRId64 " x%d = 0x%" PRIx64,
             instruction->get_instruction_id(),
             decoded_info.rd,
             instruction->get_result());
    } else {
        LOGT(COMMIT, "inst=%" PRId64 " (no destination register)",
             instruction->get_instruction_id());
    }

    state.register_rename->commit_instruction(RegisterFileKind::Integer,
                                             instruction->get_logical_dest(),
                                             instruction->get_physical_dest());

    Result result;
    result.applied = true;
    return result;
}

CommitRegisterEffects::Result CommitRegisterEffects::applyFloatingPoint(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    const auto& decoded_info = instruction->get_decoded_info();
    DynamicInst::FpExecuteInfo fp_info{};
    if (decoded_info.opcode == Opcode::LOAD_FP) {
        fp_info.value = instruction->get_result();
        fp_info.write_fp_reg = true;
    } else if (decoded_info.opcode == Opcode::STORE_FP) {
        // No architectural register destination.
    } else {
        if (!instruction->has_fp_execute_info()) {
            LOGE(COMMIT, "missing fp execute info at commit, pc=0x%" PRIx64,
                 instruction->get_pc());
            return failedResult("missing fp execute info at commit");
        }
        fp_info = instruction->get_fp_execute_info();
    }

    if (fp_info.fflags != 0) {
        csr::write(state.csr_registers,
                   csr::kFflags,
                   csr::read(state.csr_registers, csr::kFflags) | fp_info.fflags);
    }

    if (fp_info.write_int_reg && decoded_info.rd != 0) {
        const uint64_t int_result = fp_info.value;
        state.arch_registers[decoded_info.rd] = int_result;
        if (instruction->get_physical_dest_kind() == RegisterFileKind::Integer &&
            instruction->get_physical_dest() != 0) {
            state.register_rename->commit_instruction(RegisterFileKind::Integer,
                                                     instruction->get_logical_dest(),
                                                     instruction->get_physical_dest());
        }
        state.register_rename->update_architecture_register(RegisterFileKind::Integer,
                                                            decoded_info.rd,
                                                            int_result);
        LOGT(COMMIT, "inst=%" PRId64 " x%d = 0x%" PRIx64,
             instruction->get_instruction_id(), decoded_info.rd, int_result);
    } else if (fp_info.write_fp_reg) {
        state.arch_fp_registers[decoded_info.rd] = fp_info.value;
        if (instruction->get_physical_dest_kind() == RegisterFileKind::FloatingPoint) {
            state.register_rename->commit_instruction(RegisterFileKind::FloatingPoint,
                                                     instruction->get_logical_dest(),
                                                     instruction->get_physical_dest());
            state.register_rename->update_architecture_register(RegisterFileKind::FloatingPoint,
                                                                decoded_info.rd,
                                                                fp_info.value);
        }
        LOGT(COMMIT, "inst=%" PRId64 " f%d = 0x%016" PRIx64,
             instruction->get_instruction_id(), decoded_info.rd, fp_info.value);
    } else {
        LOGT(COMMIT, "inst=%" PRId64 " (no destination register)",
             instruction->get_instruction_id());
    }

    Result result;
    result.applied = true;
    return result;
}

CommitRegisterEffects::Result CommitRegisterEffects::apply(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    if (!instruction) {
        return {};
    }

    if (InstructionExecutor::isFloatingPointInstruction(instruction->get_decoded_info())) {
        return applyFloatingPoint(state, instruction);
    }
    return applyInteger(state, instruction);
}

} // namespace riscv
