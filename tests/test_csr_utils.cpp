#include <gtest/gtest.h>

#include "core/csr_utils.h"

namespace riscv {

TEST(CsrUtilsTest, WriteFflagsKeepsFcsrAliasConsistent) {
    csr::CsrFile csr_file{};
    csr_file.fill(0);
    csr_file[csr::kFcsr] = 0xA0;

    csr::write(csr_file, csr::kFflags, 0x3A);

    EXPECT_EQ(csr_file[csr::kFflags], 0x1A);
    EXPECT_EQ(csr_file[csr::kFcsr], 0xBA);
    EXPECT_EQ(csr::read(csr_file, csr::kFflags), 0x1A);
}

TEST(CsrUtilsTest, WriteFrmKeepsFcsrAliasConsistent) {
    csr::CsrFile csr_file{};
    csr_file.fill(0);
    csr_file[csr::kFcsr] = 0x1F;

    csr::write(csr_file, csr::kFrm, 0xF);

    EXPECT_EQ(csr_file[csr::kFrm], 0x7);
    EXPECT_EQ(csr_file[csr::kFcsr], 0xFF);
    EXPECT_EQ(csr::read(csr_file, csr::kFrm), 0x7);
}

TEST(CsrUtilsTest, WriteFcsrUpdatesFflagsAndFrmViews) {
    csr::CsrFile csr_file{};
    csr_file.fill(0);

    csr::write(csr_file, csr::kFcsr, 0xAB);

    EXPECT_EQ(csr_file[csr::kFcsr], 0xAB);
    EXPECT_EQ(csr_file[csr::kFflags], 0x0B);
    EXPECT_EQ(csr_file[csr::kFrm], 0x5);
    EXPECT_EQ(csr::read(csr_file, csr::kFflags), 0x0B);
    EXPECT_EQ(csr::read(csr_file, csr::kFrm), 0x5);
}

TEST(CsrUtilsTest, EnterMachineTrapWritesCauseStateAndReturnsAlignedVectorBase) {
    csr::CsrFile csr_file{};
    csr_file.fill(0);
    csr_file[csr::kMtvec] = 0x101;

    const uint64_t next_pc = csr::enterMachineTrap(
        csr_file, 0x88, csr::kBreakpointCause, 0x99);

    EXPECT_EQ(csr_file[csr::kMepc], 0x88);
    EXPECT_EQ(csr_file[csr::kMcause], csr::kBreakpointCause);
    EXPECT_EQ(csr_file[csr::kMtval], 0x99);
    EXPECT_EQ(next_pc, 0x100);
}

}  // namespace riscv
