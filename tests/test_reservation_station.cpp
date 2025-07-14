#include <gtest/gtest.h>
#include "reservation_station.h"
#include "decoder.h"
#include "debug_types.h"
#include <vector>

namespace riscv {

class ReservationStationTest : public ::testing::Test {
protected:
    ReservationStation rs;
    Decoder decoder;
    
    void SetUp() override {
        // 每个测试前的初始化
        // 启用调试输出
        auto& debugManager = DebugManager::getInstance();

        // 设置回调函数
        debugManager.setCallback([](const DebugInfo& info) {
            std::cout << DebugFormatter::format(info, DebugFormatter::Mode::VERBOSE) << std::endl;
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
    
    // 辅助函数：创建保留站表项
    ReservationStationEntry createRSEntry(const DecodedInstruction& inst, 
                                         PhysRegNum src1_reg, PhysRegNum src2_reg, 
                                         PhysRegNum dest_reg, bool src1_ready = true, 
                                         bool src2_ready = true, uint32_t pc = 0x1000,
                                         ROBEntry rob_entry = 1) {
        ReservationStationEntry entry;
        entry.instruction = inst;
        entry.src1_reg = src1_reg;
        entry.src2_reg = src2_reg;
        entry.dest_reg = dest_reg;
        entry.src1_ready = src1_ready;
        entry.src2_ready = src2_ready;
        entry.src1_value = 0x12345678;
        entry.src2_value = 0x87654321;
        entry.pc = pc;
        entry.rob_entry = rob_entry;
        entry.valid = true;
        return entry;
    }
};

// 测试1：基本发射功能
TEST_F(ReservationStationTest, BasicIssue) {
    EXPECT_TRUE(rs.has_free_entry()) << "初始应该有空闲表项";
    
    size_t initial_free = rs.get_free_entry_count();
    EXPECT_GT(initial_free, 0) << "初始应该有空闲表项";
    
    // 创建一个ALU指令
    auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto entry = createRSEntry(inst, 32, 33, 34);
    
    // 发射指令
    auto result = rs.issue_instruction(entry);
    
    EXPECT_TRUE(result.success) << "发射应该成功";
    EXPECT_EQ(rs.get_free_entry_count(), initial_free - 1) << "空闲表项应该减少1";
}

// 测试2：保留站容量测试
TEST_F(ReservationStationTest, CapacityTest) {
    std::vector<ReservationStation::IssueResult> results;
    
    // 填满保留站
    for (int i = 0; i < 20; ++i) {  // 尝试发射超过容量的指令
        auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, i % 32, 2, 3);
        auto entry = createRSEntry(inst, 32 + i, 33, 34 + i, true, true, 0x1000 + i * 4, i + 1);
        
        auto result = rs.issue_instruction(entry);
        results.push_back(result);
        
        if (!result.success) {
            break;  // 保留站已满
        }
    }
    
    // 检查是否有发射失败的情况
    bool found_failure = false;
    for (const auto& result : results) {
        if (!result.success) {
            found_failure = true;
            EXPECT_FALSE(result.error_message.empty()) << "失败时应该有错误信息";
            break;
        }
    }
    
    EXPECT_TRUE(found_failure) << "当保留站满时应该发射失败";
    EXPECT_EQ(rs.get_free_entry_count(), 0) << "保留站满时应该没有空闲表项";
}

// 测试3：基本调度功能
TEST_F(ReservationStationTest, BasicDispatch) {
    // 发射一个准备好的指令
    auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto entry = createRSEntry(inst, 32, 33, 34, true, true);  // 两个操作数都准备好
    
    auto issue_result = rs.issue_instruction(entry);
    EXPECT_TRUE(issue_result.success);
    
    // 尝试调度
    auto dispatch_result = rs.dispatch_instruction();
    
    EXPECT_TRUE(dispatch_result.success) << "调度应该成功";
    EXPECT_EQ(dispatch_result.unit_type, ExecutionUnitType::ALU) << "应该分配到ALU单元";
    EXPECT_GE(dispatch_result.unit_id, 0) << "单元ID应该有效";
}

// 测试4：操作数依赖处理
TEST_F(ReservationStationTest, OperandDependency) {
    // 发射一个操作数未准备好的指令
    auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto entry = createRSEntry(inst, 32, 33, 34, false, true);  // src1未准备好
    
    auto issue_result = rs.issue_instruction(entry);
    EXPECT_TRUE(issue_result.success);
    
    // 尝试调度，应该失败（操作数未准备好）
    auto dispatch_result = rs.dispatch_instruction();
    EXPECT_FALSE(dispatch_result.success) << "操作数未准备好时不应该调度";
    
    // 通过CDB更新操作数
    CommonDataBusEntry cdb_entry;
    cdb_entry.dest_reg = 32;  // 对应src1_reg
    cdb_entry.value = 0xAABBCCDD;
    cdb_entry.rob_entry = 1;
    cdb_entry.valid = true;
    
    rs.update_operands(cdb_entry);
    
    // 现在应该可以调度了
    dispatch_result = rs.dispatch_instruction();
    EXPECT_TRUE(dispatch_result.success) << "操作数准备好后应该可以调度";
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
        {InstructionType::I_TYPE, Opcode::LOAD, ExecutionUnitType::LOAD, "加载指令->LOAD单元"},
        {InstructionType::S_TYPE, Opcode::STORE, ExecutionUnitType::STORE, "存储指令->STORE单元"},
        {InstructionType::B_TYPE, Opcode::BRANCH, ExecutionUnitType::BRANCH, "分支指令->BRANCH单元"},
    };
    
    for (const auto& test_case : test_cases) {
        auto inst = createInstruction(test_case.type, test_case.opcode, 1, 2, 3);
        auto entry = createRSEntry(inst, 32, 33, 34);
        
        auto issue_result = rs.issue_instruction(entry);
        EXPECT_TRUE(issue_result.success) << test_case.description + " 发射失败";
        
        auto dispatch_result = rs.dispatch_instruction();
        EXPECT_TRUE(dispatch_result.success) << test_case.description + " 调度失败";
        EXPECT_EQ(dispatch_result.unit_type, test_case.expected_unit) 
            << test_case.description + " 执行单元类型错误";
        
        // 释放执行单元以便下次测试
        rs.release_execution_unit(dispatch_result.unit_type, dispatch_result.unit_id);
    }
}

// 测试6：执行单元忙碌处理
TEST_F(ReservationStationTest, ExecutionUnitBusy) {
    // 发射两个ALU指令
    auto inst1 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto entry1 = createRSEntry(inst1, 32, 33, 34, true, true, 0x1000, 1);
    
    auto inst2 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 4, 5, 6);
    auto entry2 = createRSEntry(inst2, 35, 36, 37, true, true, 0x1004, 2);
    
