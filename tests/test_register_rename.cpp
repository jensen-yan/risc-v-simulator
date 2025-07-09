#include <gtest/gtest.h>
#include "register_rename.h"
#include "decoder.h"
#include <vector>

namespace riscv {

class RegisterRenameTest : public ::testing::Test {
protected:
    RegisterRenameUnit rename_unit;
    Decoder decoder;
    
    void SetUp() override {
        // 每个测试前的初始化
    }
    
    void TearDown() override {
        // 每个测试后的清理
    }
    
    // 辅助函数：创建指令
    DecodedInstruction createInstruction(InstructionType type, uint8_t rd, uint8_t rs1, uint8_t rs2) {
        DecodedInstruction inst;
        inst.type = type;
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.rs2 = rs2;
        return inst;
    }
};

// 测试1：WAW冒险消除
TEST_F(RegisterRenameTest, WAWHazardElimination) {
    // 测试指令序列：
    // 1. ADD x1, x2, x3    # x1 = x2 + x3
    // 2. SUB x1, x4, x5    # x1 = x4 - x5 (WAW冒险)
    // 3. OR  x6, x1, x7    # x6 = x1 | x7 (应该使用第2条指令的结果)
    
    std::vector<DecodedInstruction> instructions = {
        createInstruction(InstructionType::R_TYPE, 1, 2, 3),  // ADD x1, x2, x3
        createInstruction(InstructionType::R_TYPE, 1, 4, 5),  // SUB x1, x4, x5
        createInstruction(InstructionType::R_TYPE, 6, 1, 7),  // OR x6, x1, x7
    };
    
    // 执行重命名
    std::vector<RegisterRenameUnit::RenameResult> results;
    for (const auto& inst : instructions) {
        auto result = rename_unit.rename_instruction(inst);
        EXPECT_TRUE(result.success) << "重命名应该成功";
        results.push_back(result);
    }
    
    // 验证WAW冒险消除
    EXPECT_NE(results[0].dest_reg, results[1].dest_reg) 
        << "两个写入x1的指令应该分配不同的物理寄存器";
    
    // 验证第3条指令读取的是第2条指令的结果
    EXPECT_EQ(results[2].src1_reg, results[1].dest_reg)
        << "第3条指令应该读取第2条指令的物理寄存器";
    
    // 验证第3条指令的源寄存器1还没准备好（因为第2条指令还没执行完）
    EXPECT_FALSE(results[2].src1_ready) 
        << "第3条指令的源寄存器1应该还没准备好";
}

// 测试2：WAR冒险消除
TEST_F(RegisterRenameTest, WARHazardElimination) {
    // 测试指令序列：
    // 1. ADD x1, x2, x3    # x1 = x2 + x3
    // 2. SUB x4, x1, x5    # x4 = x1 - x5 (读取x1)
    // 3. OR  x1, x6, x7    # x1 = x6 | x7 (WAR冒险，重写x1)
    
    std::vector<DecodedInstruction> instructions = {
        createInstruction(InstructionType::R_TYPE, 1, 2, 3),  // ADD x1, x2, x3
        createInstruction(InstructionType::R_TYPE, 4, 1, 5),  // SUB x4, x1, x5
        createInstruction(InstructionType::R_TYPE, 1, 6, 7),  // OR x1, x6, x7
    };
    
    // 执行第1条指令并模拟完成
    auto result1 = rename_unit.rename_instruction(instructions[0]);
    EXPECT_TRUE(result1.success);
    
    // 模拟第1条指令执行完成
    rename_unit.update_physical_register(result1.dest_reg, 0x12345678, 1);
    
    // 执行第2条指令
    auto result2 = rename_unit.rename_instruction(instructions[1]);
    EXPECT_TRUE(result2.success);
    
    // 验证第2条指令能正确读取第1条指令的结果
    EXPECT_EQ(result2.src1_reg, result1.dest_reg)
        << "第2条指令应该读取第1条指令的物理寄存器";
    EXPECT_TRUE(result2.src1_ready)
        << "第2条指令的源寄存器1应该准备好";
    EXPECT_EQ(result2.src1_value, 0x12345678)
        << "第2条指令应该读取到正确的值";
    
    // 执行第3条指令
    auto result3 = rename_unit.rename_instruction(instructions[2]);
    EXPECT_TRUE(result3.success);
    
    // 验证WAR冒险消除：第3条指令分配了新的物理寄存器
    EXPECT_NE(result3.dest_reg, result1.dest_reg)
        << "第3条指令应该分配新的物理寄存器给x1";
}

// 测试3：物理寄存器分配
TEST_F(RegisterRenameTest, PhysicalRegisterAllocation) {
    EXPECT_TRUE(rename_unit.has_free_register()) << "应该有空闲的物理寄存器";
    
    size_t initial_free = rename_unit.get_free_register_count();
    EXPECT_GT(initial_free, 0) << "初始应该有空闲寄存器";
    
    // 分配一个寄存器
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 2, 3);
    auto result = rename_unit.rename_instruction(inst);
    
