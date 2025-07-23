#include <gtest/gtest.h>
#include "system/difftest.h"
#include "cpu/inorder/cpu.h"
#include "cpu/ooo/ooo_cpu.h"
#include "core/memory.h"
#include <memory>

namespace riscv {

/**
 * DiffTest模块单元测试
 * 当前覆盖率：0% -> 目标：80%+
 */
class DiffTestTest : public ::testing::Test {
protected:
    std::shared_ptr<Memory> memory_;
    std::unique_ptr<CPU> reference_cpu_;
    std::unique_ptr<OutOfOrderCPU> main_cpu_;
    std::unique_ptr<DiffTest> difftest_;
    
    void SetUp() override {
        // 创建8KB内存用于测试
        memory_ = std::make_shared<Memory>(8192);
        
        // 创建参考CPU（顺序执行）和主CPU（乱序执行）
        reference_cpu_ = std::make_unique<CPU>(memory_);
        main_cpu_ = std::make_unique<OutOfOrderCPU>(memory_);
    }
    
    void TearDown() override {
        difftest_.reset();
        main_cpu_.reset();
        reference_cpu_.reset();
        memory_.reset();
    }
    
    // 辅助函数：创建指令
    uint32_t createRTypeInstruction(uint8_t funct7, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }
    
    uint32_t createITypeInstruction(int16_t imm, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        return (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }
    
    // 辅助函数：设置相同的CPU状态
    void synchronizeCPUStates() {
        for (RegNum reg = 0; reg < 32; ++reg) {
            uint64_t value = main_cpu_->getRegister(reg);
            reference_cpu_->setRegister(reg, value);
        }
        reference_cpu_->setPC(main_cpu_->getPC());
    }
};

// 测试1：构造函数参数验证
TEST_F(DiffTestTest, ConstructorValidation) {
    // 测试正常构造
    EXPECT_NO_THROW({
        difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    });
    
    // 测试空指针异常
    EXPECT_THROW({
        DiffTest invalid_difftest(nullptr, reference_cpu_.get());
    }, std::invalid_argument);
    
    EXPECT_THROW({
        DiffTest invalid_difftest(main_cpu_.get(), nullptr);
    }, std::invalid_argument);
    
    EXPECT_THROW({
        DiffTest invalid_difftest(nullptr, nullptr);
    }, std::invalid_argument);
}

// 测试2：基本初始化状态
TEST_F(DiffTestTest, BasicInitialization) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    
    // 检查初始状态
    EXPECT_TRUE(difftest_->isEnabled()) << "DiffTest应该默认启用";
    EXPECT_TRUE(difftest_->getStopOnMismatch()) << "应该默认在不匹配时停止";
    
    // 检查初始统计信息
    auto stats = difftest_->getStatistics();
    EXPECT_EQ(stats.comparison_count, 0) << "初始比较次数应该为0";
    EXPECT_EQ(stats.mismatch_count, 0) << "初始不匹配次数应该为0";
}

// 测试3：启用/禁用功能
TEST_F(DiffTestTest, EnableDisableFunctionality) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    
    // 测试禁用
    difftest_->setEnabled(false);
    EXPECT_FALSE(difftest_->isEnabled()) << "禁用后应该返回false";
    
    // 测试重新启用
    difftest_->setEnabled(true);
    EXPECT_TRUE(difftest_->isEnabled()) << "重新启用后应该返回true";
}

// 测试4：停止匹配配置
TEST_F(DiffTestTest, StopOnMismatchConfiguration) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    
    // 测试设置为不停止
    difftest_->setStopOnMismatch(false);
    EXPECT_FALSE(difftest_->getStopOnMismatch()) << "设置为false后应该返回false";
    
    // 测试设置为停止
    difftest_->setStopOnMismatch(true);
    EXPECT_TRUE(difftest_->getStopOnMismatch()) << "设置为true后应该返回true";
}

// 测试5：参考CPU的PC设置
TEST_F(DiffTestTest, ReferencePC) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    
    // 设置参考CPU的PC
    uint64_t test_pc = 0x1000;
    difftest_->setReferencePC(test_pc);
    EXPECT_EQ(reference_cpu_->getPC(), test_pc) << "参考CPU的PC应该被正确设置";
    
    // 测试禁用状态下的PC设置
    difftest_->setEnabled(false);
    uint64_t new_pc = 0x2000;
    difftest_->setReferencePC(new_pc);
    EXPECT_EQ(reference_cpu_->getPC(), test_pc) << "禁用状态下PC不应该被修改";
}

// 测试6：状态同步功能
TEST_F(DiffTestTest, StateSynchronization) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    
    // 设置主CPU的寄存器状态
    main_cpu_->setRegister(1, 0x12345678);
    main_cpu_->setRegister(2, 0x87654321);
    main_cpu_->setFPRegister(0, 0xDEADBEEF);
    main_cpu_->setFPRegister(1, 0xCAFEBABE);
    
    // 执行状态同步
    difftest_->syncReferenceState(main_cpu_.get());
    
    // 验证同步结果
    EXPECT_EQ(reference_cpu_->getRegister(1), 0x12345678) << "寄存器x1应该被同步";
    EXPECT_EQ(reference_cpu_->getRegister(2), 0x87654321) << "寄存器x2应该被同步";
    EXPECT_EQ(reference_cpu_->getFPRegister(0), 0xDEADBEEF) << "浮点寄存器f0应该被同步";
    EXPECT_EQ(reference_cpu_->getFPRegister(1), 0xCAFEBABE) << "浮点寄存器f1应该被同步";
}

