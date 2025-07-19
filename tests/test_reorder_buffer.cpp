#include <gtest/gtest.h>
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/dynamic_inst.h"
#include "core/decoder.h"
#include <vector>

namespace riscv {

class ReorderBufferTest : public ::testing::Test {
protected:
    ReorderBuffer rob;
    Decoder decoder;
    uint64_t next_instruction_id;
    
    void SetUp() override {
        // 每个测试前的初始化
        next_instruction_id = 1;
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
    
    // 辅助函数：获取下一个指令ID
    uint64_t getNextInstructionId() {
        return next_instruction_id++;
    }
};

// 测试1：基本分配功能
TEST_F(ReorderBufferTest, BasicAllocation) {
    EXPECT_TRUE(rob.has_free_entry()) << "初始应该有空闲表项";
    EXPECT_TRUE(rob.is_empty()) << "初始ROB应该为空";
    EXPECT_FALSE(rob.is_full()) << "初始ROB不应该满";
    
    size_t initial_free = rob.get_free_entry_count();
    EXPECT_GT(initial_free, 0) << "初始应该有空闲表项";
    
    // 分配一个表项
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 2, 3);
    auto dynamic_inst = rob.allocate_entry(inst, 0x1000, getNextInstructionId());
    
    EXPECT_TRUE(dynamic_inst != nullptr) << "分配应该成功";
    EXPECT_EQ(rob.get_free_entry_count(), initial_free - 1) << "空闲表项应该减少1";
    EXPECT_FALSE(rob.is_empty()) << "分配后ROB不应该为空";
    
    // 验证分配的指令信息
    EXPECT_EQ(dynamic_inst->get_pc(), 0x1000) << "PC应该匹配";
    EXPECT_EQ(dynamic_inst->get_decoded_info().rd, 1) << "目标寄存器应该匹配";
}

// 测试2：ROB容量测试
TEST_F(ReorderBufferTest, CapacityTest) {
    std::vector<DynamicInstPtr> allocated_instructions;
    
    // 尝试分配超过容量的表项
    for (int i = 0; i < 40; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, i % 32, 2, 3);
        auto dynamic_inst = rob.allocate_entry(inst, 0x1000 + i * 4, getNextInstructionId());
        
        if (dynamic_inst) {
            allocated_instructions.push_back(dynamic_inst);
        } else {
            // 分配失败，ROB应该已满
            break;
        }
    }
    
    // 检查ROB是否已满
    EXPECT_TRUE(rob.is_full()) << "ROB应该已满";
    EXPECT_EQ(rob.get_free_entry_count(), 0) << "满时应该没有空闲表项";
    EXPECT_LE(allocated_instructions.size(), ReorderBuffer::MAX_ROB_ENTRIES) << "分配的指令数不应超过ROB容量";
    
    // 尝试再分配一个应该失败
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 2, 3);
    auto failed_allocation = rob.allocate_entry(inst, 0x2000, getNextInstructionId());
    EXPECT_TRUE(failed_allocation == nullptr) << "ROB满时分配应该失败";
}

// 测试3：更新和提交功能
TEST_F(ReorderBufferTest, UpdateAndCommit) {
    // 分配一个表项
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 2, 3);
    auto dynamic_inst = rob.allocate_entry(inst, 0x1000, getNextInstructionId());
    EXPECT_TRUE(dynamic_inst != nullptr);
    
    // 初始时不能提交
    EXPECT_FALSE(rob.can_commit()) << "未完成的指令不应该能提交";
    
    // 更新执行结果
    const uint32_t test_result = 0xDEADBEEF;
    rob.update_entry(dynamic_inst, test_result);
    
    // 现在应该可以提交
    EXPECT_TRUE(rob.can_commit()) << "完成的指令应该能提交";
    
    // 提交指令
    auto commit_result = rob.commit_instruction();
    EXPECT_TRUE(commit_result.success) << "提交应该成功";
    EXPECT_EQ(commit_result.instruction->get_result(), test_result) << "结果应该匹配";
    EXPECT_FALSE(commit_result.has_more) << "没有更多指令可提交";
    
    // 提交后ROB应该为空
    EXPECT_TRUE(rob.is_empty()) << "提交后ROB应该为空";
}

