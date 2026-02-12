#include <gtest/gtest.h>

#include "cpu/ooo/branch_predictor.h"

namespace riscv {

TEST(BranchPredictorTest, BhtTwoBitSaturation) {
    BranchPredictor predictor;

    DecodedInstruction branch{};
    branch.opcode = Opcode::BRANCH;
    branch.imm = 8;

    const uint64_t pc = 0x100;
    const uint64_t fallthrough = pc + 4;

    // 初始为WN(weakly not-taken)
    auto pred0 = predictor.predict(pc, branch, fallthrough);
    EXPECT_TRUE(pred0.bht_used);
    EXPECT_FALSE(pred0.bht_pred_taken);
    EXPECT_EQ(pred0.next_pc, fallthrough);

    // 连续两次taken训练后，应倾向taken
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8);
    predictor.update(pc, branch, /*actual_taken=*/true, pc + 8);

    auto pred1 = predictor.predict(pc, branch, fallthrough);
    EXPECT_TRUE(pred1.bht_pred_taken);
    EXPECT_EQ(pred1.next_pc, pc + 8);

    // 连续多次not-taken训练后，应回到not-taken并饱和
    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough);
    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough);
    predictor.update(pc, branch, /*actual_taken=*/false, fallthrough);

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

} // namespace riscv

