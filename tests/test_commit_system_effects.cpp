#include <gtest/gtest.h>

#include "core/csr_utils.h"
#include "cpu/ooo/commit_system_effects.h"

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    return decoded;
}

DecodedInstruction makeCsrWriteInstruction(uint32_t csr_addr) {
    auto decoded = makeInstruction(Opcode::SYSTEM);
    decoded.funct3 = static_cast<Funct3>(0b001);
    decoded.imm = static_cast<int32_t>(csr_addr);
    return decoded;
}

} // namespace

TEST(CommitSystemEffectsTest, CommitsCsrWriteWithoutStoppingCommit) {
    CPUState state;
    auto inst = create_dynamic_inst(makeCsrWriteInstruction(csr::kMepc), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_src1_ready(true, 0x1234);
    csr::write(state.csr_registers, csr::kMepc, 0xAA);

    const auto result = CommitSystemEffects::apply(state, inst);

    EXPECT_TRUE(result.applied);
    EXPECT_FALSE(result.should_stop_commit);
    EXPECT_FALSE(result.has_flush_summary);
    EXPECT_EQ(csr::read(state.csr_registers, csr::kMepc), 0x1234u);
}

TEST(CommitSystemEffectsTest, EcallWithTrapVectorEntersMachineTrap) {
    CPUState state;
    auto decoded = makeInstruction(Opcode::SYSTEM);
    decoded.funct3 = Funct3::ECALL_EBREAK;
    decoded.imm = SystemInst::ECALL;
    auto inst = create_dynamic_inst(decoded, 0x100, 1);
    ASSERT_NE(inst, nullptr);
    csr::write(state.csr_registers, csr::kMtvec, 0x800);

    const auto result = CommitSystemEffects::apply(state, inst);

    EXPECT_TRUE(result.applied);
    EXPECT_TRUE(result.should_stop_commit);
    EXPECT_TRUE(result.has_flush_summary);
    EXPECT_EQ(result.flush_reason, OooRecovery::Reason::Trap);
    EXPECT_TRUE(result.has_redirect_pc);
    EXPECT_EQ(result.redirect_pc, 0x800u);
    EXPECT_EQ(state.pc, 0x800u);
    EXPECT_EQ(csr::read(state.csr_registers, csr::kMepc), 0x100u);
    EXPECT_EQ(csr::read(state.csr_registers, csr::kMcause), csr::kMachineEcallCause);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PIPELINE_FLUSH_TRAP), 1u);
}

TEST(CommitSystemEffectsTest, FenceIRedirectsToFallthroughAndFlushesIcachePath) {
    CPUState state;
    state.reservation_valid = true;
    state.reservation_addr = 0x80;
    auto decoded = makeInstruction(Opcode::MISC_MEM);
    decoded.funct3 = static_cast<Funct3>(0b001);
    auto inst = create_dynamic_inst(decoded, 0x200, 1);
    ASSERT_NE(inst, nullptr);

    const auto result = CommitSystemEffects::apply(state, inst);

    EXPECT_TRUE(result.applied);
    EXPECT_TRUE(result.should_stop_commit);
    EXPECT_TRUE(result.has_flush_summary);
    EXPECT_EQ(result.flush_reason, OooRecovery::Reason::FenceI);
    EXPECT_TRUE(result.has_redirect_pc);
    EXPECT_EQ(result.redirect_pc, 0x204u);
    EXPECT_EQ(state.pc, 0x204u);
    EXPECT_FALSE(state.reservation_valid);
    EXPECT_EQ(state.reservation_addr, 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PIPELINE_FLUSH_FENCEI), 1u);
}

TEST(CommitSystemEffectsTest, IgnoresNonSystemInstruction) {
    CPUState state;
    auto inst = create_dynamic_inst(makeInstruction(Opcode::OP_IMM), 0x100, 1);
    ASSERT_NE(inst, nullptr);

    const auto result = CommitSystemEffects::apply(state, inst);

    EXPECT_FALSE(result.applied);
    EXPECT_FALSE(result.should_stop_commit);
    EXPECT_FALSE(result.has_flush_summary);
}

} // namespace riscv
