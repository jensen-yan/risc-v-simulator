#include "cpu/ooo/execute_semantics.h"

#include "common/debug_types.h"
#include "common/types.h"
#include "core/csr_utils.h"
#include "core/instruction_executor.h"

namespace riscv {

namespace {

uint64_t readAtomicMemoryValue(std::shared_ptr<Memory> memory, uint64_t addr, Funct3 width) {
    switch (width) {
        case Funct3::LW:
            return memory->readWord(addr);
        case Funct3::LD:
            return memory->read64(addr);
        default:
            throw IllegalInstructionException("A扩展仅支持W/D宽度");
    }
}

bool isExtensionEnabled(const CPUState& state, Extension extension) {
    return (state.enabled_extensions & static_cast<uint32_t>(extension)) != 0;
}

bool isInstructionAddressMisaligned(const CPUState& state, uint64_t addr) {
    // C 扩展开启时 IALIGN=16，否则 IALIGN=32。
    if (isExtensionEnabled(state, Extension::C)) {
        return (addr & 0x1ULL) != 0;
    }
    return (addr & 0x3ULL) != 0;
}

void executeAtomicOperation(ExecutionUnit& unit, const DynamicInstPtr& instruction, CPUState& state) {
    const auto& inst = instruction->get_decoded_info();
    const uint64_t addr = instruction->get_src1_value();
    const uint64_t memory_value = readAtomicMemoryValue(state.memory, addr, inst.funct3);
    const bool reservation_hit = state.reservation_valid && (state.reservation_addr == addr);

    const auto amo_result = InstructionExecutor::executeAtomicOperation(
        inst, memory_value, instruction->get_src2_value(), reservation_hit);

    // LR/SC 的成败判定依赖当前reservation状态，需在执行阶段即时更新。
    if (amo_result.acquire_reservation) {
        state.reservation_valid = true;
        state.reservation_addr = addr;
    }
    if (amo_result.release_reservation) {
        state.reservation_valid = false;
    }

    DynamicInst::AtomicExecuteInfo atomic_info{};
    atomic_info.acquire_reservation = amo_result.acquire_reservation;
    atomic_info.release_reservation = amo_result.release_reservation;
    atomic_info.do_store = amo_result.do_store;
    atomic_info.address = addr;
    atomic_info.store_value = amo_result.store_value;
    atomic_info.width = inst.funct3;

    if (amo_result.do_store) {
        state.store_buffer->add_store(instruction, addr, amo_result.store_value, inst.memory_access_size);
        state.perf_counters.increment(PerfCounterId::STORES_TO_BUFFER);
    }
    instruction->set_atomic_execute_info(atomic_info);

    unit.result = amo_result.rd_value;
}

}  // namespace

void OOOExecuteSemantics::executeInstruction(ExecutionUnit& unit, const DynamicInstPtr& instruction, CPUState& state) {
    try {
        const auto& inst = instruction->get_decoded_info();

        // 首先检查解码时发现的异常
        if (inst.has_decode_exception) {
            unit.has_exception = true;
            unit.exception_msg = inst.decode_exception_msg;
            LOGW(EXECUTE, "decode exception: %s", inst.decode_exception_msg.c_str());
            return;
        }

        switch (inst.type) {
            case InstructionType::R_TYPE:
                if (inst.opcode == Opcode::AMO) {
                    executeAtomicOperation(unit, instruction, state);
                } else if (InstructionExecutor::isFloatingPointInstruction(inst)) {
                    const uint8_t current_frm =
                        static_cast<uint8_t>(csr::read(state.csr_registers, csr::kFrm) & 0x7U);
                    DynamicInst::FpExecuteInfo fp_info{};
                    if (inst.opcode == Opcode::FMADD ||
                        inst.opcode == Opcode::FMSUB ||
                        inst.opcode == Opcode::FNMSUB ||
                        inst.opcode == Opcode::FNMADD) {
                        const auto fp_result = InstructionExecutor::executeFusedFPOperation(
                            inst,
                            state.arch_fp_registers[inst.rs1],
                            state.arch_fp_registers[inst.rs2],
                            state.arch_fp_registers[inst.rs3],
                            current_frm);
                        fp_info.value = fp_result.value;
                        fp_info.write_int_reg = fp_result.write_int_reg;
                        fp_info.write_fp_reg = fp_result.write_fp_reg;
                        fp_info.fflags = fp_result.fflags;
                    } else {
                        const auto fp_result = InstructionExecutor::executeFPOperation(
                            inst,
                            state.arch_fp_registers[inst.rs1],
                            state.arch_fp_registers[inst.rs2],
                            state.arch_registers[inst.rs1],
                            current_frm);
                        fp_info.value = fp_result.value;
                        fp_info.write_int_reg = fp_result.write_int_reg;
                        fp_info.write_fp_reg = fp_result.write_fp_reg;
                        fp_info.fflags = fp_result.fflags;
                    }
                    unit.result = fp_info.value;
                    instruction->set_fp_execute_info(fp_info);
                } else if (inst.opcode == Opcode::OP) {
                    // OP指令包含基础整数和M扩展，按funct7分流
                    if (inst.funct7 == Funct7::M_EXT) {
                        unit.result = InstructionExecutor::executeMExtension(
                            inst, instruction->get_src1_value(), instruction->get_src2_value());
                    } else {
                        unit.result = InstructionExecutor::executeRegisterOperation(
                            inst, instruction->get_src1_value(), instruction->get_src2_value());
                    }
                } else if (inst.opcode == Opcode::OP_32) {
                    // RV64I: 32位寄存器运算（W后缀），含M扩展
                    if (inst.funct7 == Funct7::M_EXT) {
                        unit.result = InstructionExecutor::executeMExtension32(
                            inst, instruction->get_src1_value(), instruction->get_src2_value());
                    } else {
                        unit.result = InstructionExecutor::executeRegisterOperation32(
                            inst, instruction->get_src1_value(), instruction->get_src2_value());
                    }
                } else {
                    // 其他R_TYPE指令（如M扩展、F扩展等）
                    unit.result = InstructionExecutor::executeRegisterOperation(
                        inst, instruction->get_src1_value(), instruction->get_src2_value());
                }
                break;

            case InstructionType::I_TYPE:
                if (inst.opcode == Opcode::OP_IMM) {
                    // 立即数运算
                    unit.result = InstructionExecutor::executeImmediateOperation(inst, instruction->get_src1_value());
                } else if (inst.opcode == Opcode::OP_IMM_32) {
                    // RV64I: 32位立即数运算（W后缀）
                    unit.result = InstructionExecutor::executeImmediateOperation32(inst, instruction->get_src1_value());
                } else if (inst.opcode == Opcode::LOAD || inst.opcode == Opcode::LOAD_FP) {
                    // 加载指令 - 使用预解析的静态信息
                    uint64_t addr = instruction->get_src1_value() + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));

                    // 异常已在解码时检测，这里直接使用预解析的信息
                    unit.load_address = addr;
                    unit.load_size = inst.memory_access_size;
                    LOGT(EXECUTE, "start LOAD: addr=0x%" PRIx64 ", size=%d", addr, inst.memory_access_size);

                } else if (inst.opcode == Opcode::JALR) {
                    // JALR 指令 - I-type 跳转指令
                    // JALR 指令：跳转目标地址 = rs1 + imm，并清除最低位
                    const uint64_t target = InstructionExecutor::calculateJumpAndLinkTarget(
                        inst, instruction->get_pc(), instruction->get_src1_value());
                    if (isInstructionAddressMisaligned(state, target)) {
                        instruction->set_trap(0, target);
                        unit.is_jump = false;
                        unit.jump_target = 0;
                        LOGT(EXECUTE, "JALR misaligned trap: pc=0x%" PRIx64 " target=0x%" PRIx64,
                             instruction->get_pc(), target);
                        break;
                    }

                    unit.result = instruction->get_pc() + (inst.is_compressed ? 2 : 4);
                    unit.jump_target = target;
                    unit.is_jump = true;  // 标记为跳转指令
                    instruction->set_jump_info(true, unit.jump_target);
                } else if (inst.opcode == Opcode::MISC_MEM) {
                    // FENCE/FENCE.I：在当前单核模型中作为NOP处理
                    unit.result = 0;
                } else {
                    unit.has_exception = true;
                    unit.exception_msg = "unsupported I-type instruction";
                }
                break;

            case InstructionType::SYSTEM_TYPE:
                if (inst.opcode != Opcode::SYSTEM) {
                    unit.has_exception = true;
                    unit.exception_msg = "invalid SYSTEM_TYPE opcode";
                    break;
                }

                if (inst.funct3 == Funct3::ECALL_EBREAK) {
                    // ECALL/EBREAK/MRET/SRET/URET 等在提交阶段处理，不在执行阶段改状态。
                    if (InstructionExecutor::isTrapLikeSystemInstruction(inst)) {
                        unit.result = 0;
                        break;
                    }

                    unit.has_exception = true;
                    unit.exception_msg = "unsupported system instruction";
                    break;
                }

                if (!InstructionExecutor::isCsrInstruction(inst)) {
                    unit.has_exception = true;
                    unit.exception_msg = "unsupported system funct3";
                    break;
                }

                {
                    const uint32_t csr_addr = static_cast<uint32_t>(inst.imm) & 0xFFFU;
                    const auto csr_result = InstructionExecutor::executeCsrInstruction(
                        inst, instruction->get_src1_value(), csr::read(state.csr_registers, csr_addr));
                    unit.result = csr_result.read_value;
                    LOGT(EXECUTE, "inst=%" PRId64 " csr[0x%03x]: old=0x%" PRIx64 ", pending_new=0x%" PRIx64,
                         instruction->get_instruction_id(), csr_addr,
                         csr_result.read_value, csr_result.write_value);
                }
                break;

            case InstructionType::B_TYPE:
                // 分支指令（BNE, BEQ, BLT等）
                {
                    state.perf_counters.increment(PerfCounterId::BRANCH_INSTRUCTIONS);

                    bool should_branch = InstructionExecutor::evaluateBranchCondition(
                        inst, instruction->get_src1_value(), instruction->get_src2_value());

                    // 设置分支结果（分支指令通常不写回寄存器，但需要完成执行）
                    unit.result = 0;  // 分支指令没有写回值

                    // 简单的分支预测：静态预测不跳转
                    bool predicted_taken = false;  // 总是预测不跳转

                    if (should_branch) {
                        // 分支taken：条件成立，需要跳转
                        const uint64_t target =
                            instruction->get_pc() + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
                        if (isInstructionAddressMisaligned(state, target)) {
                            instruction->set_trap(0, target);
                            unit.is_jump = false;
                            unit.jump_target = 0;
                            LOGT(EXECUTE, "BRANCH misaligned trap: pc=0x%" PRIx64 " target=0x%" PRIx64,
                                 instruction->get_pc(), target);
                            break;
                        }

                        unit.jump_target = target;
                        unit.is_jump = true;  // 标记需要改变PC
                        instruction->set_jump_info(true, unit.jump_target);

                        if (!predicted_taken) {
                            // 预测不跳转，但实际跳转 -> 预测错误
                            LOGT(EXECUTE,
                                 "branch taken, target=0x%" PRIx64 " (pc=0x%" PRIx64 " + imm=%d), flush at commit",
                                 unit.jump_target, instruction->get_pc(), inst.imm);
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.recordBranchMispredict();
                        } else {
                            // 预测跳转，实际跳转 -> 预测正确
                            LOGT(EXECUTE, "branch taken, target=0x%" PRIx64 " (prediction correct)", unit.jump_target);
                        }
                    } else {
                        // 分支not taken：条件不成立，继续顺序执行
                        unit.is_jump = false;  // 不需要改变PC
                        unit.jump_target = 0;

                        if (predicted_taken) {
                            // 预测跳转，但实际不跳转 -> 预测错误
                            LOGT(EXECUTE, "branch not taken, flush at commit");
                            // 注意：不在执行阶段刷新，让指令正常完成并提交
                            state.recordBranchMispredict();
                        } else {
                            // 预测不跳转，实际不跳转 -> 预测正确
                            LOGT(EXECUTE, "branch not taken (prediction correct)");
                        }
                    }
                }
                break;

            case InstructionType::S_TYPE:
                // 存储指令 - 使用预解析的静态信息
                {
                    uint64_t addr = instruction->get_src1_value() + static_cast<uint64_t>(static_cast<int64_t>(inst.imm));
                    const uint64_t store_value = instruction->get_src2_value();

                    auto& memory_info = instruction->get_memory_info();
                    memory_info.is_memory_op = true;
                    memory_info.is_store = true;
                    memory_info.memory_address = addr;
                    memory_info.memory_value = store_value;
                    memory_info.memory_size = inst.memory_access_size;
                    memory_info.address_ready = true;

                    // 异常已在解码时检测，这里直接使用预解析的信息
                    LOGT(EXECUTE, "execute STORE: addr=0x%" PRIx64 " value=0x%" PRIx64 " size=%d",
                         addr, store_value, inst.memory_access_size);

                    // 仅记录待提交Store，真正写内存在commit阶段进行。
                    state.store_buffer->add_store(instruction, addr, store_value, inst.memory_access_size);
                    state.perf_counters.increment(PerfCounterId::STORES_TO_BUFFER);
                }
                break;

            case InstructionType::U_TYPE:
                // 上位立即数指令
                unit.result = InstructionExecutor::executeUpperImmediate(inst, instruction->get_pc());
                break;

            case InstructionType::J_TYPE:
                {
                    // JAL 指令 - J-type 无条件跳转
                    const uint64_t target = InstructionExecutor::calculateJumpTarget(inst, instruction->get_pc());
                    if (isInstructionAddressMisaligned(state, target)) {
                        instruction->set_trap(0, target);
                        unit.is_jump = false;
                        unit.jump_target = 0;
                        LOGT(EXECUTE, "JAL misaligned trap: pc=0x%" PRIx64 " target=0x%" PRIx64,
                             instruction->get_pc(), target);
                        break;
                    }

                    unit.result = instruction->get_pc() + (inst.is_compressed ? 2 : 4);
                    unit.jump_target = target;
                    unit.is_jump = true;  // 无条件跳转总是需要改变PC
                    instruction->set_jump_info(true, unit.jump_target);

                    // 无条件跳转指令：记录预测错误但不在执行阶段刷新
                    LOGT(EXECUTE, "unconditional jump, target=0x%" PRIx64 " (pc=0x%" PRIx64 "), flush at commit",
                         unit.jump_target, instruction->get_pc());

                    // 注意：不在执行阶段刷新，让指令正常完成并提交
                    // 流水线刷新将在提交阶段进行
                    state.recordBranchMispredict();  // 统计预测错误
                }
                break;

            default:
                unit.has_exception = true;
                unit.exception_msg = "unsupported instruction type";
                LOGW(EXECUTE, "unsupported instruction type: %d", static_cast<int>(inst.type));
                break;
        }

    } catch (const SimulatorException& e) {
        unit.has_exception = true;
        unit.exception_msg = e.what();
    }
}

} // namespace riscv
