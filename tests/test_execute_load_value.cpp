#include <gtest/gtest.h>

#include "cpu/ooo/execute_load_value.h"

namespace riscv {

TEST(ExecuteLoadValueTest, SignExtendsByteHalfAndWordLoads) {
    DecodedInstruction inst;
    inst.opcode = Opcode::LOAD;
    inst.is_signed_load = true;

    EXPECT_EQ(ExecuteLoadValue::format(inst, 1, 0x80), 0xFFFFFFFFFFFFFF80ULL);
    EXPECT_EQ(ExecuteLoadValue::format(inst, 2, 0x8000), 0xFFFFFFFFFFFF8000ULL);
    EXPECT_EQ(ExecuteLoadValue::format(inst, 4, 0x80000000), 0xFFFFFFFF80000000ULL);
}

TEST(ExecuteLoadValueTest, ZeroExtendsUnsignedLoads) {
    DecodedInstruction inst;
    inst.opcode = Opcode::LOAD;
    inst.is_signed_load = false;

    EXPECT_EQ(ExecuteLoadValue::format(inst, 1, 0xFFFFFFFFFFFFFFFFULL), 0xFFULL);
    EXPECT_EQ(ExecuteLoadValue::format(inst, 2, 0xFFFFFFFFFFFFFFFFULL), 0xFFFFULL);
    EXPECT_EQ(ExecuteLoadValue::format(inst, 4, 0xFFFFFFFFFFFFFFFFULL), 0xFFFFFFFFULL);
}

TEST(ExecuteLoadValueTest, KeepsEightByteLoadsUnchanged) {
    DecodedInstruction inst;
    inst.opcode = Opcode::LOAD;
    inst.is_signed_load = true;

    EXPECT_EQ(ExecuteLoadValue::format(inst, 8, 0xFEDCBA9876543210ULL), 0xFEDCBA9876543210ULL);
}

TEST(ExecuteLoadValueTest, NanBoxesSinglePrecisionFloatLoads) {
    DecodedInstruction inst;
    inst.opcode = Opcode::LOAD_FP;

    EXPECT_EQ(ExecuteLoadValue::format(inst, 4, 0x12345678ULL), 0xFFFFFFFF12345678ULL);
}

TEST(ExecuteLoadValueTest, KeepsDoublePrecisionFloatLoadsUnchanged) {
    DecodedInstruction inst;
    inst.opcode = Opcode::LOAD_FP;

    EXPECT_EQ(ExecuteLoadValue::format(inst, 8, 0x0123456789ABCDEFULL), 0x0123456789ABCDEFULL);
}

} // namespace riscv
