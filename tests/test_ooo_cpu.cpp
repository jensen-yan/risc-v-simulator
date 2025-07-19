#include <gtest/gtest.h>
#include "cpu/ooo/ooo_cpu.h"
#include "core/memory.h"
#include <memory>
#include "common/debug_types.h"

namespace riscv {

class OutOfOrderCPUTest : public ::testing::Test {
protected:
    std::shared_ptr<Memory> memory;
    std::unique_ptr<OutOfOrderCPU> cpu;
    
    void SetUp() override {
        memory = std::make_shared<Memory>(8192);  // 8KB内存
        cpu = std::make_unique<OutOfOrderCPU>(memory);

        // 启用调试输出
        auto& debugManager = DebugManager::getInstance();

        // 设置回调函数
        debugManager.setCallback([](const DebugInfo& info) {
            std::cout << DebugFormatter::format(info, DebugFormatter::Mode::VERBOSE) << std::endl;
        });
        
        // 启用相关分类
        debugManager.enableCategory("RS");
        debugManager.enableCategory("SYSTEM");
        debugManager.enableCategory("COMMIT");
        debugManager.enableCategory("ISSUE");
        debugManager.enableCategory("WRITEBACK");
        debugManager.enableCategory("RENAME");
        debugManager.enableCategory("ROB");
        debugManager.enableCategory("DECODE");
    }
    
    void TearDown() override {
        cpu.reset();
        memory.reset();
    }
    
    // 辅助函数：向内存写入指令
    void writeInstruction(uint32_t address, uint32_t instruction) {
        memory->writeWord(address, instruction);
    }
    
    // 辅助函数：创建R型指令
    uint32_t createRTypeInstruction(uint8_t funct7, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }
    
