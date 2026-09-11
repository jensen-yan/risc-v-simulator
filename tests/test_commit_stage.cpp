#include <gtest/gtest.h>

#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/stages/commit_stage.h"
#include "cpu/ooo/store_forwarding_buffer.h"
#include "core/memory.h"
#include "system/pipeline_tracer.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <string>

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

DecodedInstruction makeFenceIInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::MISC_MEM;
    decoded.funct3 = static_cast<Funct3>(0b001);
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
        state.store_queue = std::make_unique<StoreQueue>();
    state.store_forwarding_buffer = std::make_unique<StoreForwardingBuffer>();
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

TEST_F(CommitStageContextTest, ReportsPreFlushCountsForSerializingFenceI) {
    // 回归：FENCE.I 的 full-pipeline flush 由 CommitSystemEffects::apply 在内部触发，
    // flush 摘要必须在 flush 之前快照，否则 ROB/fetch-buffer 占用会被清零。
    state.fetch_buffer.push(FetchedInstruction{});
    state.fetch_buffer.push(FetchedInstruction{});

    auto fencei = state.reorder_buffer->allocate_entry(makeFenceIInstruction(), 0x100, 1);
    auto younger_one = state.reorder_buffer->allocate_entry(makeAddiInstruction(1, 0, 1), 0x104, 2);
    auto younger_two = state.reorder_buffer->allocate_entry(makeAddiInstruction(2, 0, 2), 0x108, 3);
    ASSERT_NE(fencei, nullptr);
    ASSERT_NE(younger_one, nullptr);
    ASSERT_NE(younger_two, nullptr);

    state.reorder_buffer->update_entry(fencei, 0);
    state.reorder_buffer->update_entry(younger_one, 1);
    state.reorder_buffer->update_entry(younger_two, 2);

    PipelineTracer tracer;
    state.pipeline_tracer = &tracer;

    CommitStage::Context context(state);
    commit_stage.execute(context);

    // FENCE.I 退休并触发 flush，younger 指令被清空。
    EXPECT_EQ(fencei->get_status(), DynamicInst::Status::RETIRED);
    EXPECT_TRUE(state.reorder_buffer->is_empty());
    EXPECT_EQ(state.pc, 0x104u);

    const std::string report_path = ::testing::TempDir() + "commit_stage_fencei_flush.txt";
    ASSERT_TRUE(tracer.generateText(report_path));
    std::ifstream report(report_path);
    ASSERT_TRUE(report.is_open());
    const std::string report_text((std::istreambuf_iterator<char>(report)),
                                  std::istreambuf_iterator<char>());

    // 提交 FENCE.I 前 ROB 中还有 2 条 younger 指令，fetch buffer 有 2 条待丢弃条目。
    EXPECT_NE(report_text.find("reason=fencei rob=2 fb=2"), std::string::npos)
        << "flush 摘要未反映 flush 前的占用，报告内容:\n" << report_text;
}

} // namespace riscv
