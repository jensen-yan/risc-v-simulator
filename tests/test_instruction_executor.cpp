#include <gtest/gtest.h>
#include "core/instruction_executor.h"
#include "core/decoder.h"
#include "core/memory.h"
#include "common/types.h"
#include <memory>
#include <cmath>

// 如果没有异常类定义，创建一个简单的
#ifndef ILLEGAL_INSTRUCTION_EXCEPTION_DEFINED
class IllegalInstructionException : public std::exception {
private:
    std::string message_;
public:
    explicit IllegalInstructionException(const std::string& msg) : message_(msg) {}
    const char* what() const noexcept override { return message_.c_str(); }
};
#define ILLEGAL_INSTRUCTION_EXCEPTION_DEFINED
#endif

namespace riscv {

/**
 * InstructionExecutor模块单元测试
 * 当前覆盖率：27.5% -> 目标：75%+
 */
class InstructionExecutorTest : public ::testing::Test {
protected:
    std::shared_ptr<Memory> memory_;
    
    void SetUp() override {
        memory_ = std::make_shared<Memory>(8192);  // 8KB内存
    }
    
    void TearDown() override {
        memory_.reset();
    }
    
    // 辅助函数：创建DecodedInstruction
    DecodedInstruction createDecodedInst(Opcode opcode, Funct3 funct3, Funct7 funct7 = Funct7::NORMAL,
                                       RegNum rd = 0, RegNum rs1 = 0, RegNum rs2 = 0, int32_t imm = 0) {
        DecodedInstruction inst;
        inst.opcode = opcode;
        inst.funct3 = funct3;
        inst.funct7 = funct7;
        inst.rd = rd;
        inst.rs1 = rs1;
        inst.rs2 = rs2;
        inst.imm = imm;
        return inst;
    }
};

// ========== 基本立即数运算测试 ==========

TEST_F(InstructionExecutorTest, ImmediateAddition) {
    auto inst = createDecodedInst(Opcode::OP_IMM, Funct3::ADD_SUB, Funct7::NORMAL, 1, 0, 0, 42);
    uint64_t result = InstructionExecutor::executeImmediateOperation(inst, 10);
    EXPECT_EQ(result, 52) << "ADDI: 10 + 42 应该等于 52";
}

TEST_F(InstructionExecutorTest, ImmediateAdditionNegative) {
    auto inst = createDecodedInst(Opcode::OP_IMM, Funct3::ADD_SUB, Funct7::NORMAL, 1, 0, 0, -20);
    uint64_t result = InstructionExecutor::executeImmediateOperation(inst, 50);
    EXPECT_EQ(result, 30) << "ADDI: 50 + (-20) 应该等于 30";
}

TEST_F(InstructionExecutorTest, ImmediateSetLessThan) {
    auto inst = createDecodedInst(Opcode::OP_IMM, Funct3::SLT, Funct7::NORMAL, 1, 0, 0, 100);
    
    uint64_t result1 = InstructionExecutor::executeImmediateOperation(inst, 50);
    EXPECT_EQ(result1, 1) << "SLTI: 50 < 100 应该返回 1";
    
    uint64_t result2 = InstructionExecutor::executeImmediateOperation(inst, 150);
    EXPECT_EQ(result2, 0) << "SLTI: 150 < 100 应该返回 0";
}

TEST_F(InstructionExecutorTest, ImmediateSetLessThanUnsigned) {
    auto inst = createDecodedInst(Opcode::OP_IMM, Funct3::SLTU, Funct7::NORMAL, 1, 0, 0, 100);
    
    uint64_t result1 = InstructionExecutor::executeImmediateOperation(inst, 50);
    EXPECT_EQ(result1, 1) << "SLTIU: 50 < 100 应该返回 1";
    
    uint64_t result2 = InstructionExecutor::executeImmediateOperation(inst, 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(result2, 0) << "SLTIU: MAX_UINT64 < 100 应该返回 0";
}

TEST_F(InstructionExecutorTest, ImmediateLogicalOperations) {
    // XOR立即数
    auto xor_inst = createDecodedInst(Opcode::OP_IMM, Funct3::XOR, Funct7::NORMAL, 1, 0, 0, 0xFF);
    uint64_t xor_result = InstructionExecutor::executeImmediateOperation(xor_inst, 0xF0);
    EXPECT_EQ(xor_result, 0x0F) << "XORI: 0xF0 ^ 0xFF 应该等于 0x0F";
    
    // OR立即数
    auto or_inst = createDecodedInst(Opcode::OP_IMM, Funct3::OR, Funct7::NORMAL, 1, 0, 0, 0x0F);
    uint64_t or_result = InstructionExecutor::executeImmediateOperation(or_inst, 0xF0);
    EXPECT_EQ(or_result, 0xFF) << "ORI: 0xF0 | 0x0F 应该等于 0xFF";
    
    // AND立即数
    auto and_inst = createDecodedInst(Opcode::OP_IMM, Funct3::AND, Funct7::NORMAL, 1, 0, 0, 0x0F);
    uint64_t and_result = InstructionExecutor::executeImmediateOperation(and_inst, 0xFF);
    EXPECT_EQ(and_result, 0x0F) << "ANDI: 0xFF & 0x0F 应该等于 0x0F";
}

TEST_F(InstructionExecutorTest, ImmediateShiftOperations) {
    // 左移
    auto sll_inst = createDecodedInst(Opcode::OP_IMM, Funct3::SLL, Funct7::NORMAL, 1, 0, 0, 4);
    uint64_t sll_result = InstructionExecutor::executeImmediateOperation(sll_inst, 0x1);
    EXPECT_EQ(sll_result, 0x10) << "SLLI: 1 << 4 应该等于 16";
    
    // 逻辑右移
    auto srl_inst = createDecodedInst(Opcode::OP_IMM, Funct3::SRL_SRA, Funct7::NORMAL, 1, 0, 0, 2);
    uint64_t srl_result = InstructionExecutor::executeImmediateOperation(srl_inst, 0x10);
    EXPECT_EQ(srl_result, 0x4) << "SRLI: 16 >> 2 应该等于 4";
    
    // 算术右移
    auto sra_inst = createDecodedInst(Opcode::OP_IMM, Funct3::SRL_SRA, Funct7::SUB_SRA, 1, 0, 0, 2);
    uint64_t sra_result = InstructionExecutor::executeImmediateOperation(sra_inst, 0xFFFFFFFFFFFFFFFC);
    EXPECT_EQ(sra_result, 0xFFFFFFFFFFFFFFFF) << "SRAI: 负数算术右移应该保持符号位";
}

// ========== 寄存器运算测试 ==========

TEST_F(InstructionExecutorTest, RegisterArithmetic) {
    // ADD
    auto add_inst = createDecodedInst(Opcode::OP, Funct3::ADD_SUB, Funct7::NORMAL, 1, 2, 3);
    uint64_t add_result = InstructionExecutor::executeRegisterOperation(add_inst, 100, 50);
    EXPECT_EQ(add_result, 150) << "ADD: 100 + 50 应该等于 150";
    
    // SUB
    auto sub_inst = createDecodedInst(Opcode::OP, Funct3::ADD_SUB, Funct7::SUB_SRA, 1, 2, 3);
    uint64_t sub_result = InstructionExecutor::executeRegisterOperation(sub_inst, 100, 30);
    EXPECT_EQ(sub_result, 70) << "SUB: 100 - 30 应该等于 70";
}

TEST_F(InstructionExecutorTest, RegisterComparison) {
    // SLT (有符号比较)
    auto slt_inst = createDecodedInst(Opcode::OP, Funct3::SLT, Funct7::NORMAL, 1, 2, 3);
    uint64_t slt_result1 = InstructionExecutor::executeRegisterOperation(slt_inst, -10, 10);
    EXPECT_EQ(slt_result1, 1) << "SLT: -10 < 10 应该返回 1";
    
    uint64_t slt_result2 = InstructionExecutor::executeRegisterOperation(slt_inst, 10, -10);
    EXPECT_EQ(slt_result2, 0) << "SLT: 10 < -10 应该返回 0";
    
    // SLTU (无符号比较)
    auto sltu_inst = createDecodedInst(Opcode::OP, Funct3::SLTU, Funct7::NORMAL, 1, 2, 3);
    uint64_t sltu_result = InstructionExecutor::executeRegisterOperation(sltu_inst, 50, 100);
    EXPECT_EQ(sltu_result, 1) << "SLTU: 50 < 100 应该返回 1";
}

TEST_F(InstructionExecutorTest, RegisterLogical) {
    // XOR
    auto xor_inst = createDecodedInst(Opcode::OP, Funct3::XOR, Funct7::NORMAL, 1, 2, 3);
    uint64_t xor_result = InstructionExecutor::executeRegisterOperation(xor_inst, 0xF0F0, 0x0F0F);
    EXPECT_EQ(xor_result, 0xFFFF) << "XOR: 0xF0F0 ^ 0x0F0F 应该等于 0xFFFF";
    
    // OR
    auto or_inst = createDecodedInst(Opcode::OP, Funct3::OR, Funct7::NORMAL, 1, 2, 3);
    uint64_t or_result = InstructionExecutor::executeRegisterOperation(or_inst, 0xF000, 0x0F00);
    EXPECT_EQ(or_result, 0xFF00) << "OR: 0xF000 | 0x0F00 应该等于 0xFF00";
    
    // AND
    auto and_inst = createDecodedInst(Opcode::OP, Funct3::AND, Funct7::NORMAL, 1, 2, 3);
    uint64_t and_result = InstructionExecutor::executeRegisterOperation(and_inst, 0xFF00, 0xF0F0);
    EXPECT_EQ(and_result, 0xF000) << "AND: 0xFF00 & 0xF0F0 应该等于 0xF000";
}

TEST_F(InstructionExecutorTest, RegisterShift) {
    // SLL
    auto sll_inst = createDecodedInst(Opcode::OP, Funct3::SLL, Funct7::NORMAL, 1, 2, 3);
    uint64_t sll_result = InstructionExecutor::executeRegisterOperation(sll_inst, 0x1, 8);
    EXPECT_EQ(sll_result, 0x100) << "SLL: 1 << 8 应该等于 256";
    
    // SRL
    auto srl_inst = createDecodedInst(Opcode::OP, Funct3::SRL_SRA, Funct7::NORMAL, 1, 2, 3);
    uint64_t srl_result = InstructionExecutor::executeRegisterOperation(srl_inst, 0x100, 4);
    EXPECT_EQ(srl_result, 0x10) << "SRL: 256 >> 4 应该等于 16";
    
    // SRA
    auto sra_inst = createDecodedInst(Opcode::OP, Funct3::SRL_SRA, Funct7::SUB_SRA, 1, 2, 3);
    uint64_t sra_result = InstructionExecutor::executeRegisterOperation(sra_inst, 0xFFFFFFFFFFFFFFF0, 4);
    EXPECT_EQ(sra_result, 0xFFFFFFFFFFFFFFFF) << "SRA: 负数算术右移应该保持符号位";
}

// ========== 分支条件测试 ==========

TEST_F(InstructionExecutorTest, BranchConditions) {
    // BEQ
    auto beq_inst = createDecodedInst(Opcode::BRANCH, Funct3::BEQ, Funct7::NORMAL, 0, 1, 2);
    EXPECT_TRUE(InstructionExecutor::evaluateBranchCondition(beq_inst, 100, 100)) << "BEQ: 相等时应该返回true";
    EXPECT_FALSE(InstructionExecutor::evaluateBranchCondition(beq_inst, 100, 50)) << "BEQ: 不等时应该返回false";
    
    // BNE
    auto bne_inst = createDecodedInst(Opcode::BRANCH, Funct3::BNE, Funct7::NORMAL, 0, 1, 2);
    EXPECT_FALSE(InstructionExecutor::evaluateBranchCondition(bne_inst, 100, 100)) << "BNE: 相等时应该返回false";
    EXPECT_TRUE(InstructionExecutor::evaluateBranchCondition(bne_inst, 100, 50)) << "BNE: 不等时应该返回true";
    
    // BLT
    auto blt_inst = createDecodedInst(Opcode::BRANCH, Funct3::BLT, Funct7::NORMAL, 0, 1, 2);
    EXPECT_TRUE(InstructionExecutor::evaluateBranchCondition(blt_inst, -10, 10)) << "BLT: -10 < 10 应该返回true";
    EXPECT_FALSE(InstructionExecutor::evaluateBranchCondition(blt_inst, 10, -10)) << "BLT: 10 < -10 应该返回false";
    
    // BGE
    auto bge_inst = createDecodedInst(Opcode::BRANCH, Funct3::BGE, Funct7::NORMAL, 0, 1, 2);
    EXPECT_TRUE(InstructionExecutor::evaluateBranchCondition(bge_inst, 10, 10)) << "BGE: 10 >= 10 应该返回true";
    EXPECT_FALSE(InstructionExecutor::evaluateBranchCondition(bge_inst, 5, 10)) << "BGE: 5 >= 10 应该返回false";
    
    // BLTU
    auto bltu_inst = createDecodedInst(Opcode::BRANCH, Funct3::BLTU, Funct7::NORMAL, 0, 1, 2);
    EXPECT_TRUE(InstructionExecutor::evaluateBranchCondition(bltu_inst, 50, 100)) << "BLTU: 50 < 100 应该返回true";
    EXPECT_FALSE(InstructionExecutor::evaluateBranchCondition(bltu_inst, 100, 50)) << "BLTU: 100 < 50 应该返回false";
    
    // BGEU
    auto bgeu_inst = createDecodedInst(Opcode::BRANCH, Funct3::BGEU, Funct7::NORMAL, 0, 1, 2);
    EXPECT_TRUE(InstructionExecutor::evaluateBranchCondition(bgeu_inst, 100, 50)) << "BGEU: 100 >= 50 应该返回true";
    EXPECT_FALSE(InstructionExecutor::evaluateBranchCondition(bgeu_inst, 50, 100)) << "BGEU: 50 >= 100 应该返回false";
}

// ========== 跳转目标计算测试 ==========

TEST_F(InstructionExecutorTest, JumpTargetCalculation) {
    // JAL
    auto jal_inst = createDecodedInst(Opcode::JAL, Funct3::ADD_SUB, Funct7::NORMAL, 1, 0, 0, 1000);
    uint64_t jal_target = InstructionExecutor::calculateJumpTarget(jal_inst, 0x1000);
    EXPECT_EQ(jal_target, 0x1000 + 1000) << "JAL目标地址计算错误";
    
    // JALR
    auto jalr_inst = createDecodedInst(Opcode::JALR, Funct3::ADD_SUB, Funct7::NORMAL, 1, 2, 0, 4);
    uint64_t jalr_target = InstructionExecutor::calculateJumpAndLinkTarget(jalr_inst, 0x1000, 0x2000);
    EXPECT_EQ(jalr_target, 0x2004 & 0xFFFFFFFFFFFFFFFE) << "JALR目标地址应该对齐到偶数";
}

// ========== 内存操作测试 ==========

TEST_F(InstructionExecutorTest, MemoryLoad) {
    // 准备测试数据
    memory_->writeByte(0x1000, 0xFF);
    memory_->writeHalfWord(0x1002, 0xFFFF);
    memory_->writeWord(0x1004, 0xFFFFFFFF);
    memory_->write64(0x1008, 0xFFFFFFFFFFFFFFFF);
    
    // LB (加载字节，符号扩展)
    uint64_t lb_result = InstructionExecutor::loadFromMemory(memory_, 0x1000, Funct3::LB);
    EXPECT_EQ(lb_result, 0xFFFFFFFFFFFFFFFF) << "LB应该符号扩展0xFF到全1";
    
    // LBU (加载字节，零扩展)
    uint64_t lbu_result = InstructionExecutor::loadFromMemory(memory_, 0x1000, Funct3::LBU);
    EXPECT_EQ(lbu_result, 0xFF) << "LBU应该零扩展0xFF";
    
    // LH (加载半字，符号扩展)
    uint64_t lh_result = InstructionExecutor::loadFromMemory(memory_, 0x1002, Funct3::LH);
    EXPECT_EQ(lh_result, 0xFFFFFFFFFFFFFFFF) << "LH应该符号扩展0xFFFF到全1";
    
    // LHU (加载半字，零扩展)
    uint64_t lhu_result = InstructionExecutor::loadFromMemory(memory_, 0x1002, Funct3::LHU);
    EXPECT_EQ(lhu_result, 0xFFFF) << "LHU应该零扩展0xFFFF";
    
    // LW (加载字，符号扩展)
    uint64_t lw_result = InstructionExecutor::loadFromMemory(memory_, 0x1004, Funct3::LW);
    EXPECT_EQ(lw_result, 0xFFFFFFFFFFFFFFFF) << "LW应该符号扩展0xFFFFFFFF到全1";
    
    // LWU (加载字，零扩展) - RV64I特有
    uint64_t lwu_result = InstructionExecutor::loadFromMemory(memory_, 0x1004, Funct3::LWU);
    EXPECT_EQ(lwu_result, 0xFFFFFFFF) << "LWU应该零扩展0xFFFFFFFF";
    
    // LD (加载双字) - RV64I特有
    uint64_t ld_result = InstructionExecutor::loadFromMemory(memory_, 0x1008, Funct3::LD);
    EXPECT_EQ(ld_result, 0xFFFFFFFFFFFFFFFF) << "LD应该加载完整的64位值";
}

TEST_F(InstructionExecutorTest, MemoryStore) {
    // SB (存储字节)
    InstructionExecutor::storeToMemory(memory_, 0x1000, 0x12345678, Funct3::SB);
    EXPECT_EQ(memory_->readByte(0x1000), 0x78) << "SB应该只存储低8位";
    
    // SH (存储半字)
    InstructionExecutor::storeToMemory(memory_, 0x1002, 0x12345678, Funct3::SH);
    EXPECT_EQ(memory_->readHalfWord(0x1002), 0x5678) << "SH应该只存储低16位";
    
    // SW (存储字)
    InstructionExecutor::storeToMemory(memory_, 0x1004, 0x123456789ABCDEF0, Funct3::SW);
    EXPECT_EQ(memory_->readWord(0x1004), 0x9ABCDEF0) << "SW应该只存储低32位";
    
    // SD (存储双字) - RV64I特有
    InstructionExecutor::storeToMemory(memory_, 0x1008, 0x123456789ABCDEF0, Funct3::SD);
    EXPECT_EQ(memory_->read64(0x1008), 0x123456789ABCDEF0) << "SD应该存储完整的64位值";
}

// ========== 上位立即数指令测试 ==========

TEST_F(InstructionExecutorTest, UpperImmediateInstructions) {
    // LUI
    auto lui_inst = createDecodedInst(Opcode::LUI, Funct3::ADD_SUB, Funct7::NORMAL, 1, 0, 0, 0x12345000);
    uint64_t lui_result = InstructionExecutor::executeUpperImmediate(lui_inst, 0x1000);
    EXPECT_EQ(lui_result, 0x12345000) << "LUI应该直接返回立即数";
    
    // AUIPC
    auto auipc_inst = createDecodedInst(Opcode::AUIPC, Funct3::ADD_SUB, Funct7::NORMAL, 1, 0, 0, 0x12345000);
    uint64_t auipc_result = InstructionExecutor::executeUpperImmediate(auipc_inst, 0x1000);
    EXPECT_EQ(auipc_result, 0x1000 + 0x12345000) << "AUIPC应该是PC加立即数";
}

// ========== RV64I 32位运算测试 ==========

TEST_F(InstructionExecutorTest, Immediate32BitOperations) {
    // ADDIW (32位立即数加法)
    auto addiw_inst = createDecodedInst(Opcode::OP_IMM_32, Funct3::ADD_SUB, Funct7::NORMAL, 1, 0, 0, 42);
    uint64_t addiw_result = InstructionExecutor::executeImmediateOperation32(addiw_inst, 0xFFFFFFFF80000010);
    EXPECT_EQ(addiw_result, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000010 + 42)))) << "ADDIW应该进行32位运算并符号扩展";
    
    // SLLIW (32位立即数左移)
    auto slliw_inst = createDecodedInst(Opcode::OP_IMM_32, Funct3::SLL, Funct7::NORMAL, 1, 0, 0, 4);
    uint64_t slliw_result = InstructionExecutor::executeImmediateOperation32(slliw_inst, 0xFFFFFFFF80000001);
    EXPECT_EQ(slliw_result, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000001) << 4))) << "SLLIW应该进行32位左移并符号扩展";
    
    // SRLIW (32位立即数逻辑右移)
    auto srliw_inst = createDecodedInst(Opcode::OP_IMM_32, Funct3::SRL_SRA, Funct7::NORMAL, 1, 0, 0, 2);
    uint64_t srliw_result = InstructionExecutor::executeImmediateOperation32(srliw_inst, 0xFFFFFFFF80000008);
    // 应该将0x80000008作为32位无符号数右移，然后符号扩展结果
    EXPECT_EQ(srliw_result, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000008U >> 2)))) << "SRLIW应该进行32位逻辑右移并符号扩展";
    
    // SRAIW (32位立即数算术右移)
    auto sraiw_inst = createDecodedInst(Opcode::OP_IMM_32, Funct3::SRL_SRA, Funct7::SUB_SRA, 1, 0, 0, 2);
    uint64_t sraiw_result = InstructionExecutor::executeImmediateOperation32(sraiw_inst, 0xFFFFFFFF80000008);
    EXPECT_EQ(sraiw_result, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000008) >> 2))) << "SRAIW应该进行32位算术右移并符号扩展";
}

