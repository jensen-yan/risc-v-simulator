#include <gtest/gtest.h>

#include "cpu/ooo/commit_control_flow_effects.h"

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    return decoded;
}

} // namespace

TEST(CommitControlFlowEffectsTest, IgnoresNonControlFlowInstruction) {
    CPUState state;
    auto inst = create_dynamic_inst(makeInstruction(Opcode::OP_IMM), 0x100, 1);
    ASSERT_NE(inst, nullptr);

    const auto result = CommitControlFlowEffects::apply(state, inst);

    EXPECT_FALSE(result.is_control_flow);
    EXPECT_FALSE(result.needs_redirect_flush);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_CONTROL_CORRECT), 0u);
}

TEST(CommitControlFlowEffectsTest, RecordsCorrectTakenBranchProfile) {
    CPUState state;
    auto branch = create_dynamic_inst(makeInstruction(Opcode::BRANCH), 0x100, 1);
    ASSERT_NE(branch, nullptr);
    branch->set_jump_info(true, 0x140);
    branch->set_predicted_next_pc(0x140);

    BranchPredictor::BranchMeta meta;
    meta.valid = true;
    meta.local_pred_taken = true;
    meta.global_pred_taken = false;
    meta.chooser_use_global = false;
    branch->set_branch_predict_meta(meta);

    const auto result = CommitControlFlowEffects::apply(state, branch);

    EXPECT_TRUE(result.is_control_flow);
    EXPECT_FALSE(result.needs_redirect_flush);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_CONTROL_CORRECT), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CONTROL_REDIRECTS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_BHT_CORRECT), 1u);
    const auto& profile = state.branch_profiles.at(0x100);
    EXPECT_EQ(profile.executions, 1u);
    EXPECT_EQ(profile.taken, 1u);
    EXPECT_EQ(profile.predicted_taken, 1u);
    EXPECT_EQ(profile.local_correct, 1u);
    EXPECT_EQ(profile.global_incorrect, 1u);
    EXPECT_EQ(profile.chooser_selected_local, 1u);
}

TEST(CommitControlFlowEffectsTest, RequestsRedirectForBranchMispredict) {
    CPUState state;
    auto branch = create_dynamic_inst(makeInstruction(Opcode::BRANCH), 0x100, 1);
    ASSERT_NE(branch, nullptr);
    branch->set_jump_info(true, 0x180);
    branch->set_predicted_next_pc(0x104);

    const auto result = CommitControlFlowEffects::apply(state, branch);

    EXPECT_TRUE(result.is_control_flow);
    EXPECT_TRUE(result.needs_redirect_flush);
    EXPECT_EQ(result.redirect_pc, 0x180u);
    EXPECT_EQ(result.flush_reason, OooRecovery::Reason::BranchMispredict);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_CONTROL_INCORRECT), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_BHT_INCORRECT), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::BRANCH_MISPREDICTS), 1u);
    EXPECT_EQ(state.branch_profiles.at(0x100).mispredicts, 1u);
}

TEST(CommitControlFlowEffectsTest, RecordsEarlyRecoveredBranchWithoutSecondFlush) {
    CPUState state;
    auto branch = create_dynamic_inst(makeInstruction(Opcode::BRANCH), 0x100, 1);
    ASSERT_NE(branch, nullptr);
    branch->set_jump_info(true, 0x180);
    branch->set_predicted_next_pc(0x104);
    branch->mark_control_recovered_early();

    const auto result = CommitControlFlowEffects::apply(state, branch);

    EXPECT_TRUE(result.is_control_flow);
    EXPECT_FALSE(result.needs_redirect_flush);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_CONTROL_INCORRECT), 1u);
    EXPECT_EQ(state.branch_profiles.at(0x100).mispredicts, 1u);
}

TEST(CommitControlFlowEffectsTest, RecordsJalrReturnLikeFallthroughMispredict) {
    CPUState state;
    auto decoded = makeInstruction(Opcode::JALR);
    decoded.rd = 0;
    decoded.rs1 = 1;
    decoded.imm = 0;
    auto jalr = create_dynamic_inst(decoded, 0x200, 1);
    ASSERT_NE(jalr, nullptr);
    jalr->set_jump_info(true, 0x280);
    jalr->set_predicted_next_pc(0x204);

    const auto result = CommitControlFlowEffects::apply(state, jalr);

    EXPECT_TRUE(result.is_control_flow);
    EXPECT_TRUE(result.needs_redirect_flush);
    EXPECT_EQ(result.redirect_pc, 0x280u);
    EXPECT_EQ(result.flush_reason, OooRecovery::Reason::UnconditionalRedirect);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::UNCONDITIONAL_REDIRECTS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_JALR_MISPREDICTS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_JALR_RETURN_MISPREDICTS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::PREDICTOR_JALR_FALLTHROUGH_MISPREDICTS), 1u);
    const auto& profile = state.jalr_profiles.at(0x200);
    EXPECT_EQ(profile.executions, 1u);
    EXPECT_EQ(profile.mispredicts, 1u);
    EXPECT_EQ(profile.predicted_fallthrough, 1u);
    EXPECT_EQ(profile.return_like, 1u);
}

} // namespace riscv
