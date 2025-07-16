#include <gtest/gtest.h>
#include "system/simulator.h"
#include "cpu/inorder/cpu.h"
#include "core/decoder.h"

namespace riscv {

class ExtensionTest : public ::testing::Test {
protected:
    void SetUp() override {
        simulator = std::make_unique<Simulator>(8192);
    }

    std::unique_ptr<Simulator> simulator;
};

// 测试M扩展 - 乘法指令
TEST_F(ExtensionTest, MulInstruction) {
    // MUL x3, x1, x2 = 0000001 00010 00001 000 00011 0110011
    // funct7=1, rs2=2, rs1=1, funct3=0, rd=3, opcode=0x33
    uint32_t instruction = 0x02208133;  // 错误编码
    // 正确编码: 0000001 00010 00001 000 00011 0110011 = 0x02208/1B3
    instruction = 0x022081B3;
    
    simulator->setRegister(1, 15);   // x1 = 15
    simulator->setRegister(2, 7);    // x2 = 7
    
    // 手动解码和执行
    Decoder decoder;
    auto decoded = decoder.decode(instruction);
    
    EXPECT_EQ(decoded.opcode, Opcode::OP);
    EXPECT_EQ(decoded.funct7, Funct7::M_EXT);
    EXPECT_EQ(decoded.funct3, Funct3::MUL);
    
    // 加载指令到内存并执行
    std::vector<uint8_t> program = {
        static_cast<uint8_t>(instruction & 0xFF),
        static_cast<uint8_t>((instruction >> 8) & 0xFF),
        static_cast<uint8_t>((instruction >> 16) & 0xFF),
        static_cast<uint8_t>((instruction >> 24) & 0xFF)
    };
    
    simulator->loadProgramFromBytes(program, 0x1000);
    simulator->setRegister(1, 15);
    simulator->setRegister(2, 7);
    simulator->step();
    
    EXPECT_EQ(simulator->getRegister(3), 105);  // 15 * 7 = 105
}

// 测试M扩展 - 除法指令
TEST_F(ExtensionTest, DivInstruction) {
    // DIV x3, x1, x2 = 0000001 00010 00001 100 00011 0110011
    uint32_t instruction = 0x0220C1B3;  // div x3, x1, x2
    
    std::vector<uint8_t> program = {
        static_cast<uint8_t>(instruction & 0xFF),
        static_cast<uint8_t>((instruction >> 8) & 0xFF),
        static_cast<uint8_t>((instruction >> 16) & 0xFF),
        static_cast<uint8_t>((instruction >> 24) & 0xFF)
    };
    
    simulator->loadProgramFromBytes(program, 0x1000);
    simulator->setRegister(1, 15);
    simulator->setRegister(2, 7);
    simulator->step();
    
    EXPECT_EQ(simulator->getRegister(3), 2);  // 15 / 7 = 2
}

// 测试M扩展 - 求余指令
TEST_F(ExtensionTest, RemInstruction) {
    // REM x3, x1, x2 = 0000001 00010 00001 110 00011 0110011
    uint32_t instruction = 0x0220E1B3;  // rem x3, x1, x2
    
    std::vector<uint8_t> program = {
        static_cast<uint8_t>(instruction & 0xFF),
        static_cast<uint8_t>((instruction >> 8) & 0xFF),
        static_cast<uint8_t>((instruction >> 16) & 0xFF),
        static_cast<uint8_t>((instruction >> 24) & 0xFF)
    };
    
    simulator->loadProgramFromBytes(program, 0x1000);
    simulator->setRegister(1, 15);
    simulator->setRegister(2, 7);
    simulator->step();
    
    EXPECT_EQ(simulator->getRegister(3), 1);  // 15 % 7 = 1
}

// 测试除零处理
TEST_F(ExtensionTest, DivisionByZero) {
    // DIV x3, x1, x2 (x2 = 0)
    uint32_t instruction = 0x0220C1B3;  // div x3, x1, x2
    
    std::vector<uint8_t> program = {
        static_cast<uint8_t>(instruction & 0xFF),
        static_cast<uint8_t>((instruction >> 8) & 0xFF),
        static_cast<uint8_t>((instruction >> 16) & 0xFF),
        static_cast<uint8_t>((instruction >> 24) & 0xFF)
    };
    
    simulator->loadProgramFromBytes(program, 0x1000);
    simulator->setRegister(1, 15);
    simulator->setRegister(2, 0);  // 除零
    simulator->step();
    
    EXPECT_EQ(simulator->getRegister(3), 0xFFFFFFFF);  // 除零返回-1
}

// 测试浮点寄存器访问
TEST_F(ExtensionTest, FloatRegisterAccess) {
    // 直接测试CPU的浮点寄存器功能
    auto memory = std::make_shared<Memory>(4096);
    CPU cpu(memory);
    
    // 测试浮点寄存器设置和读取
    cpu.setFPRegisterFloat(1, 3.14f);
    EXPECT_FLOAT_EQ(cpu.getFPRegisterFloat(1), 3.14f);
    
    cpu.setFPRegisterFloat(2, -2.5f);
    EXPECT_FLOAT_EQ(cpu.getFPRegisterFloat(2), -2.5f);
    
    // 测试边界值
    cpu.setFPRegisterFloat(3, 0.0f);
    EXPECT_FLOAT_EQ(cpu.getFPRegisterFloat(3), 0.0f);
}

// 测试指令验证
TEST_F(ExtensionTest, InstructionValidation) {
    Decoder decoder;
    
    // 测试M扩展指令验证
    uint32_t m_instruction = 0x022081B3;  // mul x3, x1, x2
    auto decoded = decoder.decode(m_instruction, static_cast<uint32_t>(Extension::I) | static_cast<uint32_t>(Extension::M));
    EXPECT_EQ(decoded.funct7, Funct7::M_EXT);
    
    // 测试当M扩展未启用时应该抛出异常
    EXPECT_THROW(
        decoder.decode(m_instruction, static_cast<uint32_t>(Extension::I)),
        IllegalInstructionException
    );
}

} // namespace riscv