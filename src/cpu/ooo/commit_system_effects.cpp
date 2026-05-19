#include "cpu/ooo/commit_system_effects.h"

#include "common/debug_types.h"
#include "core/csr_utils.h"
#include "core/instruction_executor.h"

namespace riscv {

namespace {

constexpr uint8_t kFenceIFunct3 = 0b001;
constexpr uint32_t kMstatusCsrAddr = 0x300;

void writeCommittedCsr(CPUState& state, uint32_t csr_addr, uint64_t value) {
    if (state.cpu_interface) {
        state.cpu_interface->setCSR(csr_addr, value);
        return;
    }
    csr::write(state.csr_registers, csr_addr, value);
}

} // namespace

CommitSystemEffects::Result CommitSystemEffects::apply(
    CPUState& state,
    const DynamicInstPtr& instruction) {
    Result result;
    if (!instruction) {
        return result;
    }

    const auto& decoded_info = instruction->get_decoded_info();
    if (decoded_info.opcode == Opcode::SYSTEM) {
        result.applied = true;
        if (InstructionExecutor::isCsrInstruction(decoded_info)) {
            const uint32_t csr_addr = static_cast<uint32_t>(decoded_info.imm) & 0xFFFU;
            const auto csr_result = InstructionExecutor::executeCsrInstruction(
                decoded_info, instruction->get_src1_value(), csr::read(state.csr_registers, csr_addr));
            writeCommittedCsr(state, csr_addr, csr_result.write_value);
            LOGT(COMMIT, "inst=%" PRId64 " commit csr[0x%03x]: old=0x%" PRIx64 ", new=0x%" PRIx64,
                 instruction->get_instruction_id(), csr_addr,
                 csr_result.read_value, csr_result.write_value);
        } else if (InstructionExecutor::isSystemCall(decoded_info)) {
            const bool enters_trap = csr::machineTrapVectorBase(state.csr_registers) != 0;
            if (enters_trap) {
                result.has_flush_summary = true;
                result.flush_reason = OooRecovery::Reason::Trap;
            }
            result.should_stop_commit = handleEcall(state, instruction->get_pc());
            if (enters_trap && result.should_stop_commit) {
                result.has_redirect_pc = true;
                result.redirect_pc = state.pc;
            }
        } else if (InstructionExecutor::isBreakpoint(decoded_info)) {
            result.has_flush_summary = true;
            result.flush_reason = OooRecovery::Reason::Trap;
            result.should_stop_commit = handleEbreak(state, instruction->get_pc());
            if (result.should_stop_commit) {
                result.has_redirect_pc = true;
                result.redirect_pc = state.pc;
            }
        } else if (InstructionExecutor::isMachineReturn(decoded_info)) {
            result.has_flush_summary = true;
            result.flush_reason = OooRecovery::Reason::Mret;
            result.should_stop_commit = handleMret(state);
            if (result.should_stop_commit) {
                result.has_redirect_pc = true;
                result.redirect_pc = state.pc;
            }
        }
    } else if (decoded_info.opcode == Opcode::MISC_MEM &&
               static_cast<uint8_t>(decoded_info.funct3) == kFenceIFunct3) {
        result.applied = true;
        result.has_flush_summary = true;
        result.flush_reason = OooRecovery::Reason::FenceI;
        result.should_stop_commit =
            handleFenceI(state, instruction->get_pc(), decoded_info.is_compressed);
        if (result.should_stop_commit) {
            result.has_redirect_pc = true;
            result.redirect_pc = state.pc;
        }
    }

    return result;
}

bool CommitSystemEffects::handleEcall(CPUState& state, uint64_t instruction_pc) {
    LOGT(COMMIT, "detected ECALL at pc=0x%" PRIx64, instruction_pc);
    LOGT(COMMIT, "ecall args: a7(x17)=%" PRIx64 ", a0(x10)=%" PRIx64 ", a1(x11)=%" PRIx64 ", pc=0x%" PRIx64,
         state.arch_registers[17], state.arch_registers[10],
         state.arch_registers[11], instruction_pc);

    if (csr::machineTrapVectorBase(state.csr_registers) != 0) {
        enterMachineTrap(state, instruction_pc, csr::kMachineEcallCause, 0);
        return true;
    }

    if (state.syscall_handler && state.cpu_interface) {
        LOGT(COMMIT, "invoke syscall handler");
        bool should_halt = state.syscall_handler->handleSyscall(state.cpu_interface);
        if (should_halt) {
            state.halted = true;
            LOGT(COMMIT, "syscall handling finished, halt program");
            return true;
        }

        LOGT(COMMIT, "syscall handling finished, continue execution");
        return false;
    }

    LOGW(COMMIT, "missing syscall handler, halt directly");
    state.halted = true;
    return true;
}

bool CommitSystemEffects::handleEbreak(CPUState& state, uint64_t instruction_pc) {
    enterMachineTrap(state, instruction_pc, csr::kBreakpointCause, instruction_pc);
    return true;
}

bool CommitSystemEffects::handleMret(CPUState& state) {
    uint64_t mstatus = csr::read(state.csr_registers, kMstatusCsrAddr);
    const auto restored_mode = applyMretPrivilegeMode(mstatus);
    if (!restored_mode.has_value()) {
        throw IllegalInstructionException("MRET mstatus.MPP 非法");
    }
    writeCommittedCsr(state, kMstatusCsrAddr, mstatus);
    if (state.cpu_interface) {
        state.cpu_interface->setPrivilegeMode(*restored_mode);
    } else if (state.privilege_state) {
        state.privilege_state->setMode(*restored_mode);
    }
    state.pc = csr::read(state.csr_registers, csr::kMepc);
    flushPipelineAfterCommit(state, OooRecovery::Reason::Mret);
    return true;
}

bool CommitSystemEffects::handleFenceI(CPUState& state, uint64_t instruction_pc, bool is_compressed) {
    const uint64_t next_pc = instruction_pc + (is_compressed ? 2ULL : 4ULL);
    LOGT(COMMIT, "detected FENCE.I at pc=0x%" PRIx64 ", refetch from 0x%" PRIx64, instruction_pc, next_pc);
    state.pc = next_pc;
    flushPipelineAfterCommit(state, OooRecovery::Reason::FenceI);
    return true;
}

void CommitSystemEffects::enterMachineTrap(CPUState& state,
                                           uint64_t instruction_pc,
                                           uint64_t cause,
                                           uint64_t tval) {
    PrivilegeMode current_mode = PrivilegeMode::MACHINE;
    if (state.cpu_interface) {
        current_mode = state.cpu_interface->getPrivilegeMode();
    } else if (state.privilege_state) {
        current_mode = state.privilege_state->getMode();
    }

    state.pc = csr::enterMachineTrap(state.csr_registers, instruction_pc, cause, tval, current_mode);
    if (state.cpu_interface) {
        state.cpu_interface->setPrivilegeMode(PrivilegeMode::MACHINE);
        state.cpu_interface->setCSR(csr::kMstatus, csr::read(state.csr_registers, csr::kMstatus));
    } else if (state.privilege_state) {
        state.privilege_state->setMode(PrivilegeMode::MACHINE);
        state.privilege_state->setMstatus(csr::read(state.csr_registers, csr::kMstatus));
    }
    flushPipelineAfterCommit(state, OooRecovery::Reason::Trap);
}

void CommitSystemEffects::flushPipelineAfterCommit(CPUState& state, OooRecovery::Reason reason) {
    LOGT(COMMIT, "serializing event committed, start pipeline flush");
    OooRecovery::FullPipelineRequest request;
    request.reason = reason;
    request.clear_reservation = true;
    request.reset_execution_units = true;
    OooRecovery::recoverFullPipeline(state, request);
    LOGT(COMMIT, "pipeline flush completed, restart fetch");
}

} // namespace riscv
