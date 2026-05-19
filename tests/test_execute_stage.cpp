#include <gtest/gtest.h>
#include "cpu/ooo/stages/execute_stage.h"
#include <memory>

namespace riscv {

/**
 * ExecuteStage模块单元测试
 * 当前覆盖率：28.9% -> 目标：65%+
 * 
 * 注意：ExecuteStage 行为仍依赖完整 CPUState；当前只保留阶段观测面测试。
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

// 注意：更复杂的功能测试需要完整的CPU状态，这里暂时跳过
// 在实际的集成测试中会进行更全面的测试

} // namespace riscv