TEST_F(InstructionExecutorTest, Register32BitOperations) {
    // ADDW (32位寄存器加法)
    auto addw_inst = createDecodedInst(Opcode::OP_32, Funct3::ADD_SUB, Funct7::NORMAL, 1, 2, 3);
    uint64_t addw_result = InstructionExecutor::executeRegisterOperation32(addw_inst, 0xFFFFFFFF80000010, 0xFFFFFFFF7FFFFFF0);
    EXPECT_EQ(addw_result, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000010 + 0x7FFFFFF0)))) << "ADDW应该进行32位加法并符号扩展";
    
    // SUBW (32位寄存器减法)
    auto subw_inst = createDecodedInst(Opcode::OP_32, Funct3::ADD_SUB, Funct7::SUB_SRA, 1, 2, 3);
    uint64_t subw_result = InstructionExecutor::executeRegisterOperation32(subw_inst, 0xFFFFFFFF80000020, 0xFFFFFFFF80000010);
    EXPECT_EQ(subw_result, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000020 - 0x80000010)))) << "SUBW应该进行32位减法并符号扩展";
    
    // SLLW (32位寄存器左移)
    auto sllw_inst = createDecodedInst(Opcode::OP_32, Funct3::SLL, Funct7::NORMAL, 1, 2, 3);
    uint64_t sllw_result = InstructionExecutor::executeRegisterOperation32(sllw_inst, 0xFFFFFFFF80000001, 4);
    EXPECT_EQ(sllw_result, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x80000001) << 4))) << "SLLW应该进行32位左移并符号扩展";
}