    auto inst3 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 7, 8, 9);
    auto entry3 = createRSEntry(inst3, 38, 39, 40, true, true, 0x1008, 3);
    
    // 发射三条指令
    EXPECT_TRUE(rs.issue_instruction(entry1).success);
    EXPECT_TRUE(rs.issue_instruction(entry2).success);
    EXPECT_TRUE(rs.issue_instruction(entry3).success);
    
    // 调度前两条指令（假设有2个ALU单元）
    auto dispatch1 = rs.dispatch_instruction();
    EXPECT_TRUE(dispatch1.success) << "第一条指令应该成功调度";
    
    auto dispatch2 = rs.dispatch_instruction();
    EXPECT_TRUE(dispatch2.success) << "第二条指令应该成功调度";
    
    // 第三条指令应该因为执行单元忙碌而调度失败
    auto dispatch3 = rs.dispatch_instruction();
    EXPECT_FALSE(dispatch3.success) << "执行单元忙碌时第三条指令不应该调度";
    
    // 释放一个执行单元
    rs.release_execution_unit(dispatch1.unit_type, dispatch1.unit_id);
    
    // 现在第三条指令应该可以调度了
    dispatch3 = rs.dispatch_instruction();
    EXPECT_TRUE(dispatch3.success) << "释放执行单元后应该可以调度";
}

// 测试7：流水线刷新
TEST_F(ReservationStationTest, PipelineFlush) {
    // 发射一些指令
    for (int i = 0; i < 5; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, i + 1, 2, 3);
        auto entry = createRSEntry(inst, 32 + i, 33, 34 + i, true, true, 0x1000 + i * 4, i + 1);
        
        auto result = rs.issue_instruction(entry);
        EXPECT_TRUE(result.success);
    }
    
    size_t free_before = rs.get_free_entry_count();
    
    // 调度一些指令占用执行单元
    rs.dispatch_instruction();
    rs.dispatch_instruction();
    
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
    uint64_t issued_before, dispatched_before, stalls_before;
    rs.get_statistics(issued_before, dispatched_before, stalls_before);
    
    // 发射几条指令
    for (int i = 0; i < 3; ++i) {
        auto inst = createInstruction(InstructionType::R_TYPE, Opcode::OP, i + 1, 2, 3);
        auto entry = createRSEntry(inst, 32 + i, 33, 34 + i, true, true, 0x1000 + i * 4, i + 1);
        
        auto result = rs.issue_instruction(entry);
        EXPECT_TRUE(result.success);
    }
    
    // 调度几条指令
    rs.dispatch_instruction();
    rs.dispatch_instruction();
    
    uint64_t issued_after, dispatched_after, stalls_after;
    rs.get_statistics(issued_after, dispatched_after, stalls_after);
    
    EXPECT_EQ(issued_after, issued_before + 3) << "发射计数应该增加3";
    EXPECT_EQ(dispatched_after, dispatched_before + 2) << "调度计数应该增加2";
    EXPECT_EQ(stalls_after, stalls_before) << "没有资源冲突时停顿次数不应该增加";
}

// 测试9：优先级调度
TEST_F(ReservationStationTest, PriorityScheduling) {
    // 发射多条准备好的指令，ROB编号不同
    auto inst1 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 1, 2, 3);
    auto entry1 = createRSEntry(inst1, 32, 33, 34, true, true, 0x1000, 5);  // ROB=5
    
    auto inst2 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 4, 5, 6);
    auto entry2 = createRSEntry(inst2, 35, 36, 37, true, true, 0x1004, 2);  // ROB=2
    
    auto inst3 = createInstruction(InstructionType::R_TYPE, Opcode::OP, 7, 8, 9);
    auto entry3 = createRSEntry(inst3, 38, 39, 40, true, true, 0x1008, 8);  // ROB=8
    
    // 发射指令（故意乱序发射）
    EXPECT_TRUE(rs.issue_instruction(entry1).success);
    EXPECT_TRUE(rs.issue_instruction(entry3).success);
    EXPECT_TRUE(rs.issue_instruction(entry2).success);
    
    // 调度第一条指令，应该是ROB编号最小的（entry2, ROB=2）
    auto dispatch_result = rs.dispatch_instruction();
    EXPECT_TRUE(dispatch_result.success);
    EXPECT_EQ(dispatch_result.instruction.rob_entry, 2) 
        << "应该优先调度ROB编号最小的指令";
}

} // namespace riscv