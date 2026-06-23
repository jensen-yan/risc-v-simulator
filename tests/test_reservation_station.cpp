#include <gtest/gtest.h>
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/dynamic_inst.h"
#include "core/decoder.h"
#include "common/debug_types.h"
#include <vector>

namespace riscv {

class ReservationStationTest : public ::testing::Test {
protected:
    ReservationStation rs;
    Decoder decoder;
    uint64_t next_instruction_id;
    
    void SetUp() override {
        // 每个测试前的初始化
        next_instruction_id = 1;
        // 启用调试输出
        auto& debugManager = DebugManager::getInstance();

        // 设置回调函数
        debugManager.setCallback([](const DebugInfo& info) {
            std::cout << DebugFormatter::format(info) << std::endl;
        });
        
        // 启用相关分类
        debugManager.enableCategory("RS");
        debugManager.enableCategory("SYSTEM");
    }
    
    void TearDown() override {
        // 每个测试后的清理
    }
    
    // 辅助函数：创建指令
    DecodedInstruction createInstruction(InstructionType type, Opcode opcode, 
                                       uint8_t rd, uint8_t rs1, uint8_t rs2) {
        DecodedInstruction inst;
        inst.type = type;
        inst.opcode = opcode;
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.rs2 = rs2;
        return inst;
    }
    
    // 辅助函数：创建 DynamicInst 对象
    DynamicInstPtr createDynamicInst(const DecodedInstruction& inst, 
                                   PhysRegNum src1_reg, PhysRegNum src2_reg, 
                                   PhysRegNum dest_reg, bool src1_ready = true, 
                                   bool src2_ready = true, uint32_t pc = 0x1000,
                                   ROBEntry rob_entry = 1) {
        auto dynamic_inst = create_dynamic_inst(inst, pc, next_instruction_id++);
        // 设置寄存器重命名信息
        dynamic_inst->set_physical_dest(dest_reg);
        dynamic_inst->set_physical_src1(src1_reg);
        dynamic_inst->set_physical_src2(src2_reg);
        dynamic_inst->set_physical_dest_kind(RegisterFileKind::Integer);
        dynamic_inst->set_physical_src1_kind(RegisterFileKind::Integer);
        dynamic_inst->set_physical_src2_kind(RegisterFileKind::Integer);
        dynamic_inst->set_rob_entry(rob_entry);
        // 设置操作数状态
        if (src1_ready) {
            dynamic_inst->set_src1_ready(true, 0x12345678);
        }
        if (src2_ready) {
            dynamic_inst->set_src2_ready(true, 0x87654321);
        }
        return dynamic_inst;
    }
    
    // 辅助函数：获取下一个指令ID
    uint64_t getNextInstructionId() {
        return next_instruction_id++;
    }
};

// 测试2：保留站容量测试
TEST_F(ReservationStationTest, CapacityTest) {
    std::vector<ReservationStation::DispatchResult> results;
    const size_t initial_free = rs.get_free_entry_count();
    
    // 填满保留站
    for (size_t i = 0; i < initial_free + 8; ++i) {  // 尝试派发超过容量的指令
        auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, i % 32, 2, 3);
        auto dynamic_inst = createDynamicInst(
            inst, 32 + static_cast<int>(i), 33, 34 + static_cast<int>(i),
            true, true, 0x1000 + static_cast<uint32_t>(i) * 4, static_cast<int>(i) + 1);
        
        auto result = rs.dispatch_instruction(dynamic_inst);
        results.push_back(result);
        
        if (!result.success) {
            break;  // 保留站已满
        }
    }
    
    // 检查是否有派发失败的情况
    bool found_failure = false;
    for (const auto& result : results) {
        if (!result.success) {
            found_failure = true;
            EXPECT_FALSE(result.error_message.empty()) << "失败时应该有错误信息";
            break;
        }
    }
    
    EXPECT_TRUE(found_failure) << "当保留站满时应该派发失败";
    EXPECT_EQ(rs.get_free_entry_count(), 0) << "保留站满时应该没有空闲表项";
}