    EXPECT_TRUE(result.success) << "分配应该成功";
    EXPECT_EQ(rename_unit.get_free_register_count(), initial_free - 1) 
        << "空闲寄存器应该减少1";
}

// 测试4：物理寄存器状态管理
TEST_F(RegisterRenameTest, PhysicalRegisterStateManagement) {
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 0, 0);
    auto result = rename_unit.rename_instruction(inst);
    
    EXPECT_TRUE(result.success);
    
    // 新分配的寄存器应该还没准备好
    EXPECT_FALSE(rename_unit.is_physical_register_ready(result.dest_reg))
        << "新分配的物理寄存器应该还没准备好";
    
    // 更新寄存器值
    const uint32_t test_value = 0xDEADBEEF;
    rename_unit.update_physical_register(result.dest_reg, test_value, 1);
    
    // 现在应该准备好了
    EXPECT_TRUE(rename_unit.is_physical_register_ready(result.dest_reg))
        << "更新后的物理寄存器应该准备好";
    
    EXPECT_EQ(rename_unit.get_physical_register_value(result.dest_reg), test_value)
        << "物理寄存器应该包含正确的值";
}

// 测试5：流水线刷新
TEST_F(RegisterRenameTest, PipelineFlush) {
    // 执行一些重命名操作
    std::vector<PhysRegNum> allocated_regs;
    for (int i = 1; i <= 5; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, i, 0, 0);
        auto result = rename_unit.rename_instruction(inst);
        EXPECT_TRUE(result.success);
        allocated_regs.push_back(result.dest_reg);
    }
    
    size_t free_before_flush = rename_unit.get_free_register_count();
    
    // 执行流水线刷新
    rename_unit.flush_pipeline();
    
    size_t free_after_flush = rename_unit.get_free_register_count();
    
    // 刷新后应该有更多空闲寄存器
    EXPECT_GT(free_after_flush, free_before_flush)
        << "流水线刷新后应该释放投机分配的寄存器";
}

// 测试6：指令提交
TEST_F(RegisterRenameTest, InstructionCommit) {
    // 重命名指令
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 2, 3);
    auto result = rename_unit.rename_instruction(inst);
    EXPECT_TRUE(result.success);
    
    // 更新物理寄存器
    const uint32_t test_value = 0xABCDEF00;
    rename_unit.update_physical_register(result.dest_reg, test_value, 1);
    
    size_t free_before_commit = rename_unit.get_free_register_count();
    
    // 提交指令
    rename_unit.commit_instruction(1, result.dest_reg);
    
    // 提交后，旧的架构寄存器应该被释放（如果它是额外分配的）
    // 但在我们的简单测试中，由于是第一次重命名，可能不会有变化
    // 这里主要测试函数不会崩溃
    size_t free_after_commit = rename_unit.get_free_register_count();
    EXPECT_GE(free_after_commit, free_before_commit)
        << "提交后空闲寄存器数量不应该减少";
}

// 测试7：x0寄存器特殊处理
TEST_F(RegisterRenameTest, ZeroRegisterHandling) {
    // x0寄存器不应该被重命名
    auto inst = createInstruction(InstructionType::R_TYPE, 0, 1, 2);
    auto result = rename_unit.rename_instruction(inst);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.dest_reg, 0) << "x0寄存器应该始终映射到物理寄存器0";
    
    // x0寄存器的值应该始终是0
    EXPECT_EQ(rename_unit.get_physical_register_value(0), 0)
        << "x0寄存器的值应该始终是0";
    
    // 尝试更新x0寄存器应该无效
    rename_unit.update_physical_register(0, 0xDEADBEEF, 1);
    EXPECT_EQ(rename_unit.get_physical_register_value(0), 0)
        << "x0寄存器的值应该保持为0";
}

// 测试8：统计信息
TEST_F(RegisterRenameTest, Statistics) {
    uint64_t renames_before, stalls_before;
    rename_unit.get_statistics(renames_before, stalls_before);
    
    // 执行几次重命名
    for (int i = 1; i <= 3; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, i, 0, 0);
        auto result = rename_unit.rename_instruction(inst);
        EXPECT_TRUE(result.success);
    }
    
    uint64_t renames_after, stalls_after;
    rename_unit.get_statistics(renames_after, stalls_after);
    
    EXPECT_EQ(renames_after, renames_before + 3)
        << "重命名次数应该增加3";
    EXPECT_EQ(stalls_after, stalls_before)
        << "没有资源冲突时，停顿次数不应该增加";
}

} // namespace riscv