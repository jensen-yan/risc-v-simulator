#include <gtest/gtest.h>
#include "cpu/inorder/cpu.h"
#include "core/memory.h"
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
    
    // 辅助方法：创建R-Type指令
    uint32_t createRType(Opcode opcode, RegNum rd, RegNum rs1, RegNum rs2, Funct3 funct3, Funct7 funct7) {
        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
        inst |= (rd & 0x1F) << 7;                               // rd [11:7]
        inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;    // funct3 [14:12]
        inst |= (rs1 & 0x1F) << 15;                             // rs1 [19:15]
        inst |= (rs2 & 0x1F) << 20;                             // rs2 [24:20]
        inst |= (static_cast<uint32_t>(funct7) & 0x7F) << 25;   // funct7 [31:25]
        return inst;
    }
    
    // 辅助方法：创建S-Type指令
    uint32_t createSType(Opcode opcode, RegNum rs1, RegNum rs2, int32_t imm, Funct3 funct3) {
        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
        inst |= (imm & 0x1F) << 7;                              // imm[4:0] [11:7]
        inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;    // funct3 [14:12]
        inst |= (rs1 & 0x1F) << 15;                             // rs1 [19:15]
        inst |= (rs2 & 0x1F) << 20;                             // rs2 [24:20]
        inst |= ((imm >> 5) & 0x7F) << 25;                      // imm[11:5] [31:25]
        return inst;
    }
    
    // 辅助方法：创建U-Type指令
    uint32_t createUType(Opcode opcode, RegNum rd, int32_t imm) {
        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
        inst |= (rd & 0x1F) << 7;                               // rd [11:7]
        inst |= (imm & 0xFFFFF000);                             // imm[31:12] [31:12]
        return inst;
    }
    
    // 辅助方法：创建J-Type指令
    uint32_t createJType(Opcode opcode, RegNum rd, int32_t imm) {
        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
        inst |= (rd & 0x1F) << 7;                               // rd [11:7]
        
        // J-Type立即数编码比较复杂
        inst |= ((imm >> 12) & 0xFF) << 12;                     // imm[19:12] [19:12]
        inst |= ((imm >> 11) & 0x1) << 20;                      // imm[11] [20]
        inst |= ((imm >> 1) & 0x3FF) << 21;                     // imm[10:1] [30:21]
        inst |= ((imm >> 20) & 0x1) << 31;                      // imm[20] [31]
        return inst;
    }
    
    // 辅助方法：创建B-Type指令
    uint32_t createBType(Opcode opcode, RegNum rs1, RegNum rs2, int32_t imm, Funct3 funct3) {
        uint32_t inst = 0;
        inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
        inst |= ((imm >> 11) & 0x1) << 7;                       // imm[11] [7]
        inst |= ((imm >> 1) & 0xF) << 8;                        // imm[4:1] [11:8]
        inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;    // funct3 [14:12]
        inst |= (rs1 & 0x1F) << 15;                             // rs1 [19:15]
        inst |= (rs2 & 0x1F) << 20;                             // rs2 [24:20]
        inst |= ((imm >> 5) & 0x3F) << 25;                      // imm[10:5] [30:25]
        inst |= ((imm >> 12) & 0x1) << 31;                      // imm[12] [31]
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

TEST_F(CPUTest, ADD_Instruction) {
    // 设置寄存器值
    cpu->setRegister(1, 15);
    cpu->setRegister(2, 25);
    
    // 测试 ADD x3, x1, x2  (15 + 25 = 40)
    uint32_t inst = createRType(Opcode::OP, 3, 1, 2, Funct3::ADD_SUB, Funct7::NORMAL);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(3), 40);
    EXPECT_EQ(cpu->getPC(), 4);
}

TEST_F(CPUTest, SUB_Instruction) {
    // 设置寄存器值
    cpu->setRegister(1, 100);
    cpu->setRegister(2, 30);
    
    // 测试 SUB x3, x1, x2  (100 - 30 = 70)
    uint32_t inst = createRType(Opcode::OP, 3, 1, 2, Funct3::ADD_SUB, Funct7::SUB_SRA);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(3), 70);
}