// 测试3：基本发射功能
TEST_F(ReservationStationTest, BasicIssue) {
    // 派发一个准备好的指令
    auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto dynamic_inst = createDynamicInst(inst, 32, 33, 34, true, true);  // 两个操作数都准备好
    
    auto dispatch_result = rs.dispatch_instruction(dynamic_inst);
    EXPECT_TRUE(dispatch_result.success);
    
    // 尝试发射
    auto issue_result = rs.issue_ready_instruction();
    
    EXPECT_TRUE(issue_result.success) << "发射应该成功";
    EXPECT_EQ(issue_result.unit_type, ExecutionUnitType::ALU) << "应该分配到ALU单元";
    EXPECT_GE(issue_result.unit_id, 0) << "单元ID应该有效";
}

// 测试4：操作数依赖处理
TEST_F(ReservationStationTest, OperandDependency) {
    // 派发一个操作数未准备好的指令
    auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto dynamic_inst = createDynamicInst(inst, 32, 33, 34, false, true);  // src1未准备好
    
    auto dispatch_result = rs.dispatch_instruction(dynamic_inst);
    EXPECT_TRUE(dispatch_result.success);
    
    // 尝试发射，应该失败（操作数未准备好）
    auto issue_result = rs.issue_ready_instruction();
    EXPECT_FALSE(issue_result.success) << "操作数未准备好时不应该发射";
    
    // 通过完成事件更新操作数
    // 创建一个模拟的DynamicInst来测试完成事件更新
    DecodedInstruction test_decoded;
    test_decoded.type = InstructionType::R_TYPE;
    test_decoded.opcode = Opcode::OP;
    test_decoded.rd = 1;
    test_decoded.rs1 = 2;
    test_decoded.rs2 = 3;
    test_decoded.funct3 = Funct3::ADD_SUB;
    test_decoded.funct7 = Funct7::NORMAL;
    test_decoded.imm = 0;
    
    auto mock_inst = std::make_shared<DynamicInst>(test_decoded, 0x1000, 999);
    mock_inst->set_physical_dest(PhysRegNum(32));  // 对应src1_reg
    mock_inst->set_physical_dest_kind(RegisterFileKind::Integer);
    mock_inst->set_result(0xAABBCCDD);
    
    CompletionEvent completion_event(mock_inst);
    
    rs.update_operands(completion_event, nullptr);
    
    // 现在应该可以发射了
    issue_result = rs.issue_ready_instruction();
    EXPECT_TRUE(issue_result.success) << "操作数准备好后应该可以发射";
}

// 测试5：不同指令类型的执行单元分配
TEST_F(ReservationStationTest, ExecutionUnitAllocation) {
    struct TestCase {
        InstructionType type;
        Opcode opcode;
        ExecutionUnitType expected_unit;
        std::string description;
    };
    
    std::vector<TestCase> test_cases = {
        {InstructionType::R_TYPE, Opcode::OP, ExecutionUnitType::ALU, "R型指令->ALU"},
        {InstructionType::I_TYPE, Opcode::OP_IMM, ExecutionUnitType::ALU, "I型算术指令->ALU"},
        {InstructionType::R_TYPE, Opcode::OP_FP, ExecutionUnitType::FP, "浮点算术指令->FP"},
        {InstructionType::I_TYPE, Opcode::LOAD, ExecutionUnitType::LOAD, "加载指令->LOAD单元"},
        {InstructionType::S_TYPE, Opcode::STORE, ExecutionUnitType::STORE, "存储指令->STORE单元"},
        {InstructionType::B_TYPE, Opcode::BRANCH, ExecutionUnitType::BRANCH, "分支指令->BRANCH单元"},
    };
    
    for (const auto& test_case : test_cases) {
        auto inst = createInstruction(test_case.type, test_case.opcode, 1, 2, 3);
        auto dynamic_inst = createDynamicInst(inst, 32, 33, 34);
        
        auto dispatch_result = rs.dispatch_instruction(dynamic_inst);
        EXPECT_TRUE(dispatch_result.success) << test_case.description + " 派发失败";
        
        auto issue_result = rs.issue_ready_instruction();
        EXPECT_TRUE(issue_result.success) << test_case.description + " 发射失败";
        EXPECT_EQ(issue_result.unit_type, test_case.expected_unit) 
            << test_case.description + " 执行单元类型错误";
        
        // 释放保留站表项（发射后指令离开保留站）
        rs.release_entry(issue_result.rs_entry);
        // 释放执行单元以便下次测试
        rs.release_execution_unit(issue_result.unit_type, issue_result.unit_id);
    }
}

