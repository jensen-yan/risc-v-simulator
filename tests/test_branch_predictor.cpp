#include <gtest/gtest.h>

#include "cpu/ooo/branch_predictor.h"

namespace riscv {

namespace {

DecodedInstruction makeBranchInst(int32_t imm = 8) {
    DecodedInstruction branch{};
    branch.opcode = Opcode::BRANCH;
    branch.imm = imm;
    return branch;
}

BranchPredictor::BranchMeta makeBranchMeta(uint16_t ghr_before,
                                           uint16_t local_history_before,
                                           uint16_t local_history_index,
                                           bool local_pred_taken,
                                           bool global_pred_taken) {
    BranchPredictor::BranchMeta meta{};
    meta.valid = true;
    meta.is_conditional_branch = true;
    meta.ghr_before = ghr_before;
    meta.local_history_before = local_history_before;
    meta.local_history_index = local_history_index;
    meta.local_pred_taken = local_pred_taken;
    meta.global_pred_taken = global_pred_taken;
    meta.chooser_use_global = false;
    return meta;
}

} // namespace

TEST(BranchPredictorTest, ConditionalPredictionCompatibility) {
    BranchPredictor predictor;
    const DecodedInstruction branch = makeBranchInst();

    const uint64_t pc = 0x100;
    const uint64_t fallthrough = pc + 4;
    const uint16_t local_idx = static_cast<uint16_t>((pc >> 1) & 0x3FFU);

    // 初始倾向 not-taken（兼容 bht_* 统计口径）。
    auto pred0 = predictor.predict(pc, branch, fallthrough);
    EXPECT_TRUE(pred0.bht_used);
    EXPECT_FALSE(pred0.bht_pred_taken);
    EXPECT_EQ(pred0.next_pc, fallthrough);
    EXPECT_TRUE(pred0.branch_meta.valid);

    // 在固定上下文(ghr=0)下训练 global=taken，并让 chooser 偏向 global。
    auto train_taken = makeBranchMeta(/*ghr_before=*/0, /*local_history_before=*/0,
                                      /*local_history_index=*/local_idx,
                                      /*local_pred=*/false, /*global_pred=*/true);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &train_taken);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &train_taken);

    auto force_ghr0 = makeBranchMeta(/*ghr_before=*/0, 0, local_idx, false, false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0); // push(0,0)=0

    auto pred1 = predictor.predict(pc, branch, fallthrough);
    EXPECT_TRUE(pred1.bht_pred_taken);
    EXPECT_EQ(pred1.next_pc, pc + 8);

    // 同一上下文继续训练 global=not-taken，保持 chooser 偏向 global。
    auto train_not_taken = makeBranchMeta(/*ghr_before=*/0, /*local_history_before=*/0,
                                          /*local_history_index=*/local_idx,
                                          /*local_pred=*/true, /*global_pred=*/false);
    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough, &train_not_taken);
    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough, &train_not_taken);
    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough, &train_not_taken);

    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0); // push(0,0)=0

    auto pred2 = predictor.predict(pc, branch, fallthrough);
    EXPECT_FALSE(pred2.bht_pred_taken);
    EXPECT_EQ(pred2.next_pc, fallthrough);
}

TEST(BranchPredictorTest, BtbHitAndTagCheck) {
    BranchPredictor predictor;

    DecodedInstruction jalr{};
    jalr.opcode = Opcode::JALR;

    const uint64_t pc = 0x200;
    const uint64_t fallthrough = pc + 4;

    auto miss = predictor.predict(pc, jalr, fallthrough);
    EXPECT_TRUE(miss.btb_used);
    EXPECT_FALSE(miss.btb_hit);
    EXPECT_EQ(miss.next_pc, fallthrough);

    predictor.update(pc, jalr, /*actual_taken=*/true, /*actual_target=*/0x800);

    auto hit = predictor.predict(pc, jalr, fallthrough);
    EXPECT_TRUE(hit.btb_used);
    EXPECT_TRUE(hit.btb_hit);
    EXPECT_EQ(hit.next_pc, 0x800);

    // 不同PC不应误命中
    auto other = predictor.predict(pc + 4, jalr, pc + 8);
    EXPECT_FALSE(other.btb_hit);
}

