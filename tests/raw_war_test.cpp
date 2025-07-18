#include <gtest/gtest.h>
#include "cpu/ooo/register_rename.h"
#include "core/decoder.h"
#include "core/decoder.h"

using namespace riscv;

class RAWWARTest : public ::testing::Test {
protected:
    RegisterRenameUnit rename_unit;
    Decoder decoder;
    
    void SetUp() override {
        // 每个测试前的初始化
    }
    
    DecodedInstruction createInstruction(InstructionType type, uint8_t rd, uint8_t rs1, uint8_t rs2 = 0, int32_t imm = 0) {
        DecodedInstruction inst;
        inst.type = type;
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.rs2 = rs2;
        inst.imm = imm;
        return inst;
    }
};

// 测试自依赖寄存器问题：addi tp,tp,1
TEST_F(RAWWARTest, SelfDependencyTest) {
    // 设置初始状态
    rename_unit.update_architecture_register(4, 0);  // tp = 0
    
    // 创建测试指令：addi tp,tp,1
    DecodedInstruction inst = createInstruction(InstructionType::I_TYPE, 4, 4, 0, 1);
    
    // 执行重命名
    auto result = rename_unit.rename_instruction(inst);
    EXPECT_TRUE(result.success);
    
    // 验证自依赖处理
    // 源寄存器应该使用旧的物理寄存器
    // 目标寄存器应该使用新的物理寄存器
    EXPECT_NE(result.dest_reg, result.src1_reg) 
        << "自依赖指令中，源和目标应该使用不同的物理寄存器";
    
    // 模拟执行完成
    rename_unit.update_physical_register(result.dest_reg, 1, 1);
    rename_unit.commit_instruction(4, result.dest_reg);
    
    // 验证最终结果
    EXPECT_EQ(rename_unit.get_physical_register_value(result.dest_reg), 1u);
}

// 测试循环中的自依赖问题
TEST_F(RAWWARTest, LoopSelfDependencyTest) {
    // 设置初始状态
    rename_unit.update_architecture_register(4, 0);  // tp = 0
    
    // 模拟两轮循环的addi tp,tp,1
    for (int i = 0; i < 2; i++) {
        DecodedInstruction inst = createInstruction(InstructionType::I_TYPE, 4, 4, 0, 1);
        
        auto result = rename_unit.rename_instruction(inst);
        EXPECT_TRUE(result.success);
        
        // 关键验证：每次循环都应该正确处理自依赖
        EXPECT_NE(result.dest_reg, result.src1_reg) 
            << "第" << (i + 1) << "次循环中自依赖处理失败";
        
        // 模拟执行：新值 = 旧值 + 1
        uint32_t old_value = rename_unit.get_physical_register_value(result.src1_reg);
        rename_unit.update_physical_register(result.dest_reg, old_value + 1, 1);
        rename_unit.commit_instruction(4, result.dest_reg);
    }
    
    // 验证最终结果
    // 经过两次加1操作，tp应该为2
    auto final_mapping = rename_unit.rename_instruction(createInstruction(InstructionType::I_TYPE, 4, 4, 0, 0));
    EXPECT_EQ(rename_unit.get_physical_register_value(final_mapping.src1_reg), 2u);
}

// 测试更复杂的RAW/WAR依赖链
TEST_F(RAWWARTest, ComplexDependencyChainTest) {
    // 设置初始状态
    rename_unit.update_architecture_register(1, 10);  // ra = 10
    rename_unit.update_architecture_register(2, 5);   // sp = 5
    
    // 依赖链：
    // 1. add a4, ra, sp    -> a4 = 15 (RAW: ra, sp -> a4)
    // 2. addi tp, a4, 3   -> tp = 18 (RAW: a4 -> tp)
    // 3. addi a4, a4, 2   -> a4 = 17 (RAW/WAR: a4 -> a4)
    
    // 第1条指令
    DecodedInstruction add_inst = createInstruction(InstructionType::R_TYPE, 14, 1, 2);
    auto result1 = rename_unit.rename_instruction(add_inst);
    EXPECT_TRUE(result1.success);
    rename_unit.update_physical_register(result1.dest_reg, 15, 1);
    rename_unit.commit_instruction(14, result1.dest_reg);
    
    // 第2条指令
    DecodedInstruction addi1_inst = createInstruction(InstructionType::I_TYPE, 4, 14, 0, 3);
    auto result2 = rename_unit.rename_instruction(addi1_inst);
    EXPECT_TRUE(result2.success);
    EXPECT_EQ(result2.src1_reg, result1.dest_reg);  // 应该使用第1条指令的结果
    rename_unit.update_physical_register(result2.dest_reg, 18, 1);
    rename_unit.commit_instruction(4, result2.dest_reg);
    
    // 第3条指令：关键的自依赖测试
    DecodedInstruction addi2_inst = createInstruction(InstructionType::I_TYPE, 14, 14, 0, 2);
    auto result3 = rename_unit.rename_instruction(addi2_inst);
    EXPECT_TRUE(result3.success);
    
    // 关键验证：源寄存器应该使用第1条指令的结果，而不是新的寄存器
    EXPECT_EQ(result3.src1_reg, result1.dest_reg) 
        << "自依赖指令的源寄存器应该使用正确的旧值";
    
    // 验证新的目标寄存器与源寄存器不同
    EXPECT_NE(result3.dest_reg, result3.src1_reg);
    
    // 模拟执行
    rename_unit.update_physical_register(result3.dest_reg, 17, 1);
    rename_unit.commit_instruction(14, result3.dest_reg);
    
    // 验证最终结果
    auto a4_mapping = rename_unit.rename_instruction(createInstruction(InstructionType::I_TYPE, 14, 14, 0, 0));
    auto tp_mapping = rename_unit.rename_instruction(createInstruction(InstructionType::I_TYPE, 4, 4, 0, 0));
    
    EXPECT_EQ(rename_unit.get_physical_register_value(a4_mapping.src1_reg), 17u);
    EXPECT_EQ(rename_unit.get_physical_register_value(tp_mapping.src1_reg), 18u);
}

// 测试边界情况：寄存器0的自依赖
TEST_F(RAWWARTest, ZeroRegisterSelfDependencyTest) {
    // x0的自依赖应该被忽略
    DecodedInstruction inst = createInstruction(InstructionType::I_TYPE, 0, 0, 0, 1);
    
    auto result = rename_unit.rename_instruction(inst);
    EXPECT_TRUE(result.success);
    
    // x0应该始终映射到物理寄存器0
    EXPECT_EQ(result.dest_reg, 0u);
    EXPECT_EQ(result.src1_reg, 0u);
    
    // 值应该始终为0
    EXPECT_EQ(rename_unit.get_physical_register_value(0), 0u);
}