// 测试6：执行单元忙碌处理  
TEST_F(ReservationStationTest, ExecutionUnitBusy) {
    constexpr size_t instruction_count = OOOPipelineConfig::ALU_UNITS + 1;
    std::vector<DynamicInstPtr> instructions;
    instructions.reserve(instruction_count);
    for (size_t i = 0; i < instruction_count; ++i) {
        const auto inst = createInstruction(InstructionType::R_TYPE,
                                            Opcode::OP,
                                            static_cast<uint8_t>(i + 1),
                                            static_cast<uint8_t>(i + 2),
                                            static_cast<uint8_t>(i + 3));
        instructions.push_back(createDynamicInst(inst,
                                                 static_cast<PhysRegNum>(32 + i * 3),
                                                 static_cast<PhysRegNum>(33 + i * 3),
                                                 static_cast<PhysRegNum>(34 + i * 3),
                                                 true,
                                                 true,
                                                 static_cast<uint32_t>(0x1000 + i * 4),
                                                 static_cast<ROBEntry>(i + 1)));
        EXPECT_TRUE(rs.dispatch_instruction(instructions.back()).success);
    }

    ReservationStation::ReadyIssueResult first_issue;
    for (size_t i = 0; i < OOOPipelineConfig::ALU_UNITS; ++i) {
        auto issue = rs.issue_ready_instruction();
        EXPECT_TRUE(issue.success) << "有空闲 ALU 时应成功发射";
        if (i == 0) {
            first_issue = issue;
        }
    }

    auto blocked_issue = rs.issue_ready_instruction();
    EXPECT_FALSE(blocked_issue.success) << "ALU 全忙时下一条指令不应该发射";

    // 释放一个执行单元
    rs.release_execution_unit(first_issue.unit_type, first_issue.unit_id);
    
    auto resumed_issue = rs.issue_ready_instruction();
    EXPECT_TRUE(resumed_issue.success) << "释放执行单元后应该可以发射";
}

TEST_F(ReservationStationTest, FlushYoungerThanKeepsOlderEntries) {
    auto inst1 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto inst2 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 4, 5, 6);

    auto older = createDynamicInst(inst1, 32, 33, 34, true, true, 0x1000, 1);
    auto younger = createDynamicInst(inst2, 35, 36, 37, true, true, 0x1004, 2);

    ASSERT_TRUE(rs.dispatch_instruction(older).success);
    ASSERT_TRUE(rs.dispatch_instruction(younger).success);

    rs.flush_younger_than(older->get_instruction_id());

    EXPECT_EQ(rs.get_occupied_entry_count(), 1u);
    EXPECT_TRUE(rs.issue_ready_instruction().success) << "older 指令应保留在保留站中";
    EXPECT_FALSE(rs.issue_ready_instruction().success) << "younger 指令应被冲刷";
}