TEST_F(CPUTest, XOR_Instruction) {
    // 设置寄存器值
    cpu->setRegister(1, 0xF0F0F0F0);
    cpu->setRegister(2, 0x0F0F0F0F);
    
    // 测试 XOR x3, x1, x2  (0xF0F0F0F0 ^ 0x0F0F0F0F = 0xFFFFFFFF)
    uint32_t inst = createRType(Opcode::OP, 3, 1, 2, Funct3::XOR, Funct7::NORMAL);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(3), 0xFFFFFFFF);
}

TEST_F(CPUTest, SLT_Instruction) {
    // 设置寄存器值
    cpu->setRegister(1, static_cast<uint64_t>(-10)); // 负数
    cpu->setRegister(2, 5);
    
    // 测试 SLT x3, x1, x2  (-10 < 5, 结果应该是1)
    uint32_t inst = createRType(Opcode::OP, 3, 1, 2, Funct3::SLT, Funct7::NORMAL);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(3), 1);
}

TEST_F(CPUTest, SLTU_Instruction) {
    // 设置寄存器值
    cpu->setRegister(1, static_cast<uint64_t>(-10)); // 作为无符号数很大
    cpu->setRegister(2, 5);
    
    // 测试 SLTU x3, x1, x2  (18446744073709551606 < 5, 结果应该是0)
    uint32_t inst = createRType(Opcode::OP, 3, 1, 2, Funct3::SLTU, Funct7::NORMAL);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(3), 0);
}

TEST_F(CPUTest, SLL_Instruction) {
    // 设置寄存器值
    cpu->setRegister(1, 7);
    cpu->setRegister(2, 3);
    
    // 测试 SLL x3, x1, x2  (7 << 3 = 56)
    uint32_t inst = createRType(Opcode::OP, 3, 1, 2, Funct3::SLL, Funct7::NORMAL);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(3), 56);
}

TEST_F(CPUTest, LoadWord_Instruction) {
    // 在内存地址100处写入数据
    memory->writeWord(100, 0x12345678);
    
    // 设置基址寄存器
    cpu->setRegister(1, 80);  // 基址80
    
    // 测试 LW x2, 20(x1)  (从地址80+20=100加载字)
    uint32_t inst = createIType(Opcode::LOAD, 2, 1, 20, Funct3::LW);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    EXPECT_EQ(cpu->getRegister(2), 0x12345678);
}

TEST_F(CPUTest, LoadByte_Instruction) {
    // 在内存地址100处写入字节数据
    memory->writeByte(100, 0x80); // 负数字节
    
    // 设置基址寄存器
    cpu->setRegister(1, 100);
    
    // 测试 LB x2, 0(x1)  (从地址100加载有符号字节)
    uint32_t inst = createIType(Opcode::LOAD, 2, 1, 0, Funct3::LB);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 0x80应该符号扩展为0xFFFFFFFFFFFFFF80 (64位)
    EXPECT_EQ(cpu->getRegister(2), 0xFFFFFFFFFFFFFF80ULL);
}

TEST_F(CPUTest, LoadByteUnsigned_Instruction) {
    // 在内存地址100处写入字节数据
    memory->writeByte(100, 0x80);
    
    // 设置基址寄存器
    cpu->setRegister(1, 100);
    
    // 测试 LBU x2, 0(x1)  (从地址100加载无符号字节)
    uint32_t inst = createIType(Opcode::LOAD, 2, 1, 0, Funct3::LBU);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 0x80应该无符号扩展为0x00000080
    EXPECT_EQ(cpu->getRegister(2), 0x00000080);
}