// ========== M扩展指令测试 ==========

TEST_F(InstructionExecutorTest, MExtensionMultiplication) {
    // MUL (乘法)
    auto mul_inst = createDecodedInst(Opcode::OP, Funct3::MUL, Funct7::M_EXT, 1, 2, 3);
    uint64_t mul_result = InstructionExecutor::executeMExtension(mul_inst, 123, 456);
    EXPECT_EQ(mul_result, 123 * 456) << "MUL: 123 * 456 计算错误";
    
    // MULH (有符号高位乘法)
    auto mulh_inst = createDecodedInst(Opcode::OP, Funct3::MULH, Funct7::M_EXT, 1, 2, 3);
    uint64_t mulh_result = InstructionExecutor::executeMExtension(mulh_inst, 0x8000000000000000, 2);
    // 应该返回乘法高位结果
    EXPECT_NE(mulh_result, 0) << "MULH应该返回非零的高位结果";
    
    // MULHSU (有符号无符号高位乘法)
    auto mulhsu_inst = createDecodedInst(Opcode::OP, Funct3::MULHSU, Funct7::M_EXT, 1, 2, 3);
    uint64_t mulhsu_result = InstructionExecutor::executeMExtension(mulhsu_inst, -1, 0xFFFFFFFFFFFFFFFF);
    EXPECT_NE(mulhsu_result, 0) << "MULHSU应该返回非零的高位结果";
    
    // MULHU (无符号高位乘法)
    auto mulhu_inst = createDecodedInst(Opcode::OP, Funct3::MULHU, Funct7::M_EXT, 1, 2, 3);
    uint64_t mulhu_result = InstructionExecutor::executeMExtension(mulhu_inst, 0xFFFFFFFFFFFFFFFF, 2);
    EXPECT_EQ(mulhu_result, 1) << "MULHU: UINT64_MAX * 2 的高位应该是1";
}