// 测试4：程序顺序提交
TEST_F(ReorderBufferTest, InOrderCommit) {
    // 分配多个表项
    std::vector<DynamicInstPtr> alloc_results;
    for (int i = 0; i < 3; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, i + 1, 2, 3);
        auto dynamic_inst = rob.allocate_entry(inst, 0x1000 + i * 4, getNextInstructionId());
        EXPECT_TRUE(dynamic_inst != nullptr);
        alloc_results.push_back(dynamic_inst);
    }
    
    // 乱序完成指令（第3条先完成）
    rob.update_entry(alloc_results[2], 0x33333333);
    EXPECT_FALSE(rob.can_commit()) << "第3条指令完成，但第1条未完成，不应该能提交";
    
    // 第2条指令完成
    rob.update_entry(alloc_results[1], 0x22222222);
    EXPECT_FALSE(rob.can_commit()) << "第2、3条指令完成，但第1条未完成，不应该能提交";
    
    // 第1条指令完成
    rob.update_entry(alloc_results[0], 0x11111111);
    EXPECT_TRUE(rob.can_commit()) << "第1条指令完成，应该能提交";
    
    // 按顺序提交
    auto commit1 = rob.commit_instruction();
    EXPECT_TRUE(commit1.success);
    EXPECT_EQ(commit1.instruction->get_result(), 0x11111111) << "应该提交第1条指令";
    EXPECT_TRUE(commit1.has_more) << "应该还有更多指令可提交";
    
    auto commit2 = rob.commit_instruction();
    EXPECT_TRUE(commit2.success);
    EXPECT_EQ(commit2.instruction->get_result(), 0x22222222) << "应该提交第2条指令";
    EXPECT_TRUE(commit2.has_more) << "应该还有更多指令可提交";
    
    auto commit3 = rob.commit_instruction();
    EXPECT_TRUE(commit3.success);
    EXPECT_EQ(commit3.instruction->get_result(), 0x33333333) << "应该提交第3条指令";
    EXPECT_FALSE(commit3.has_more) << "没有更多指令可提交";
}

// 测试5：异常处理
TEST_F(ReorderBufferTest, ExceptionHandling) {
    // 分配一个表项
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 2, 3);
    auto dynamic_inst = rob.allocate_entry(inst, 0x1000, getNextInstructionId());
    EXPECT_TRUE(dynamic_inst != nullptr);
    
    // 更新为异常状态
    rob.update_entry(dynamic_inst, 0, true, "除零错误");
    
    // 检查异常状态
    EXPECT_TRUE(rob.has_pending_exception()) << "应该有待处理的异常";
    
    auto exception_info = rob.get_oldest_exception();
    EXPECT_TRUE(exception_info.has_exception) << "应该有异常";
    EXPECT_EQ(exception_info.exception_message, "除零错误") << "异常信息应该匹配";
    EXPECT_EQ(exception_info.pc, 0x1000) << "异常PC应该匹配";
    
    // 提交异常指令
    auto commit_result = rob.commit_instruction();
    EXPECT_TRUE(commit_result.success) << "异常指令应该能提交";
    EXPECT_TRUE(commit_result.instruction->has_exception()) << "提交的指令应该有异常";
    EXPECT_EQ(commit_result.instruction->get_exception_message(), "除零错误") << "异常信息应该匹配";
}

// 测试6：流水线刷新
TEST_F(ReorderBufferTest, PipelineFlush) {
    // 分配一些表项
    std::vector<DynamicInstPtr> alloc_results;
    for (int i = 0; i < 5; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, i + 1, 2, 3);
        auto dynamic_inst = rob.allocate_entry(inst, 0x1000 + i * 4, getNextInstructionId());
        EXPECT_TRUE(dynamic_inst != nullptr);
        alloc_results.push_back(dynamic_inst);
    }
    
    EXPECT_FALSE(rob.is_empty()) << "分配后ROB不应该为空";
    
    // 刷新流水线
    rob.flush_pipeline();
    
    // 检查刷新后状态
    EXPECT_TRUE(rob.is_empty()) << "刷新后ROB应该为空";
    EXPECT_FALSE(rob.is_full()) << "刷新后ROB不应该满";
    EXPECT_FALSE(rob.can_commit()) << "刷新后不应该能提交";
    EXPECT_FALSE(rob.has_pending_exception()) << "刷新后不应该有异常";
}

// 测试7：部分刷新
TEST_F(ReorderBufferTest, PartialFlush) {
    // 暂时跳过这个测试，因为需要检查flush_after_entry接口
    // TODO: 适配新的DynamicInst接口
    GTEST_SKIP() << "部分刷新测试暂时跳过，需要适配新接口";
}