TEST(BranchPredictorTest, GShareUsesDifferentHistoryContexts) {
    BranchPredictor predictor;
    const DecodedInstruction branch = makeBranchInst();
    const uint64_t pc = 0x180;
    const uint64_t fallthrough = pc + 4;

    // 仅训练 ghr=0 对应的 global PHT 项为 taken。
    auto ghr0_meta = makeBranchMeta(/*ghr_before=*/0, /*local_history_before=*/0,
                                    /*local_history_index=*/0, /*local_pred=*/false, /*global_pred=*/false);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &ghr0_meta);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &ghr0_meta);

    // 强制 speculative GHR 到 0，检查该上下文下 global 分量预测为 taken。
    auto force_ghr0 = makeBranchMeta(/*ghr_before=*/0, 0, 0, false, false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0); // push(0,0)=0
    auto pred_ghr0 = predictor.predict(pc, branch, fallthrough);
    EXPECT_EQ(pred_ghr0.branch_meta.ghr_before, 0u);
    EXPECT_TRUE(pred_ghr0.branch_meta.global_pred_taken);
    EXPECT_EQ(pred_ghr0.branch_meta.local_pht_index, 0u);
    EXPECT_EQ(pred_ghr0.branch_meta.global_pht_index, 0xC0u);

    // 强制 speculative GHR 到 1，检查同 PC 不同 GHR 的 global 分量预测不同。
    auto force_ghr1 = makeBranchMeta(/*ghr_before=*/0, 0, 0, false, false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/true, &force_ghr1); // push(0,1)=1
    auto pred_ghr1 = predictor.predict(pc, branch, fallthrough);
    EXPECT_EQ(pred_ghr1.branch_meta.ghr_before, 1u);
    EXPECT_FALSE(pred_ghr1.branch_meta.global_pred_taken);
    EXPECT_NE(pred_ghr0.branch_meta.global_pht_index, pred_ghr1.branch_meta.global_pht_index);
    EXPECT_LE(pred_ghr1.branch_meta.chooser_counter_before, 3u);
    EXPECT_LE(pred_ghr1.branch_meta.local_counter_before, 3u);
    EXPECT_LE(pred_ghr1.branch_meta.global_counter_before, 3u);
}

TEST(BranchPredictorTest, TournamentChooserConvergesToGlobal) {
    BranchPredictor predictor;
    const DecodedInstruction branch = makeBranchInst();
    const uint64_t pc = 0x1C0;
    const uint64_t fallthrough = pc + 4;

    // 初始 chooser 为 weak-global。
    auto force_ghr0 = makeBranchMeta(/*ghr_before=*/0, 0, 0, false, false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0); // push(0,0)=0
    auto before = predictor.predict(pc, branch, fallthrough);
    EXPECT_TRUE(before.branch_meta.chooser_use_global);

    // 构造“local错误、global正确”的训练，推动 chooser 向 global 收敛。
    auto train_meta = makeBranchMeta(/*ghr_before=*/0, /*local_history_before=*/0,
                                     /*local_history_index=*/0, /*local_pred=*/false, /*global_pred=*/true);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &train_meta);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &train_meta);

    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0); // 保持查询上下文稳定
    auto after = predictor.predict(pc, branch, fallthrough);
    EXPECT_TRUE(after.branch_meta.chooser_use_global);
}

TEST(BranchPredictorTest, TournamentChooserWeakLocalSelectsLocal) {
    BranchPredictor predictor;
    const DecodedInstruction branch = makeBranchInst();
    const uint64_t pc = 0x1E0;
    const uint64_t fallthrough = pc + 4;

    auto force_ghr0 = makeBranchMeta(/*ghr_before=*/0, /*local_history_before=*/0,
                                     /*local_history_index=*/0, /*local_pred=*/false, /*global_pred=*/false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0);

    auto train_local = makeBranchMeta(/*ghr_before=*/0, /*local_history_before=*/0,
                                      /*local_history_index=*/0, /*local_pred=*/true, /*global_pred=*/false);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &train_local);

    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0);
    auto pred = predictor.predict(pc, branch, fallthrough);
    EXPECT_FALSE(pred.branch_meta.chooser_use_global);
    EXPECT_TRUE(pred.bht_pred_taken);
    EXPECT_EQ(pred.next_pc, pc + 8);
}