TEST_F(InstructionExecutorTest, MExtensionDivision) {
    // DIV (有符号除法)
    auto div_inst = createDecodedInst(Opcode::OP, Funct3::DIV, Funct7::M_EXT, 1, 2, 3);
    uint64_t div_result = InstructionExecutor::executeMExtension(div_inst, 100, 3);
    EXPECT_EQ(div_result, static_cast<uint64_t>(static_cast<int64_t>(100) / static_cast<int64_t>(3))) << "DIV: 100 / 3 计算错误";
    
    // 除零测试
    uint64_t div_NORMAL = InstructionExecutor::executeMExtension(div_inst, 100, 0);
    EXPECT_EQ(div_NORMAL, 0xFFFFFFFFFFFFFFFF) << "DIV除零应该返回-1";
    
    // DIVU (无符号除法)
    auto divu_inst = createDecodedInst(Opcode::OP, Funct3::DIVU, Funct7::M_EXT, 1, 2, 3);
    uint64_t divu_result = InstructionExecutor::executeMExtension(divu_inst, 100, 3);
    EXPECT_EQ(divu_result, 100 / 3) << "DIVU: 100 / 3 计算错误";
    
    // REM (有符号求余)
    auto rem_inst = createDecodedInst(Opcode::OP, Funct3::REM, Funct7::M_EXT, 1, 2, 3);
    uint64_t rem_result = InstructionExecutor::executeMExtension(rem_inst, 100, 3);
    EXPECT_EQ(rem_result, static_cast<uint64_t>(static_cast<int64_t>(100) % static_cast<int64_t>(3))) << "REM: 100 % 3 计算错误";
    
    // 求余除零测试
    uint64_t rem_NORMAL = InstructionExecutor::executeMExtension(rem_inst, 100, 0);
    EXPECT_EQ(rem_NORMAL, 100) << "REM除零应该返回被除数";
    
    // REMU (无符号求余)
    auto remu_inst = createDecodedInst(Opcode::OP, Funct3::REMU, Funct7::M_EXT, 1, 2, 3);
    uint64_t remu_result = InstructionExecutor::executeMExtension(remu_inst, 100, 3);
    EXPECT_EQ(remu_result, 100 % 3) << "REMU: 100 % 3 计算错误";
}

