#include <gtest/gtest.h>
#include "cpu.h"
#include "memory.h"
#include <memory>

using namespace riscv;

class CPUTest : public ::testing::Test {
protected:
    void SetUp() override {
        memory = std::make_shared<Memory>(4096); // 4KB 测试内存
        cpu = std::make_unique<CPU>(memory);
    }
    
    // 辅助方法：创建I-Type指令
    uint32_t createIType(Opcode opcode, RegNum rd, RegNum rs1, int32_t imm, Funct3 funct3) {
        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
        inst |= (rd & 0x1F) << 7;                               // rd [11:7]
        inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;    // funct3 [14:12]
        inst |= (rs1 & 0x1F) << 15;                             // rs1 [19:15]
        inst |= (imm & 0xFFF) << 20;                            // imm [31:20]
        return inst;
    }
    
    std::shared_ptr<Memory> memory;
    std::unique_ptr<CPU> cpu;
};

TEST_F(CPUTest, ADDI_Instruction) {
    // 测试 ADDI x1, x0, 42  (将42加到x0寄存器，结果存入x1)
    uint32_t inst = createIType(Opcode::OP_IMM, 1, 0, 42, Funct3::ADD_SUB);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(1), 42);
    EXPECT_EQ(cpu->getPC(), 4);
}

TEST_F(CPUTest, SLTI_Instruction) {
    // 设置x1为10
    cpu->setRegister(1, 10);
    
    // 测试 SLTI x2, x1, 20  (10 < 20, 结果应该是1)
    uint32_t inst = createIType(Opcode::OP_IMM, 2, 1, 20, Funct3::SLT);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(2), 1);
    EXPECT_EQ(cpu->getPC(), 4);
}

TEST_F(CPUTest, XORI_Instruction) {
    // 设置x1为0xFF
    cpu->setRegister(1, 0xFF);
    
    // 测试 XORI x2, x1, 0xAA  (0xFF ^ 0xAA = 0x55)
    uint32_t inst = createIType(Opcode::OP_IMM, 2, 1, 0xAA, Funct3::XOR);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(2), 0x55);
}

TEST_F(CPUTest, SLLI_Instruction) {
    // 设置x1为5
    cpu->setRegister(1, 5);
    
    // 测试 SLLI x2, x1, 2  (5 << 2 = 20)
    uint32_t inst = createIType(Opcode::OP_IMM, 2, 1, 2, Funct3::SLL);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(2), 20);
}

TEST_F(CPUTest, RegisterX0_AlwaysZero) {
    // 尝试写入x0寄存器
    cpu->setRegister(0, 123);
    EXPECT_EQ(cpu->getRegister(0), 0);
    
    // 测试 ADDI x0, x0, 999  (x0应该始终为0)
    uint32_t inst = createIType(Opcode::OP_IMM, 0, 0, 999, Funct3::ADD_SUB);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(0), 0);
}