    // 辅助函数：创建I型指令
    uint32_t createITypeInstruction(int16_t imm, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        return (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }
    
    // 辅助函数：创建ECALL指令
    uint32_t createECallInstruction() {
        return 0x00000073;  // ECALL指令的机器码
    }
};

// 测试1：基本CPU初始化
TEST_F(OutOfOrderCPUTest, BasicInitialization) {
    EXPECT_FALSE(cpu->isHalted()) << "CPU初始化时不应该停机";
    EXPECT_EQ(cpu->getPC(), 0) << "初始PC应该为0";
    EXPECT_EQ(cpu->getInstructionCount(), 0) << "初始指令计数应该为0";
    EXPECT_EQ(cpu->getCycleCount(), 0) << "初始周期计数应该为0";
    
    // 检查寄存器初始状态
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(cpu->getRegister(i), 0) << "寄存器 x" << i << " 初始值应该为0";
    }
}

// 测试2：简单指令执行
TEST_F(OutOfOrderCPUTest, SimpleInstructionExecution) {
    // 写入简单的ADD指令: add x1, x0, x0 (x1 = x0 + x0 = 0)
    uint32_t add_inst = createRTypeInstruction(0x00, 0, 0, 0x0, 1, 0x33);
    writeInstruction(0x0, add_inst);
    
    // 写入ECALL指令结束程序
    writeInstruction(0x4, createECallInstruction());
    
    // 设置PC并执行
    cpu->setPC(0x0);
    
    // 执行几个周期
    for (int i = 0; i < 10 && !cpu->isHalted(); ++i) {
        cpu->step();
    }
    
    // 验证结果
    EXPECT_TRUE(cpu->isHalted()) << "程序应该已经停机";
    EXPECT_GT(cpu->getCycleCount(), 0) << "应该执行了一些周期";
}

// 测试3：寄存器操作
TEST_F(OutOfOrderCPUTest, RegisterOperations) {
    // 测试寄存器读写
    cpu->setRegister(1, 0x12345678);
    EXPECT_EQ(cpu->getRegister(1), 0x12345678) << "寄存器写入读取应该一致";
    
    // 测试x0寄存器始终为0
    cpu->setRegister(0, 0xFFFFFFFF);
    EXPECT_EQ(cpu->getRegister(0), 0) << "x0寄存器应该始终为0";
    
    // 测试浮点寄存器
    cpu->setFPRegister(0, 0x12345678);
    EXPECT_EQ(cpu->getFPRegister(0), 0x12345678) << "浮点寄存器写入读取应该一致";
    
    // 测试浮点寄存器浮点数访问
    cpu->setFPRegisterFloat(1, 3.14159f);
    EXPECT_NEAR(cpu->getFPRegisterFloat(1), 3.14159f, 0.00001f) << "浮点寄存器浮点数访问应该正确";
}

// 测试4：立即数指令
TEST_F(OutOfOrderCPUTest, ImmediateInstructions) {
    // ADDI x1, x0, 42  (x1 = x0 + 42 = 42)
    uint32_t addi_inst = createITypeInstruction(42, 0, 0x0, 1, 0x13);
    writeInstruction(0x0, addi_inst);
    
    // ECALL
    writeInstruction(0x4, createECallInstruction());
    
    cpu->setPC(0x0);
    
    // 执行直到停机
    for (int i = 0; i < 20 && !cpu->isHalted(); ++i) {
        cpu->step();
    }
    
    // 验证结果
    EXPECT_TRUE(cpu->isHalted()) << "程序应该已经停机";
    EXPECT_EQ(cpu->getRegister(1), 42) << "x1寄存器应该包含42";
}

// 测试5：多指令序列
TEST_F(OutOfOrderCPUTest, MultipleInstructions) {
    // 指令序列:
    // ADDI x1, x0, 10   // x1 = 10
    // ADDI x2, x0, 20   // x2 = 20  
    // ADD x3, x1, x2    // x3 = x1 + x2 = 30
    // ECALL
    
    writeInstruction(0x0, createITypeInstruction(10, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(20, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createRTypeInstruction(0x00, 2, 1, 0x0, 3, 0x33));
    writeInstruction(0xC, createECallInstruction());
    
    cpu->setPC(0x0);
    
    // 执行直到停机
    for (int i = 0; i < 50 && !cpu->isHalted(); ++i) {
        cpu->step();
    }
    
    // 验证结果
    EXPECT_TRUE(cpu->isHalted()) << "程序应该已经停机";
    EXPECT_EQ(cpu->getRegister(1), 10) << "x1应该等于10";
    EXPECT_EQ(cpu->getRegister(2), 20) << "x2应该等于20";
    EXPECT_EQ(cpu->getRegister(3), 30) << "x3应该等于30";
    
    // 验证性能统计
    uint64_t instructions, cycles, branch_mispredicts, stalls;
    cpu->getPerformanceStats(instructions, cycles, branch_mispredicts, stalls);
    
    EXPECT_GT(instructions, 0) << "应该执行了一些指令";
    EXPECT_GT(cycles, 0) << "应该消耗了一些周期";
    
    std::cout << "性能统计:" << std::endl;
    std::cout << "指令数: " << instructions << std::endl;
    std::cout << "周期数: " << cycles << std::endl;
    std::cout << "分支预测错误: " << branch_mispredicts << std::endl;
    std::cout << "流水线停顿: " << stalls << std::endl;
    
    if (cycles > 0) {
        double ipc = static_cast<double>(instructions) / cycles;
        std::cout << "IPC: " << ipc << std::endl;
    }
}

// 测试6：CPU状态重置
TEST_F(OutOfOrderCPUTest, CPUStateReset) {
    // 设置一些状态
    cpu->setRegister(1, 0x12345678);
    cpu->setPC(0x100);
    
    // 执行一些周期
    for (int i = 0; i < 5; ++i) {
        cpu->step();
    }
    
    // 重置CPU
    cpu->reset();
    
    // 验证重置后状态
    EXPECT_EQ(cpu->getPC(), 0) << "重置后PC应该为0";
    EXPECT_EQ(cpu->getRegister(1), 0) << "重置后寄存器应该为0";
    EXPECT_EQ(cpu->getCycleCount(), 0) << "重置后周期计数应该为0";
    EXPECT_EQ(cpu->getInstructionCount(), 0) << "重置后指令计数应该为0";
    EXPECT_FALSE(cpu->isHalted()) << "重置后CPU不应该停机";
}

// 测试7：流水线状态调试
TEST_F(OutOfOrderCPUTest, DebugOutput) {
    // 这个测试主要验证调试输出函数不会崩溃
    cpu->dumpState();
    cpu->dumpRegisters();
    cpu->dumpPipelineState();
    
    // 如果没有崩溃，测试就通过了
    SUCCEED();
}

} // namespace riscv