// ========== 浮点运算测试 ==========

TEST_F(InstructionExecutorTest, FloatingPointOperations) {
    // FADD.S (单精度加法)
    auto fadd_inst = createDecodedInst(Opcode::OP_FP, Funct3::ADD_SUB, Funct7::FADD_S, 1, 2, 3);
    uint32_t fadd_result = InstructionExecutor::executeFPExtension(fadd_inst, 3.14f, 2.86f);
    float fadd_float = *reinterpret_cast<float*>(&fadd_result);
    EXPECT_NEAR(fadd_float, 6.0f, 0.001f) << "FADD.S: 3.14 + 2.86 应该约等于6.0";
    
    // FSUB.S (单精度减法)
    auto fsub_inst = createDecodedInst(Opcode::OP_FP, Funct3::ADD_SUB, Funct7::FSUB_S, 1, 2, 3);
    uint32_t fsub_result = InstructionExecutor::executeFPExtension(fsub_inst, 5.0f, 2.0f);
    float fsub_float = *reinterpret_cast<float*>(&fsub_result);
    EXPECT_NEAR(fsub_float, 3.0f, 0.001f) << "FSUB.S: 5.0 - 2.0 应该等于3.0";
    
    // FMUL.S (单精度乘法)
    auto fmul_inst = createDecodedInst(Opcode::OP_FP, Funct3::ADD_SUB, Funct7::FMUL_S, 1, 2, 3);
    uint32_t fmul_result = InstructionExecutor::executeFPExtension(fmul_inst, 2.5f, 4.0f);
    float fmul_float = *reinterpret_cast<float*>(&fmul_result);
    EXPECT_NEAR(fmul_float, 10.0f, 0.001f) << "FMUL.S: 2.5 * 4.0 应该等于10.0";
    
    // FDIV.S (单精度除法)
    auto fdiv_inst = createDecodedInst(Opcode::OP_FP, Funct3::ADD_SUB, Funct7::FDIV_S, 1, 2, 3);
    uint32_t fdiv_result = InstructionExecutor::executeFPExtension(fdiv_inst, 10.0f, 2.0f);
    float fdiv_float = *reinterpret_cast<float*>(&fdiv_result);
    EXPECT_NEAR(fdiv_float, 5.0f, 0.001f) << "FDIV.S: 10.0 / 2.0 应该等于5.0";
    
    // 除零测试
    uint32_t fdiv_NORMAL = InstructionExecutor::executeFPExtension(fdiv_inst, 1.0f, 0.0f);
    float fdiv_NORMAL_float = *reinterpret_cast<float*>(&fdiv_NORMAL);
    EXPECT_TRUE(std::isinf(fdiv_NORMAL_float)) << "FDIV.S除零应该返回无穷大";
}

