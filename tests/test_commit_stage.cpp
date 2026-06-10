#include <gtest/gtest.h>

#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/store_buffer.h"
#include "core/memory.h"

#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeAddiInstruction(RegNum rd, RegNum rs1, int32_t imm) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::OP_IMM;
    decoded.rd = rd;
    decoded.rs1 = rs1;
    decoded.rs2 = 0;
    decoded.imm = imm;
    decoded.execution_cycles = 1;
    return decoded;
}

DecodedInstruction makeStoreInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::S_TYPE;
    decoded.opcode = Opcode::STORE;
    decoded.funct3 = Funct3::SW;
    decoded.memory_access_size = 4;
    decoded.execution_cycles = 1;
    return decoded;
}

void completeStore(DynamicInstPtr store, uint64_t address, uint64_t value) {
    ASSERT_NE(store, nullptr);
    auto& memory_info = store->get_memory_info();
    memory_info.is_memory_op = true;
    memory_info.is_store = true;
    memory_info.address_ready = true;
    memory_info.memory_address = address;
    memory_info.memory_value = value;
    memory_info.memory_size = 4;
    store->set_status(DynamicInst::Status::COMPLETED);
}

} // namespace

class CommitStageContextTest : public ::testing::Test {
protected:
    CPUState state;
    CommitStage commit_stage;

    void SetUp() override {
        state.reorder_buffer = std::make_unique<ReorderBuffer>();
        state.register_rename = std::make_unique<RegisterRenameUnit>();
        state.store_buffer = std::make_unique<StoreBuffer>();
        state.cycle_count = 17;
    }
};

TEST_F(CommitStageContextTest, EmptyRobSkipsCommitThroughNarrowContext) {
    CommitStage::Context context(state);
    commit_stage.execute(context);

    EXPECT_EQ(state.instruction_count, 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_SLOTS),
              OOOPipelineConfig::COMMIT_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_UTILIZED_SLOTS), 0u);
}

TEST_F(CommitStageContextTest, CommitsCompletedIntegerInstructionThroughNarrowContext) {
    auto inst = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 42), 0x100, 1);
    ASSERT_NE(inst, nullptr);
    inst->set_physical_dest_kind(RegisterFileKind::Integer);
    inst->set_physical_dest(32);
    state.reorder_buffer->update_entry(inst, 42);

    CommitStage::Context context(state);
    commit_stage.execute(context);

    EXPECT_EQ(inst->get_status(), DynamicInst::Status::RETIRED);
    EXPECT_EQ(inst->get_retire_cycle(), 17u);
    EXPECT_EQ(state.arch_registers[1], 42u);
    EXPECT_EQ(state.instruction_count, 1u);
    EXPECT_TRUE(state.reorder_buffer->is_empty());
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_SLOTS),
              OOOPipelineConfig::COMMIT_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::INSTRUCTIONS_RETIRED), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMMIT_UTILIZED_SLOTS), 1u);
}

TEST_F(CommitStageContextTest, StopsBeforeSecondStoreWhenStoreCommitPortBusy) {
    state.memory = std::make_shared<Memory>(4096);

    auto first_store = state.reorder_buffer->allocate_entry(makeStoreInstruction(), 0x100, 1);
    auto addi = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 42), 0x104, 2);
    auto second_store = state.reorder_buffer->allocate_entry(makeStoreInstruction(), 0x108, 3);
    ASSERT_NE(first_store, nullptr);
    ASSERT_NE(addi, nullptr);
    ASSERT_NE(second_store, nullptr);

    completeStore(first_store, 0x80, 0x11111111);
    addi->set_physical_dest_kind(RegisterFileKind::Integer);
    addi->set_physical_dest(32);
    state.reorder_buffer->update_entry(addi, 42);
    completeStore(second_store, 0x84, 0x22222222);

    CommitStage::Context context(state);
    commit_stage.execute(context);

    EXPECT_EQ(first_store->get_status(), DynamicInst::Status::RETIRED);
    EXPECT_EQ(addi->get_status(), DynamicInst::Status::RETIRED);
    EXPECT_EQ(second_store->get_status(), DynamicInst::Status::COMPLETED);
    EXPECT_EQ(state.reorder_buffer->get_head_entry(), second_store->get_rob_entry());
    EXPECT_EQ(state.instruction_count, 2u);
    EXPECT_EQ(state.arch_registers[1], 42u);
    EXPECT_EQ(state.memory->readWord(0x80), 0x11111111u);
    EXPECT_EQ(state.memory->readWord(0x84), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STORES_COMMITTED), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_COMMIT_STORE_PORT_BUSY), 1u);
}

} // namespace riscv