TEST(BranchPredictorTest, SaturatingCountersDoNotUnderflowOrOverflow) {
    BranchPredictor predictor;
    const DecodedInstruction branch = makeBranchInst();
    const uint64_t pc = 0x240;
    const uint64_t fallthrough = pc + 4;
    const uint16_t local_idx = static_cast<uint16_t>((pc >> 1) & 0x3FFU);

    auto force_ghr0 = makeBranchMeta(/*ghr_before=*/0, /*local_history_before=*/0,
                                     /*local_history_index=*/local_idx,
                                     /*local_pred=*/false, /*global_pred=*/false);

    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0);
    auto pred0 = predictor.predict(pc, branch, fallthrough);
    EXPECT_FALSE(pred0.bht_pred_taken);
    EXPECT_EQ(pred0.branch_meta.local_counter_before, 1u);
    EXPECT_EQ(pred0.branch_meta.global_counter_before, 1u);

    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough, &pred0.branch_meta);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0);
    auto pred1 = predictor.predict(pc, branch, fallthrough);
    EXPECT_FALSE(pred1.bht_pred_taken);
    EXPECT_EQ(pred1.branch_meta.local_counter_before, 0u);
    EXPECT_EQ(pred1.branch_meta.global_counter_before, 0u);

    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough, &pred1.branch_meta);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0);
    auto pred2 = predictor.predict(pc, branch, fallthrough);
    EXPECT_FALSE(pred2.bht_pred_taken);
    EXPECT_EQ(pred2.branch_meta.local_counter_before, 0u);
    EXPECT_EQ(pred2.branch_meta.global_counter_before, 0u);

    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &pred2.branch_meta);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &pred2.branch_meta);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &pred2.branch_meta);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &pred2.branch_meta);

    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0);
    auto pred3 = predictor.predict(pc, branch, fallthrough);
    EXPECT_TRUE(pred3.branch_meta.local_counter_before <= 3u);
    EXPECT_TRUE(pred3.branch_meta.global_counter_before <= 3u);
}

TEST(BranchPredictorTest, RecoverAndFlushResetSpeculativeHistory) {
    BranchPredictor predictor;
    const DecodedInstruction branch = makeBranchInst();
    const uint64_t pc = 0x200;
    const uint64_t fallthrough = pc + 4;

    // 从 GHR=0 取一条条件分支预测元数据。
    auto force_ghr0 = makeBranchMeta(/*ghr_before=*/0, 0, 0, false, false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/false, &force_ghr0); // push(0,0)=0
    auto pred = predictor.predict(pc, branch, fallthrough);
    ASSERT_TRUE(pred.branch_meta.valid);
    EXPECT_EQ(pred.branch_meta.ghr_before, 0u);

    // 按真实结果提交该分支（taken）。
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8, &pred.branch_meta);

    // 人为污染 speculative GHR 到 7：push(3,1)=7。
    auto pollute = makeBranchMeta(/*ghr_before=*/3, 0, 0, false, false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/true, &pollute);

    // 对真实误预测执行 recover，应回到该分支正确历史（push(0,1)=1）。
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/true, &pred.branch_meta);
    auto after_recover = predictor.predict(pc, branch, fallthrough);
    EXPECT_EQ(after_recover.branch_meta.ghr_before, 1u);

    // 再污染一次 speculative GHR 后，非分支 flush 应丢弃投机历史并回到 committed GHR(=1)。
    auto pollute2 = makeBranchMeta(/*ghr_before=*/7, 0, 0, false, false);
    predictor.recover_after_branch_mispredict(pc, /*actual_taken=*/true, &pollute2);
    predictor.on_pipeline_flush();
    auto after_flush = predictor.predict(pc, branch, fallthrough);
    EXPECT_EQ(after_flush.branch_meta.ghr_before, 1u);
}

TEST(BranchPredictorTest, LocalHistoryLearnsEightTakenThenExitPattern) {
    BranchPredictor predictor;
    const DecodedInstruction branch = makeBranchInst();
    const uint64_t pc = 0x280;
    const uint64_t fallthrough = pc + 4;
    const uint64_t taken_target = pc + 8;

    auto executeBranch = [&](bool actual_taken) {
        const auto pred = predictor.predict(pc, branch, fallthrough);
        predictor.update(pc, branch, actual_taken, actual_taken ? taken_target : fallthrough, &pred.branch_meta);
        if (pred.bht_pred_taken != actual_taken) {
            predictor.recover_after_branch_mispredict(pc, actual_taken, &pred.branch_meta);
        }
        return pred;
    };

    // 训练多个周期的 8T + 1N 模式，让 local history 为退出相位建立独立项。
    for (int round = 0; round < 6; ++round) {
        for (int i = 0; i < 8; ++i) {
            executeBranch(true);
        }
        executeBranch(false);
    }

    for (int i = 0; i < 8; ++i) {
        executeBranch(true);
    }

    const auto exit_pred = predictor.predict(pc, branch, fallthrough);
    EXPECT_EQ(exit_pred.branch_meta.local_history_before, 0xFFu);
    EXPECT_FALSE(exit_pred.branch_meta.local_pred_taken)
        << "8-bit local history 应能把 8T + 1N 的退出相位学成 not-taken";
}

} // namespace riscv