// 测试7：流水线刷新
TEST_F(ReservationStationTest, PipelineFlush) {
    // 派发一些指令
    for (int i = 0; i < 5; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, i + 1, 2, 3);
        auto dynamic_inst = createDynamicInst(inst, 32 + i, 33, 34 + i, true, true, 0x1000 + i * 4, i + 1);
        
        auto result = rs.dispatch_instruction(dynamic_inst);
        EXPECT_TRUE(result.success);
    }
    
    size_t free_before = rs.get_free_entry_count();
    
    // 发射一些指令占用执行单元
    rs.issue_ready_instruction();
    rs.issue_ready_instruction();
    
    // 刷新流水线
    rs.flush_pipeline();
    
    size_t free_after = rs.get_free_entry_count();
    
    // 刷新后应该释放所有表项和执行单元
    EXPECT_GT(free_after, free_before) << "刷新后应该有更多空闲表项";
    EXPECT_TRUE(rs.is_execution_unit_available(ExecutionUnitType::ALU)) 
        << "刷新后ALU单元应该可用";
}

// 测试8：统计信息
TEST_F(ReservationStationTest, Statistics) {
    uint64_t dispatched_before, issued_before, stalls_before;
    rs.get_statistics(dispatched_before, issued_before, stalls_before);
    
    // 派发几条指令
    for (int i = 0; i < 3; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, i + 1, 2, 3);
        auto dynamic_inst = createDynamicInst(inst, 32 + i, 33, 34 + i, true, true, 0x1000 + i * 4, i + 1);
        
        auto result = rs.dispatch_instruction(dynamic_inst);
        EXPECT_TRUE(result.success);
    }
    
    // 发射几条指令
    rs.issue_ready_instruction();
    rs.issue_ready_instruction();
    
    uint64_t dispatched_after, issued_after, stalls_after;
    rs.get_statistics(dispatched_after, issued_after, stalls_after);
    
    EXPECT_EQ(dispatched_after, dispatched_before + 3) << "派发计数应该增加3";
    EXPECT_EQ(issued_after, issued_before + 2) << "发射计数应该增加2";
    EXPECT_EQ(stalls_after, stalls_before) << "没有资源冲突时停顿次数不应该增加";
}

// 测试9：优先级发射 (简化版本，只测试基本功能)
TEST_F(ReservationStationTest, PriorityIssue) {
    // 派发一条准备好的指令
    auto inst1 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto dynamic_inst1 = createDynamicInst(inst1, 32, 33, 34, true, true, 0x1000, 5);
    
    // 派发指令
    EXPECT_TRUE(rs.dispatch_instruction(dynamic_inst1).success);
    
    // 发射指令
    auto issue_result = rs.issue_ready_instruction();
    EXPECT_TRUE(issue_result.success);
    EXPECT_EQ(issue_result.instruction->get_rob_entry(), 5) 
        << "应该发射正确的指令";
}

// 测试10：当最老ready指令执行单元忙碌时，应该继续尝试后续可发射指令
TEST_F(ReservationStationTest, SkipBusyUnitAndIssueOtherReadyInstruction) {
    // 先占用一个 LOAD 执行单元
    auto busy_load = createInstruction(InstructionType::I_TYPE, Opcode::LOAD, 1, 2, 0);
    auto busy_load_inst = createDynamicInst(busy_load, 32, 0, 40, true, true, 0x1000, 1);
    EXPECT_TRUE(rs.dispatch_instruction(busy_load_inst).success);
    auto first_issue = rs.issue_ready_instruction();
    EXPECT_TRUE(first_issue.success);
    EXPECT_EQ(first_issue.unit_type, ExecutionUnitType::LOAD);

    // 再放入一条更老的 ready LOAD。由于现在有第二个 LOAD 单元，它应优先被发射。
    auto blocked_load = createInstruction(InstructionType::I_TYPE, Opcode::LOAD, 3, 4, 0);
    auto blocked_load_inst = createDynamicInst(blocked_load, 41, 0, 42, true, true, 0x1004, 2);
    EXPECT_TRUE(rs.dispatch_instruction(blocked_load_inst).success);

    // 放入一条更年轻的 ready STORE，验证不会越过更老且可执行的 LOAD。
    auto ready_store = createInstruction(InstructionType::S_TYPE, Opcode::STORE, 0, 5, 6);
    auto ready_store_inst = createDynamicInst(ready_store, 43, 44, 0, true, true, 0x1008, 3);
    EXPECT_TRUE(rs.dispatch_instruction(ready_store_inst).success);

    auto second_issue = rs.issue_ready_instruction();
    EXPECT_TRUE(second_issue.success) << "第二个 LOAD 单元空闲时，应优先发射更老的 LOAD";
    EXPECT_EQ(second_issue.unit_type, ExecutionUnitType::LOAD);
    EXPECT_EQ(second_issue.instruction->get_instruction_id(),
              blocked_load_inst->get_instruction_id());
    EXPECT_NE(second_issue.unit_id, first_issue.unit_id)
        << "更老的 LOAD 应占用另一个 LOAD 单元";
}