TEST_F(InstructionExecutorTest, FloatingPointComparison) {
    // FEQ.S (浮点相等比较)
    auto feq_inst = createDecodedInst(Opcode::OP_FP, Funct3::FEQ, Funct7::FCMP_S, 1, 2, 3);
    uint32_t feq_result1 = InstructionExecutor::executeFPExtension(feq_inst, 3.14f, 3.14f);
    EXPECT_EQ(feq_result1, 1) << "FEQ.S: 相等的浮点数应该返回1";
    
    uint32_t feq_result2 = InstructionExecutor::executeFPExtension(feq_inst, 3.14f, 2.71f);
    EXPECT_EQ(feq_result2, 0) << "FEQ.S: 不等的浮点数应该返回0";
    
    // FLT.S (浮点小于比较)
    auto flt_inst = createDecodedInst(Opcode::OP_FP, Funct3::FLT, Funct7::FCMP_S, 1, 2, 3);
    uint32_t flt_result1 = InstructionExecutor::executeFPExtension(flt_inst, 2.0f, 3.0f);
    EXPECT_EQ(flt_result1, 1) << "FLT.S: 2.0 < 3.0 应该返回1";
    
    uint32_t flt_result2 = InstructionExecutor::executeFPExtension(flt_inst, 3.0f, 2.0f);
    EXPECT_EQ(flt_result2, 0) << "FLT.S: 3.0 < 2.0 应该返回0";
    
    // FLE.S (浮点小于等于比较)
    auto fle_inst = createDecodedInst(Opcode::OP_FP, Funct3::FLE, Funct7::FCMP_S, 1, 2, 3);
    uint32_t fle_result1 = InstructionExecutor::executeFPExtension(fle_inst, 2.0f, 2.0f);
    EXPECT_EQ(fle_result1, 1) << "FLE.S: 2.0 <= 2.0 应该返回1";
    
    uint32_t fle_result2 = InstructionExecutor::executeFPExtension(fle_inst, 3.0f, 2.0f);
    EXPECT_EQ(fle_result2, 0) << "FLE.S: 3.0 <= 2.0 应该返回0";
}

