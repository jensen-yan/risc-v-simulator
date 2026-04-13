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
constexpr uint64_t kPteV = 1ULL << 0;
constexpr uint64_t kPteR = 1ULL << 1;
constexpr uint64_t kPteW = 1ULL << 2;
constexpr uint64_t kPteX = 1ULL << 3;
constexpr uint64_t kPteU = 1ULL << 4;
constexpr uint64_t kPteA = 1ULL << 6;
constexpr uint64_t kPteD = 1ULL << 7;

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

    void installSv39Mapping4K(Address virtualAddress, Address physicalAddress, uint64_t leafFlags) {
        const uint64_t vpn2 = (virtualAddress >> 30) & 0x1FF;
        const uint64_t vpn1 = (virtualAddress >> 21) & 0x1FF;
        const uint64_t vpn0 = (virtualAddress >> 12) & 0x1FF;

        memory->write64(kRootPageTable + vpn2 * 8, makePte(kLevel1PageTable, kPteV));
        memory->write64(kLevel1PageTable + vpn1 * 8, makePte(kLevel0PageTable, kPteV));
        memory->write64(kLevel0PageTable + vpn0 * 8, makePte(physicalAddress, leafFlags));

        privilegeState.setSatp((kSv39Mode << 60) | (kRootPageTable >> 12));
    }

    void installSv39RootOnly(Address rootPageTable) {
        privilegeState.setSatp((kSv39Mode << 60) | (rootPageTable >> 12));
    }

    std::shared_ptr<Memory> memory;
    PrivilegeState privilegeState;
    std::unique_ptr<AddressTranslation> translation;
};

TEST_F(AddressTranslationTest, BareModeBypassesTranslation) {
    privilegeState.setSatp(0);

    const TranslationResult result = translation->translateInstructionAddress(0x1234, 2);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, 0x1234);
    EXPECT_EQ(result.failure_reason, TranslationFailureReason::None);
}

TEST_F(AddressTranslationTest, Sv39WalkResolves4KLeaf) {
    privilegeState.setMode(PrivilegeMode::SUPERVISOR);
    installSv39Mapping4K(kVirtualAddress, kPhysicalAddress, kPteV | kPteR);

    const TranslationResult result = translation->translateLoadAddress(kVirtualAddress, 8);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, kPhysicalAddress);
    EXPECT_EQ(result.failure_reason, TranslationFailureReason::None);

    const uint64_t updatedPte = memory->read64(kLevel0PageTable + 8);
    EXPECT_NE(updatedPte & kPteA, 0U);
    EXPECT_EQ(updatedPte & kPteD, 0U);
}

TEST_F(AddressTranslationTest, Sv39StoreSetsAccessedAndDirtyBits) {
    privilegeState.setMode(PrivilegeMode::SUPERVISOR);
    installSv39Mapping4K(kVirtualAddress, kPhysicalAddress, kPteV | kPteR | kPteW);

    const TranslationResult result = translation->translateStoreAddress(kVirtualAddress, 8);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, kPhysicalAddress);

    const uint64_t updatedPte = memory->read64(kLevel0PageTable + 8);
    EXPECT_NE(updatedPte & kPteA, 0U);
    EXPECT_NE(updatedPte & kPteD, 0U);
}

TEST_F(AddressTranslationTest, Sv39RejectsUserAccessToSupervisorPage) {
    privilegeState.setMode(PrivilegeMode::USER);
    installSv39Mapping4K(kVirtualAddress, kPhysicalAddress, kPteV | kPteR | kPteX | kPteA | kPteD);

    const TranslationResult result = translation->translateInstructionAddress(kVirtualAddress, 4);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_reason, TranslationFailureReason::PermissionDenied);
}

TEST_F(AddressTranslationTest, NullPrivilegeStateReturnsAccessFault) {
    AddressTranslation translationWithoutPrivilege(memory, nullptr);

    const TranslationResult result = translationWithoutPrivilege.translateLoadAddress(kVirtualAddress, 8);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_reason, TranslationFailureReason::AccessFault);
}

TEST_F(AddressTranslationTest, OutOfBoundsPageTableAccessReturnsAccessFault) {
    privilegeState.setMode(PrivilegeMode::SUPERVISOR);
    installSv39RootOnly(0x10000);

    const TranslationResult result = translation->translateLoadAddress(kVirtualAddress, 8);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_reason, TranslationFailureReason::AccessFault);
}

TEST_F(AddressTranslationTest, Sv39RejectsUnsupportedLargeLeafPage) {
    privilegeState.setMode(PrivilegeMode::SUPERVISOR);
    memory->write64(kRootPageTable, makePte(kLevel1PageTable, kPteV));
    memory->write64(kLevel1PageTable, makePte(kPhysicalAddress, kPteV | kPteR | kPteA));
    privilegeState.setSatp((kSv39Mode << 60) | (kRootPageTable >> 12));

    const TranslationResult result = translation->translateLoadAddress(kVirtualAddress, 8);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_reason, TranslationFailureReason::UnsupportedPageSize);
}

TEST_F(AddressTranslationTest, Sv39FetchAlsoSetsAccessedBit) {
    privilegeState.setMode(PrivilegeMode::SUPERVISOR);
    installSv39Mapping4K(kVirtualAddress, kPhysicalAddress, kPteV | kPteX);

    const TranslationResult result = translation->translateInstructionAddress(kVirtualAddress, 4);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, kPhysicalAddress);

    const uint64_t updatedPte = memory->read64(kLevel0PageTable + 8);
    EXPECT_NE(updatedPte & kPteA, 0U);
    EXPECT_EQ(updatedPte & kPteD, 0U);
}