TEST_F(CPUTest, StoreWord_Instruction) {
    // 设置要存储的值
    cpu->setRegister(1, 200);         // 基址
    cpu->setRegister(2, 0xABCDEF12);  // 要存储的值
    
    // 测试 SW x2, 4(x1)  (将x2存储到地址200+4=204)
    uint32_t inst = createSType(Opcode::STORE, 1, 2, 4, Funct3::SW);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 验证内存中的值
    EXPECT_EQ(memory->readWord(204), 0xABCDEF12);
}

TEST_F(CPUTest, StoreByte_Instruction) {
    // 设置要存储的值
    cpu->setRegister(1, 200);       // 基址
    cpu->setRegister(2, 0x12345678); // 要存储的值(只存储低8位)
    
    // 测试 SB x2, 8(x1)  (将x2的低8位存储到地址200+8=208)
    uint32_t inst = createSType(Opcode::STORE, 1, 2, 8, Funct3::SB);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 验证内存中的值(只有低8位0x78被存储)
    EXPECT_EQ(memory->readByte(208), 0x78);
}

TEST_F(CPUTest, LoadStore_Combined) {
    // 组合测试：存储然后加载
    cpu->setRegister(1, 300);        // 基址
    cpu->setRegister(2, 0x9876FEDC); // 要存储的值
    
    // 1. 存储：SW x2, 0(x1)
    uint32_t store_inst = createSType(Opcode::STORE, 1, 2, 0, Funct3::SW);
    memory->writeWord(0, store_inst);
    cpu->step();
    
    // 重置x2寄存器
    cpu->setRegister(2, 0);
    
    // 2. 加载：LW x3, 0(x1)
    uint32_t load_inst = createIType(Opcode::LOAD, 3, 1, 0, Funct3::LW);
    memory->writeWord(4, load_inst);
    cpu->step();
    
    // 验证加载的值 (32位数据在64位寄存器中需要符号扩展)
    // 0x9876FEDC 作为有符号数为负数，需要符号扩展到64位
    EXPECT_EQ(cpu->getRegister(3), 0xFFFFFFFF9876FEDCULL);
    EXPECT_EQ(cpu->getRegister(2), 0);  // x2应该保持为0
}

TEST_F(CPUTest, LUI_Instruction) {
    // 测试 LUI x1, 0x12345  (将0x12345加载到高20位)
    uint32_t inst = createUType(Opcode::LUI, 1, 0x12345000);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // LUI应该将立即数放到高20位，低12位为0
    EXPECT_EQ(cpu->getRegister(1), 0x12345000);
    EXPECT_EQ(cpu->getPC(), 4);
}

TEST_F(CPUTest, AUIPC_Instruction) {
    // 设置PC为100
    cpu->setPC(100);
    
    // 测试 AUIPC x2, 0x1000  (PC + 0x1000000)
    uint32_t inst = createUType(Opcode::AUIPC, 2, 0x1000000);
    memory->writeWord(100, inst);
    
    cpu->step();
    
    // AUIPC应该将PC + 立即数存入寄存器
    EXPECT_EQ(cpu->getRegister(2), 100 + 0x1000000);
    EXPECT_EQ(cpu->getPC(), 104);
}

TEST_F(CPUTest, JAL_Instruction) {
    // 设置PC为0
    cpu->setPC(0);
    
    // 测试 JAL x1, 20  (跳转到PC+20，保存返回地址)
    uint32_t inst = createJType(Opcode::JAL, 1, 20);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 检查返回地址和PC
    EXPECT_EQ(cpu->getRegister(1), 4);   // 返回地址是PC+4
    EXPECT_EQ(cpu->getPC(), 20);         // PC应该跳转到0+20
}

TEST_F(CPUTest, BEQ_Instruction_Taken) {
    // 设置相等的寄存器值
    cpu->setRegister(1, 42);
    cpu->setRegister(2, 42);
    cpu->setPC(0);
    
    // 测试 BEQ x1, x2, 16  (如果相等则跳转)
    uint32_t inst = createBType(Opcode::BRANCH, 1, 2, 16, Funct3::BEQ);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 应该跳转
    EXPECT_EQ(cpu->getPC(), 16);
}