// ========== 系统指令识别测试 ==========

TEST_F(InstructionExecutorTest, SystemInstructions) {
    // ECALL
    auto ecall_inst = createDecodedInst(Opcode::SYSTEM, Funct3::ECALL_EBREAK, Funct7::NORMAL, 0, 0, 0, 0);
    EXPECT_TRUE(InstructionExecutor::isSystemCall(ecall_inst)) << "应该识别ECALL指令";
    EXPECT_FALSE(InstructionExecutor::isBreakpoint(ecall_inst)) << "ECALL不应该被识别为断点";
    
    // EBREAK
    auto ebreak_inst = createDecodedInst(Opcode::SYSTEM, Funct3::ECALL_EBREAK, Funct7::NORMAL, 0, 0, 0, 1);
    EXPECT_FALSE(InstructionExecutor::isSystemCall(ebreak_inst)) << "EBREAK不应该被识别为系统调用";
    EXPECT_TRUE(InstructionExecutor::isBreakpoint(ebreak_inst)) << "应该识别EBREAK指令";
    
    // MRET
    auto mret_inst = createDecodedInst(Opcode::SYSTEM, Funct3::ECALL_EBREAK, Funct7::NORMAL, 0, 0, 0, 0x302);
    EXPECT_TRUE(InstructionExecutor::isMachineReturn(mret_inst)) << "应该识别MRET指令";
    
    // SRET
    auto sret_inst = createDecodedInst(Opcode::SYSTEM, Funct3::ECALL_EBREAK, Funct7::NORMAL, 0, 0, 0, 0x102);
    EXPECT_TRUE(InstructionExecutor::isSupervisorReturn(sret_inst)) << "应该识别SRET指令";
    
    // URET
    auto uret_inst = createDecodedInst(Opcode::SYSTEM, Funct3::ECALL_EBREAK, Funct7::NORMAL, 0, 0, 0, 0x002);
    EXPECT_TRUE(InstructionExecutor::isUserReturn(uret_inst)) << "应该识别URET指令";
}