TEST_F(ReservationStationTest, BatchIssueUsesTwoAvailableAluSlots) {
    auto inst1 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto dyn1 = createDynamicInst(inst1, 32, 33, 34, true, true, 0x1000, 1);
    auto inst2 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 4, 5, 6);
    auto dyn2 = createDynamicInst(inst2, 35, 36, 37, true, true, 0x1004, 2);

    EXPECT_TRUE(rs.dispatch_instruction(dyn1).success);
    EXPECT_TRUE(rs.dispatch_instruction(dyn2).success);

    const auto results = rs.issue_ready_instructions(2);
    ASSERT_EQ(results.size(), 2u) << "同拍应能挑出两条 ALU 指令";
    EXPECT_EQ(results[0].instruction->get_instruction_id(), dyn1->get_instruction_id());
    EXPECT_EQ(results[1].instruction->get_instruction_id(), dyn2->get_instruction_id());
    EXPECT_NE(results[0].unit_id, results[1].unit_id) << "两条 ALU 指令应占用不同 ALU 单元";
}

TEST_F(ReservationStationTest, BatchIssueUsesIndependentAluAndFpSlots) {
    auto alu_inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto alu_dyn = createDynamicInst(alu_inst, 32, 33, 34, true, true, 0x1000, 1);

    auto fp_inst = createInstruction(InstructionType::R_TYPE, Opcode::OP_FP, 4, 5, 6);
    fp_inst.funct7 = Funct7::FADD_S;
    auto fp_dyn = createDynamicInst(fp_inst, 35, 36, 37, true, true, 0x1004, 2);
    fp_dyn->set_physical_dest_kind(RegisterFileKind::FloatingPoint);
    fp_dyn->set_physical_src1_kind(RegisterFileKind::FloatingPoint);
    fp_dyn->set_physical_src2_kind(RegisterFileKind::FloatingPoint);

    EXPECT_TRUE(rs.dispatch_instruction(alu_dyn).success);
    EXPECT_TRUE(rs.dispatch_instruction(fp_dyn).success);

    const auto results = rs.issue_ready_instructions(2);
    ASSERT_EQ(results.size(), 2u) << "同拍应能同时发射一条整数 ALU 和一条 FP 算术指令";
    EXPECT_EQ(results[0].instruction->get_instruction_id(), alu_dyn->get_instruction_id());
    EXPECT_EQ(results[0].unit_type, ExecutionUnitType::ALU);
    EXPECT_EQ(results[1].instruction->get_instruction_id(), fp_dyn->get_instruction_id());
    EXPECT_EQ(results[1].unit_type, ExecutionUnitType::FP);
}

TEST_F(ReservationStationTest, BatchIssueUsesTwoAvailableLoadSlots) {
    auto load1 = createInstruction(InstructionType::I_TYPE, Opcode::LOAD, 1, 2, 0);
    auto load_dyn1 = createDynamicInst(load1, 32, 0, 40, true, true, 0x1000, 1);

    auto load2 = createInstruction(InstructionType::I_TYPE, Opcode::LOAD, 3, 4, 0);
    auto load_dyn2 = createDynamicInst(load2, 41, 0, 42, true, true, 0x1004, 2);

    EXPECT_TRUE(rs.dispatch_instruction(load_dyn1).success);
    EXPECT_TRUE(rs.dispatch_instruction(load_dyn2).success);

    const auto results = rs.issue_ready_instructions(2);
    ASSERT_EQ(results.size(), 2u) << "同拍应能发射两条加载指令";
    EXPECT_EQ(results[0].unit_type, ExecutionUnitType::LOAD);
    EXPECT_EQ(results[1].unit_type, ExecutionUnitType::LOAD);
    EXPECT_NE(results[0].unit_id, results[1].unit_id) << "两条加载指令应占用不同 LOAD 单元";
}

