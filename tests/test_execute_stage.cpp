#include <gtest/gtest.h>
#include "cpu/ooo/stages/execute_stage.h"
#include "core/memory.h"
#include "system/address_translation.h"
#include "system/privilege_state.h"
#include <memory>

namespace riscv {

namespace {

DecodedInstruction makeAddiInstruction(RegNum rd, int32_t imm) {
    DecodedInstruction decoded;
    decoded.type = InstructionType::I_TYPE;
    decoded.opcode = Opcode::OP_IMM;
    decoded.rd = rd;
    decoded.rs1 = 0;
    decoded.rs2 = 0;
    decoded.imm = imm;
    decoded.execution_cycles = 1;
    return decoded;
}

DecodedInstruction makeStoreInstruction() {
    DecodedInstruction decoded;
    decoded.type = InstructionType::S_TYPE;
    decoded.opcode = Opcode::STORE;
    decoded.rs1 = 1;
    decoded.rs2 = 2;
    decoded.imm = 8;
    decoded.memory_access_size = 4;
    decoded.execution_cycles = 1;
    return decoded;
}

size_t countBusyCompletionPendingAluUnits(const CPUState& state) {
    size_t count = 0;
    for (const auto& unit : state.alu_units) {
        if (unit.busy && unit.completion_pending) {
            ++count;
        }
    }
    return count;
}

size_t countBusyStoreUnits(const CPUState& state) {
    size_t count = 0;
    for (const auto& unit : state.store_units) {
        if (unit.busy) {
            ++count;
        }
    }
    return count;
}

} // namespace

/**
 * ExecuteStage模块单元测试
 * 当前覆盖率：28.9% -> 目标：65%+
 * 
 * 注意：ExecuteStage 仍包含 LSU/recovery 等宽依赖；当前先覆盖 Context 入口的
 * no-ready 行为，后续随深模块拆分继续补行为测试。
 */
class ExecuteStageTest : public ::testing::Test {
protected:
    std::unique_ptr<ExecuteStage> execute_stage_;
    
    void SetUp() override {
        execute_stage_ = std::make_unique<ExecuteStage>();
    }
    
    void TearDown() override {
        execute_stage_.reset();
    }
};

// ========== 基本初始化测试 ==========

TEST_F(ExecuteStageTest, BasicInitialization) {
    EXPECT_NE(execute_stage_, nullptr) << "ExecuteStage应该被正确创建";
    EXPECT_STREQ(execute_stage_->get_stage_name(), "EXECUTE") << "阶段名称应该是EXECUTE";
}

// ========== 基本接口测试 ==========

TEST_F(ExecuteStageTest, BasicInterfaceTest) {
    // 验证ExecuteStage保留了PipelineStage观测接口
    PipelineStage* stage = execute_stage_.get();
    EXPECT_NE(stage, nullptr) << "ExecuteStage应该实现PipelineStage接口";
    
    // 验证阶段名称
    EXPECT_STREQ(stage->get_stage_name(), "EXECUTE") << "阶段名称应该正确";
}

TEST_F(ExecuteStageTest, EmptyReservationStationRecordsFrontendStarvedThroughContext) {
    CPUState state;
    state.reservation_station = std::make_unique<ReservationStation>();
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.store_queue = std::make_unique<StoreQueue>();
    state.store_forwarding_buffer = std::make_unique<StoreForwardingBuffer>();

    ExecuteStage::Context context(state);
    execute_stage_->execute(context);

    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUE_SLOTS),
              OOOPipelineConfig::ISSUE_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::ISSUE_UTILIZED_SLOTS), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_TOTAL),
              OOOPipelineConfig::ISSUE_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_EXECUTED), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_FRONTEND_EMPTY),
              OOOPipelineConfig::ISSUE_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_EXECUTE_NO_READY), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_EXECUTE_FRONTEND_STARVED), 1u);
}

TEST_F(ExecuteStageTest, CompletionBackpressureKeepsExecutionUnitBusy) {
    CPUState state;
    state.reservation_station = std::make_unique<ReservationStation>();
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.store_queue = std::make_unique<StoreQueue>();
    state.store_forwarding_buffer = std::make_unique<StoreForwardingBuffer>();

    for (size_t i = 0; i < OOOPipelineConfig::COMPLETION_WIDTH + 1; ++i) {
        auto inst = state.reorder_buffer->allocate_entry(
            makeAddiInstruction(static_cast<RegNum>(i + 1), static_cast<int32_t>(i + 1)),
            0x100 + i * 4,
            i + 1);
        ASSERT_NE(inst, nullptr);
        inst->set_physical_dest_kind(RegisterFileKind::Integer);
        inst->set_physical_dest(static_cast<PhysRegNum>(32 + i));
        inst->set_src1_ready(true, 0);
        inst->set_src2_ready(true, 0);
        inst->set_src3_ready(true, 0);

        auto dispatch_result = state.reservation_station->dispatch_instruction(inst);
        ASSERT_TRUE(dispatch_result.success);
    }

    ExecuteStage::Context context(state);
    execute_stage_->execute(context);

    size_t busy_alu_units = 0;
    for (const auto& unit : state.alu_units) {
        if (unit.busy) {
            ++busy_alu_units;
        }
    }
    ASSERT_EQ(busy_alu_units, OOOPipelineConfig::COMPLETION_WIDTH + 1);
    EXPECT_TRUE(state.completion_fabric.empty());

    state.completion_fabric.beginCycle();
    execute_stage_->execute(context);

    EXPECT_EQ(state.completion_fabric.size(), OOOPipelineConfig::COMPLETION_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::COMPLETION_ACCEPTED),
              OOOPipelineConfig::COMPLETION_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_COMPLETION_PORT_BUSY), 1u);
    EXPECT_EQ(countBusyCompletionPendingAluUnits(state), 1u);
}

TEST_F(ExecuteStageTest, StoreAddressCanExecuteBeforeStoreDataIsReady) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    state.privilege_state = std::make_unique<PrivilegeState>();
    state.address_translation =
        std::make_unique<AddressTranslation>(state.memory, state.privilege_state.get());
    state.reservation_station = std::make_unique<ReservationStation>();
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.store_queue = std::make_unique<StoreQueue>();
    state.store_forwarding_buffer = std::make_unique<StoreForwardingBuffer>();

    auto store = state.reorder_buffer->allocate_entry(makeStoreInstruction(), 0x100, 1);
    ASSERT_NE(store, nullptr);
    store->set_src1_ready(true, 0x200);
    store->set_src3_ready(true, 0);
    ASSERT_TRUE(state.reservation_station->dispatch_instruction(store).success);

    ExecuteStage::Context context(state);
    execute_stage_->execute(context);

    const auto* entry = state.store_queue->findEntryForInstruction(store);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->address_ready);
    EXPECT_EQ(entry->address, 0x208u);
    EXPECT_FALSE(entry->data_ready);
    EXPECT_EQ(store->get_status(), DynamicInst::Status::EXECUTING);
    EXPECT_TRUE(state.completion_fabric.empty());
    EXPECT_EQ(countBusyStoreUnits(state), 1u);

    execute_stage_->execute(context);

    EXPECT_EQ(store->get_status(), DynamicInst::Status::DISPATCHED);
    EXPECT_FALSE(store->is_completed());
    EXPECT_EQ(state.reservation_station->get_entry(store->get_rs_entry()), store);
    EXPECT_TRUE(state.completion_fabric.empty());
    EXPECT_EQ(countBusyStoreUnits(state), 0u);
}

} // namespace riscv
