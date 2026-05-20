#include <gtest/gtest.h>
#include "cpu/ooo/stages/execute_stage.h"
#include <memory>

namespace riscv {

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
    state.store_buffer = std::make_unique<StoreBuffer>();

    ExecuteStage::Context context(state);
    execute_stage_->execute(context);

    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCH_SLOTS),
              OOOPipelineConfig::DISPATCH_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::DISPATCH_UTILIZED_SLOTS), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_TOTAL),
              OOOPipelineConfig::DISPATCH_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_EXECUTED), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::TOPDOWN_SLOTS_FRONTEND_EMPTY),
              OOOPipelineConfig::DISPATCH_WIDTH);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_EXECUTE_NO_READY), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::STALL_EXECUTE_FRONTEND_STARVED), 1u);
}

} // namespace riscv