TEST_F(ReservationStationTest, BatchIssueSkipsBlockedOlderCandidateForSecondSlot) {
    auto busy_load = createInstruction(InstructionType::I_TYPE, Opcode::LOAD, 1, 2, 0);
    auto busy_load_inst = createDynamicInst(busy_load, 32, 0, 40, true, true, 0x1000, 1);
    EXPECT_TRUE(rs.dispatch_instruction(busy_load_inst).success);
    auto first_issue = rs.issue_ready_instruction();
    ASSERT_TRUE(first_issue.success);
    ASSERT_EQ(first_issue.unit_type, ExecutionUnitType::LOAD);

    auto blocked_load = createInstruction(InstructionType::I_TYPE, Opcode::LOAD, 3, 4, 0);
    auto blocked_load_inst = createDynamicInst(blocked_load, 41, 0, 42, true, true, 0x1004, 2);
    auto alu_inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, 5, 6, 7);
    auto alu_dyn = createDynamicInst(alu_inst, 43, 44, 45, true, true, 0x1008, 3);
    auto store_inst = createInstruction(InstructionType::S_TYPE, Opcode::STORE, 0, 8, 9);
    auto store_dyn = createDynamicInst(store_inst, 46, 47, 0, true, true, 0x100C, 4);

    EXPECT_TRUE(rs.dispatch_instruction(blocked_load_inst).success);
    EXPECT_TRUE(rs.dispatch_instruction(alu_dyn).success);
    EXPECT_TRUE(rs.dispatch_instruction(store_dyn).success);

    const auto results = rs.issue_ready_instructions(2);
    ASSERT_EQ(results.size(), 2u) << "第二个 LOAD 单元可用时，应先发射更老 LOAD，再发射后续 ALU";
    EXPECT_EQ(results[0].instruction->get_instruction_id(), blocked_load_inst->get_instruction_id());
    EXPECT_EQ(results[1].instruction->get_instruction_id(), alu_dyn->get_instruction_id());
    EXPECT_EQ(results[0].unit_type, ExecutionUnitType::LOAD);
    EXPECT_EQ(results[1].unit_type, ExecutionUnitType::ALU);
    EXPECT_NE(results[0].unit_id, first_issue.unit_id)
        << "更老的 LOAD 应分配到另一个 LOAD 单元";
}

TEST_F(ReservationStationTest, BatchIssueSkipsPredicateRejectedOlderCandidate) {
    auto older_load = createInstruction(InstructionType::I_TYPE, Opcode::LOAD, 1, 2, 0);
    auto older_load_inst = createDynamicInst(older_load, 32, 0, 40, true, true, 0x1000, 1);
    auto younger_alu = createInstruction(InstructionType::R_TYPE, Opcode::OP, 3, 4, 5);
    auto younger_alu_inst = createDynamicInst(younger_alu, 41, 42, 43, true, true, 0x1004, 2);

    EXPECT_TRUE(rs.dispatch_instruction(older_load_inst).success);
    EXPECT_TRUE(rs.dispatch_instruction(younger_alu_inst).success);

    const auto results = rs.issue_ready_instructions(
        2,
        [&](const DynamicInstPtr& inst) {
            return inst->get_instruction_id() != older_load_inst->get_instruction_id();
        });

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].instruction->get_instruction_id(),
              younger_alu_inst->get_instruction_id());
    EXPECT_EQ(results[0].unit_type, ExecutionUnitType::ALU);
}

} // namespace riscv
