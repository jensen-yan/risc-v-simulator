#include <gtest/gtest.h>

#include "cpu/ooo/execute_host_comm_access.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeInstruction(Opcode opcode = Opcode::LOAD) {
    DecodedInstruction decoded;
    decoded.opcode = opcode;
    return decoded;
}

CPUState makeStateWithHostCommMemory() {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    state.memory->setHostCommAddresses(0x100, 0x140);
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    return state;
}

} // namespace

TEST(ExecuteHostCommAccessTest, DetectsOverlappingTohostAndFromhostRanges) {
    auto state = makeStateWithHostCommMemory();

    EXPECT_TRUE(ExecuteHostCommAccess::isAccess(state, 0x100, 8));
    EXPECT_TRUE(ExecuteHostCommAccess::isAccess(state, 0x104, 4));
    EXPECT_TRUE(ExecuteHostCommAccess::isAccess(state, 0x140, 8));
    EXPECT_TRUE(ExecuteHostCommAccess::isAccess(state, 0x13c, 8));
    EXPECT_FALSE(ExecuteHostCommAccess::isAccess(state, 0x120, 4));
    EXPECT_FALSE(ExecuteHostCommAccess::isAccess(state, 0x100, 0));
}

TEST(ExecuteHostCommAccessTest, NonHostCommAccessDoesNotSerialize) {
    auto state = makeStateWithHostCommMemory();
    auto inst = state.reorder_buffer->allocate_entry(makeInstruction(), 0x200, 1);

    EXPECT_FALSE(ExecuteHostCommAccess::mustSerialize(state, inst, 0x120, 4));
}

TEST(ExecuteHostCommAccessTest, RobHeadHostCommAccessDoesNotSerialize) {
    auto state = makeStateWithHostCommMemory();
    auto head = state.reorder_buffer->allocate_entry(makeInstruction(), 0x200, 1);
    ASSERT_NE(head, nullptr);

    EXPECT_FALSE(ExecuteHostCommAccess::mustSerialize(state, head, 0x100, 8));
}

TEST(ExecuteHostCommAccessTest, YoungerHostCommAccessMustSerialize) {
    auto state = makeStateWithHostCommMemory();
    ASSERT_NE(state.reorder_buffer->allocate_entry(makeInstruction(Opcode::STORE), 0x200, 1), nullptr);
    auto younger = state.reorder_buffer->allocate_entry(makeInstruction(), 0x204, 2);
    ASSERT_NE(younger, nullptr);

    EXPECT_TRUE(ExecuteHostCommAccess::mustSerialize(state, younger, 0x140, 8));
}

TEST(ExecuteHostCommAccessTest, MissingInstructionOrRobDoesNotSerialize) {
    auto state = makeStateWithHostCommMemory();

    EXPECT_FALSE(ExecuteHostCommAccess::mustSerialize(state, nullptr, 0x100, 8));

    auto inst = state.reorder_buffer->allocate_entry(makeInstruction(), 0x200, 1);
    state.reorder_buffer.reset();
    EXPECT_FALSE(ExecuteHostCommAccess::mustSerialize(state, inst, 0x100, 8));
}

} // namespace riscv