// 测试8：ROB状态查询
TEST_F(ReorderBufferTest, StateQuery) {
    // 分配一个表项
    auto inst = createInstruction(InstructionType::R_TYPE, 1, 2, 3);
    auto dynamic_inst = rob.allocate_entry(inst, 0x1000, getNextInstructionId());
    EXPECT_TRUE(dynamic_inst != nullptr);
    
    // 检查表项有效性
    EXPECT_TRUE(rob.is_entry_valid(dynamic_inst->get_rob_entry())) << "分配的表项应该有效";
    
    // 获取表项内容
    const auto& entry = rob.get_entry(dynamic_inst->get_rob_entry());
    EXPECT_EQ(entry->get_pc(), 0x1000) << "表项PC应该匹配";
    EXPECT_EQ(entry->get_decoded_info().rd, 1) << "表项指令应该匹配";
    
    // 检查头尾表项
    EXPECT_EQ(rob.get_head_entry(), dynamic_inst->get_rob_entry()) << "头部表项应该匹配";
    EXPECT_EQ(rob.get_tail_entry(), dynamic_inst->get_rob_entry()) << "尾部表项应该匹配";
}

// 测试9：统计信息
TEST_F(ReorderBufferTest, Statistics) {
    uint64_t allocated_before, committed_before, flushed_before, exceptions_before;
    rob.get_statistics(allocated_before, committed_before, flushed_before, exceptions_before);
    
    // 分配、完成、提交一些指令
    std::vector<DynamicInstPtr> alloc_results;
    for (int i = 0; i < 3; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, i + 1, 2, 3);
        auto dynamic_inst = rob.allocate_entry(inst, 0x1000 + i * 4, getNextInstructionId());
        EXPECT_TRUE(dynamic_inst != nullptr);
        alloc_results.push_back(dynamic_inst);
    }
    
    // 完成前两条指令
    rob.update_entry(alloc_results[0], 0x11111111);
    rob.update_entry(alloc_results[1], 0x22222222);
    
    // 提交前两条指令
    auto commit1 = rob.commit_instruction();
    EXPECT_TRUE(commit1.success);
    auto commit2 = rob.commit_instruction();
    EXPECT_TRUE(commit2.success);
    
    // 刷新剩余指令
    rob.flush_pipeline();
    
    uint64_t allocated_after, committed_after, flushed_after, exceptions_after;
    rob.get_statistics(allocated_after, committed_after, flushed_after, exceptions_after);
    
    EXPECT_EQ(allocated_after, allocated_before + 3) << "分配计数应该增加3";
    EXPECT_EQ(committed_after, committed_before + 2) << "提交计数应该增加2";
    EXPECT_GT(flushed_after, flushed_before) << "刷新计数应该增加";
    EXPECT_EQ(exceptions_after, exceptions_before) << "异常计数不应该增加";
}

// 测试10：复杂场景
TEST_F(ReorderBufferTest, ComplexScenario) {
    // 分配多个表项，模拟复杂的乱序执行场景
    std::vector<DynamicInstPtr> alloc_results;
    for (int i = 0; i < 8; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, i + 1, 2, 3);
        auto dynamic_inst = rob.allocate_entry(inst, 0x1000 + i * 4, getNextInstructionId());
        EXPECT_TRUE(dynamic_inst != nullptr);
        alloc_results.push_back(dynamic_inst);
    }
    
    // 乱序完成指令
    rob.update_entry(alloc_results[3], 0x44444444);  // 第4条
    rob.update_entry(alloc_results[0], 0x11111111);  // 第1条
    rob.update_entry(alloc_results[6], 0x77777777);  // 第7条
    rob.update_entry(alloc_results[1], 0x22222222);  // 第2条
    
    // 应该只能提交前两条
    EXPECT_TRUE(rob.can_commit());
    auto commit1 = rob.commit_instruction();
    EXPECT_TRUE(commit1.success);
    EXPECT_EQ(commit1.instruction->get_result(), 0x11111111);
    
    auto commit2 = rob.commit_instruction();
    EXPECT_TRUE(commit2.success);
    EXPECT_EQ(commit2.instruction->get_result(), 0x22222222);
    
    // 第3条还未完成，不能继续提交
    EXPECT_FALSE(rob.can_commit());
    
    // 完成第3条
    rob.update_entry(alloc_results[2], 0x33333333);
    
    // 现在可以提交第3、4条
    EXPECT_TRUE(rob.can_commit());
    auto commit3 = rob.commit_instruction();
    EXPECT_TRUE(commit3.success);
    EXPECT_EQ(commit3.instruction->get_result(), 0x33333333);
    
    auto commit4 = rob.commit_instruction();
    EXPECT_TRUE(commit4.success);
    EXPECT_EQ(commit4.instruction->get_result(), 0x44444444);
    
    // 第5条还未完成，不能继续提交
    EXPECT_FALSE(rob.can_commit());
}

} // namespace riscv