TEST_F(CPUTest, BEQ_Instruction_NotTaken) {
    // 设置不相等的寄存器值
    cpu->setRegister(1, 42);
    cpu->setRegister(2, 43);
    cpu->setPC(0);
    
    // 测试 BEQ x1, x2, 16  (如果相等则跳转)
    uint32_t inst = createBType(Opcode::BRANCH, 1, 2, 16, Funct3::BEQ);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 不应该跳转，PC正常递增
    EXPECT_EQ(cpu->getPC(), 4);
}

TEST_F(CPUTest, BLT_Instruction) {
    // 设置寄存器值：-10 < 5
    cpu->setRegister(1, static_cast<uint64_t>(-10));
    cpu->setRegister(2, 5);
    cpu->setPC(0);
    
    // 测试 BLT x1, x2, 12  (有符号比较)
    uint32_t inst = createBType(Opcode::BRANCH, 1, 2, 12, Funct3::BLT);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 应该跳转（-10 < 5）
    EXPECT_EQ(cpu->getPC(), 12);
}

TEST_F(CPUTest, BLTU_Instruction) {
    // 设置寄存器值：作为无符号数，-10实际是很大的正数
    cpu->setRegister(1, static_cast<uint64_t>(-10));
    cpu->setRegister(2, 5);
    cpu->setPC(0);
    
    // 测试 BLTU x1, x2, 12  (无符号比较)
    uint32_t inst = createBType(Opcode::BRANCH, 1, 2, 12, Funct3::BLTU);
    memory->writeWord(0, inst);
    
    cpu->step();
    
    // 不应该跳转（作为无符号数，-10 > 5）
    EXPECT_EQ(cpu->getPC(), 4);
}

TEST_F(CPUTest, SystemCSR_CSRRWAndCSRRS) {
    // csrrw x2, mstatus(0x300), x1 : x2 <- old(mstatus), mstatus <- x1
    cpu->setRegister(1, 0x55);
    uint32_t csrrw_inst = createIType(Opcode::SYSTEM, 2, 1, 0x300, static_cast<Funct3>(0b001));
    memory->writeWord(0, csrrw_inst);

    // csrrs x4, mstatus(0x300), x3 : x4 <- old(mstatus), mstatus <- old | x3
    cpu->setRegister(3, 0x02);
    uint32_t csrrs_set_inst = createIType(Opcode::SYSTEM, 4, 3, 0x300, static_cast<Funct3>(0b010));
    memory->writeWord(4, csrrs_set_inst);

    // csrr x5, mstatus(0x300) = csrrs x5, mstatus, x0
    uint32_t csrr_inst = createIType(Opcode::SYSTEM, 5, 0, 0x300, static_cast<Funct3>(0b010));
    memory->writeWord(8, csrr_inst);

    cpu->step();
    EXPECT_EQ(cpu->getRegister(2), 0x0);
    EXPECT_EQ(cpu->getPC(), 4);

    cpu->step();
    EXPECT_EQ(cpu->getRegister(4), 0x55);
    EXPECT_EQ(cpu->getPC(), 8);

    cpu->step();
    EXPECT_EQ(cpu->getRegister(5), 0x57);
    EXPECT_EQ(cpu->getPC(), 12);
}

TEST_F(CPUTest, SystemCSR_CSRRShouldOverwriteRdValue) {
    // 复现 rv64ui-p-add 的关键点：csrr a0, mhartid 必须覆盖 a0 旧值
    cpu->setRegister(10, 0x80000000ULL);
    uint32_t csrr_mhartid = createIType(Opcode::SYSTEM, 10, 0, 0xF14, static_cast<Funct3>(0b010));
    memory->writeWord(0, csrr_mhartid);

    cpu->step();

    EXPECT_EQ(cpu->getRegister(10), 0x0);
    EXPECT_EQ(cpu->getPC(), 4);
}