// 测试7：禁用状态下的同步
TEST_F(DiffTestTest, SyncWhenDisabled) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    difftest_->setEnabled(false);
    
    // 设置主CPU状态
    main_cpu_->setRegister(1, 0x12345678);
    uint64_t original_value = reference_cpu_->getRegister(1);
    
    // 尝试同步（应该被忽略）
    difftest_->syncReferenceState(main_cpu_.get());
    
    // 验证状态没有改变
    EXPECT_EQ(reference_cpu_->getRegister(1), original_value) << "禁用状态下不应该同步";
}

// 测试8：空指针参数的同步
TEST_F(DiffTestTest, SyncWithNullPointer) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    
    // 传入空指针不应该崩溃
    EXPECT_NO_THROW({
        difftest_->syncReferenceState(nullptr);
    });
}

// 测试9：重置功能
TEST_F(DiffTestTest, ResetFunctionality) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    
    // 设置一些状态，创建统计数据
    reference_cpu_->setRegister(1, 0x12345678);
    reference_cpu_->setPC(0x1000);
    
    // 模拟一些比较操作以增加统计计数
    // 注意：这里我们无法直接访问私有成员，但可以通过reset验证
    
    // 执行重置
    difftest_->reset();
    
    // 验证统计信息被重置
    auto stats = difftest_->getStatistics();
    EXPECT_EQ(stats.comparison_count, 0) << "重置后比较次数应该为0";
    EXPECT_EQ(stats.mismatch_count, 0) << "重置后不匹配次数应该为0";
    
    // 验证参考CPU被重置
    EXPECT_EQ(reference_cpu_->getPC(), 0) << "重置后参考CPU的PC应该为0";
    EXPECT_EQ(reference_cpu_->getRegister(1), 0) << "重置后寄存器应该为0";
}

// 测试10：简单的状态比较（相同状态）
TEST_F(DiffTestTest, SimpleStateComparisonMatch) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    difftest_->setStopOnMismatch(false); // 避免测试中途退出
    
    // 写入简单的ADDI指令：addi x1, x0, 42
    uint32_t addi_inst = createITypeInstruction(42, 0, 0x0, 1, 0x13);
    memory_->writeWord(0x0, addi_inst);
    
    // 同步两个CPU到相同的初始状态
    main_cpu_->setPC(0x0);
    reference_cpu_->setPC(0x0);
    
    // 执行主CPU指令
    main_cpu_->step();
    // 等待指令提交
    while (!main_cpu_->isHalted() && main_cpu_->getRegister(1) == 0) {
        main_cpu_->step();
    }
    
    // 获取提交的PC并进行比较
    uint64_t committed_pc = 0x0; // 第一条指令的PC
    bool result = difftest_->stepAndCompareWithCommittedPC(main_cpu_.get(), committed_pc);
    
    // 验证比较结果
    EXPECT_TRUE(result) << "相同状态的CPU应该比较成功";
    
    // 验证统计信息
    auto stats = difftest_->getStatistics();
    EXPECT_GT(stats.comparison_count, 0) << "应该有比较操作";
    EXPECT_EQ(stats.mismatch_count, 0) << "相同状态不应该有不匹配";
}

// 测试11：状态不匹配的情况
TEST_F(DiffTestTest, StateComparisonMismatch) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    difftest_->setStopOnMismatch(false); // 避免测试中途退出
    
    // 设置不同的寄存器状态以制造不匹配
    main_cpu_->setRegister(1, 0x12345678);
    reference_cpu_->setRegister(1, 0x87654321);
    
    // 设置相同的PC
    uint64_t test_pc = 0x1000;
    main_cpu_->setPC(test_pc);
    reference_cpu_->setPC(test_pc);
    
    // 写入NOP指令
    uint32_t nop_inst = createITypeInstruction(0, 0, 0x0, 0, 0x13);
    memory_->writeWord(test_pc, nop_inst);
    
    // 进行比较（应该检测到不匹配）
    bool result = difftest_->stepAndCompareWithCommittedPC(main_cpu_.get(), test_pc);
    
    // 验证比较结果
    EXPECT_FALSE(result) << "不同状态的CPU应该比较失败";
    
    // 验证统计信息
    auto stats = difftest_->getStatistics();
    EXPECT_GT(stats.comparison_count, 0) << "应该有比较操作";
    EXPECT_GT(stats.mismatch_count, 0) << "应该检测到不匹配";
}

// 测试12：PC不匹配的情况
TEST_F(DiffTestTest, PCMismatch) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    difftest_->setStopOnMismatch(false);
    
    // 设置不同的PC
    reference_cpu_->setPC(0x1000);
    uint64_t committed_pc = 0x2000; // 不同的PC
    
    // 进行比较
    bool result = difftest_->stepAndCompareWithCommittedPC(main_cpu_.get(), committed_pc);
    
    // 验证PC不匹配被检测到
    EXPECT_FALSE(result) << "PC不匹配应该被检测到";
    
    auto stats = difftest_->getStatistics();
    EXPECT_GT(stats.mismatch_count, 0) << "PC不匹配应该增加不匹配计数";
}

// 测试13：禁用状态下的比较
TEST_F(DiffTestTest, ComparisonWhenDisabled) {
    difftest_ = std::make_unique<DiffTest>(main_cpu_.get(), reference_cpu_.get());
    difftest_->setEnabled(false);
    
    // 尝试进行比较
    bool result = difftest_->stepAndCompareWithCommittedPC(main_cpu_.get(), 0x1000);
    
    // 禁用状态下应该返回true（跳过比较）
    EXPECT_TRUE(result) << "禁用状态下应该跳过比较并返回true";
    
    // 验证统计信息没有变化
    auto stats = difftest_->getStatistics();
    EXPECT_EQ(stats.comparison_count, 0) << "禁用状态下不应该增加比较计数";
}

} // namespace riscv