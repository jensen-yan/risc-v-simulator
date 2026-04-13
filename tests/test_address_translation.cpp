#include <gtest/gtest.h>

#include "core/memory.h"
#include "system/address_translation.h"
#include "system/privilege_state.h"

using namespace riscv;

namespace {

constexpr uint64_t kSv39Mode = 8ULL;
constexpr Address kRootPageTable = 0x1000;
constexpr Address kLevel1PageTable = 0x2000;
constexpr Address kLevel0PageTable = 0x3000;
constexpr Address kVirtualAddress = 0x1000;
constexpr Address kPhysicalAddress = 0x4000;

uint64_t makePte(Address target, uint64_t flags) {
    return ((target >> 12) << 10) | flags;
}

} // namespace

class AddressTranslationTest : public ::testing::Test {
protected:
    void SetUp() override {
        memory = std::make_shared<Memory>(64 * 1024);
        translation = std::make_unique<AddressTranslation>(memory, &privilegeState);
    }

    void installSv39Mapping4K(Address virtualAddress, Address physicalAddress) {
        const uint64_t vpn2 = (virtualAddress >> 30) & 0x1FF;
        const uint64_t vpn1 = (virtualAddress >> 21) & 0x1FF;
        const uint64_t vpn0 = (virtualAddress >> 12) & 0x1FF;

        memory->write64(kRootPageTable + vpn2 * 8, makePte(kLevel1PageTable, 0x1));
        memory->write64(kLevel1PageTable + vpn1 * 8, makePte(kLevel0PageTable, 0x1));
        memory->write64(kLevel0PageTable + vpn0 * 8, makePte(physicalAddress, 0x7));

        privilegeState.setSatp((kSv39Mode << 60) | (kRootPageTable >> 12));
    }

    std::shared_ptr<Memory> memory;
    PrivilegeState privilegeState;
    std::unique_ptr<AddressTranslation> translation;
};

TEST_F(AddressTranslationTest, BareModeBypassesTranslation) {
    privilegeState.setSatp(0);

    const TranslationResult result = translation->translateInstructionAddress(0x1234);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, 0x1234);
}

TEST_F(AddressTranslationTest, Sv39WalkResolves4KLeaf) {
    installSv39Mapping4K(kVirtualAddress, kPhysicalAddress);

    const TranslationResult result = translation->translateLoadAddress(kVirtualAddress, 8);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, kPhysicalAddress);
}