// ========== 辅助方法测试 ==========

TEST_F(InstructionExecutorTest, SignExtension) {
    // 8位符号扩展
    int32_t result1 = InstructionExecutor::signExtend(0xFF, 8);
    EXPECT_EQ(result1, -1) << "8位0xFF应该符号扩展为-1";
    
    int32_t result2 = InstructionExecutor::signExtend(0x7F, 8);
    EXPECT_EQ(result2, 127) << "8位0x7F应该符号扩展为127";
    
    // 12位符号扩展
    int32_t result3 = InstructionExecutor::signExtend(0x800, 12);
    EXPECT_LT(result3, 0) << "12位0x800应该符号扩展为负数";
    
    int32_t result4 = InstructionExecutor::signExtend(0x7FF, 12);
    EXPECT_EQ(result4, 2047) << "12位0x7FF应该符号扩展为2047";
    
    // 边界情况测试
    int32_t result5 = InstructionExecutor::signExtend(0x12345678, 0);  // 无效位数
    EXPECT_EQ(result5, static_cast<int32_t>(0x12345678)) << "无效位数应该返回原值";
    
    int32_t result6 = InstructionExecutor::signExtend(0x12345678, 40);  // 超过32位
    EXPECT_EQ(result6, static_cast<int32_t>(0x12345678)) << "超过32位应该返回原值";
}

// ========== 边界条件测试 ==========

TEST_F(InstructionExecutorTest, BoundaryConditions) {
    // 测试各种边界情况的处理，确保程序不会崩溃
    
    // 测试除零情况
    auto div_inst = createDecodedInst(Opcode::OP, Funct3::DIV, Funct7::M_EXT, 1, 2, 3);
    EXPECT_NO_THROW({
        uint64_t result = InstructionExecutor::executeMExtension(div_inst, 100, 0);
        // 除零通常返回特定值（如-1或0）
        (void)result;
    }) << "除零操作应该被正确处理";
    
    // 测试内存边界访问
    EXPECT_NO_THROW({
        uint64_t result = InstructionExecutor::loadFromMemory(memory_, 0, Funct3::LW);
        (void)result;
    }) << "内存边界访问应该被正确处理";
    
    // 测试符号扩展边界情况
    int32_t result1 = InstructionExecutor::signExtend(0x80, 8);
    EXPECT_LT(result1, 0) << "8位最高位为1应该扩展为负数";
    
    int32_t result2 = InstructionExecutor::signExtend(0x7F, 8);
    EXPECT_GT(result2, 0) << "8位最高位为0应该扩展为正数";
}

} // namespace riscv