#include <gtest/gtest.h>

#include "core/csr_utils.h"
#include "cpu/ooo/commit_register_effects.h"
#include "cpu/ooo/register_rename.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode, RegNum rd = 0) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    decoded.rd = rd;
    return decoded;
}

} // namespace

TEST(CommitRegisterEffectsTest, CommitsIntegerRegisterAndRenameState) {
    CPUState state;
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    auto inst = create_dynamic_inst(makeInstruction(Opcode::OP_IMM, 3), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_physical_dest_kind(RegisterFileKind::Integer);
    inst->set_physical_dest(32);
    inst->set_result(0x1234);

    const auto result = CommitRegisterEffects::apply(state, inst);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.applied);
    EXPECT_EQ(state.arch_registers[3], 0x1234u);
}

TEST(CommitRegisterEffectsTest, KeepsIntegerZeroRegisterUnchanged) {
    CPUState state;
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    auto inst = create_dynamic_inst(makeInstruction(Opcode::OP_IMM, 0), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_result(0xFFFF);

    const auto result = CommitRegisterEffects::apply(state, inst);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.applied);
    EXPECT_EQ(state.arch_registers[0], 0u);
}

TEST(CommitRegisterEffectsTest, CommitsFloatingPointLoadToFpRegister) {
    CPUState state;
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    auto inst = create_dynamic_inst(makeInstruction(Opcode::LOAD_FP, 4), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_physical_dest_kind(RegisterFileKind::FloatingPoint);
    inst->set_physical_dest(40);
    inst->set_result(0x3F800000);

    const auto result = CommitRegisterEffects::apply(state, inst);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.applied);
    EXPECT_EQ(state.arch_fp_registers[4], 0x3F800000u);
}

TEST(CommitRegisterEffectsTest, CommitsFpResultToIntegerRegisterAndFflags) {
    CPUState state;
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    auto inst = create_dynamic_inst(makeInstruction(Opcode::OP_FP, 5), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_physical_dest_kind(RegisterFileKind::Integer);
    inst->set_physical_dest(41);

    DynamicInst::FpExecuteInfo fp_info;
    fp_info.value = 0x77;
    fp_info.write_int_reg = true;
    fp_info.fflags = 0b00101;
    inst->set_fp_execute_info(fp_info);
    csr::write(state.csr_registers, csr::kFflags, 0b00010);

    const auto result = CommitRegisterEffects::apply(state, inst);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.applied);
    EXPECT_EQ(state.arch_registers[5], 0x77u);
    EXPECT_EQ(csr::read(state.csr_registers, csr::kFflags), 0b00111u);
}

TEST(CommitRegisterEffectsTest, ReportsMissingFpExecuteInfo) {
    CPUState state;
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    auto inst = create_dynamic_inst(makeInstruction(Opcode::OP_FP, 1), 0x100, 1);
    ASSERT_NE(inst, nullptr);

    const auto result = CommitRegisterEffects::apply(state, inst);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.applied);
    EXPECT_EQ(result.error_message, "missing fp execute info at commit");
}

} // namespace riscv
