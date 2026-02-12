#include <gtest/gtest.h>

#include "cpu/ooo/ooo_cpu.h"
#include "core/memory.h"

#include <memory>

namespace riscv {

namespace {

uint32_t createIType(Opcode opcode, RegNum rd, RegNum rs1, int32_t imm, Funct3 funct3) {
    uint32_t inst = 0;
    inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
    inst |= (rd & 0x1F) << 7;                               // rd [11:7]
    inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;    // funct3 [14:12]
    inst |= (rs1 & 0x1F) << 15;                             // rs1 [19:15]
    inst |= (static_cast<uint32_t>(imm) & 0xFFF) << 20;     // imm[11:0] [31:20]
    return inst;
}

uint32_t createJType(Opcode opcode, RegNum rd, int32_t imm) {
    uint32_t inst = 0;
    inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
    inst |= (rd & 0x1F) << 7;                               // rd [11:7]
    inst |= ((static_cast<uint32_t>(imm) >> 12) & 0xFF) << 12; // imm[19:12] [19:12]
    inst |= ((static_cast<uint32_t>(imm) >> 11) & 0x1) << 20;  // imm[11] [20]
    inst |= ((static_cast<uint32_t>(imm) >> 1) & 0x3FF) << 21; // imm[10:1] [30:21]
    inst |= ((static_cast<uint32_t>(imm) >> 20) & 0x1) << 31;  // imm[20] [31]
    return inst;
}

uint32_t createBType(Opcode opcode, RegNum rs1, RegNum rs2, int32_t imm, Funct3 funct3) {
    uint32_t inst = 0;
    inst |= static_cast<uint32_t>(opcode) & 0x7F;           // opcode [6:0]
    inst |= ((static_cast<uint32_t>(imm) >> 11) & 0x1) << 7;   // imm[11] [7]
    inst |= ((static_cast<uint32_t>(imm) >> 1) & 0xF) << 8;    // imm[4:1] [11:8]
    inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;    // funct3 [14:12]
    inst |= (rs1 & 0x1F) << 15;                             // rs1 [19:15]
    inst |= (rs2 & 0x1F) << 20;                             // rs2 [24:20]
    inst |= ((static_cast<uint32_t>(imm) >> 5) & 0x3F) << 25;  // imm[10:5] [30:25]
    inst |= ((static_cast<uint32_t>(imm) >> 12) & 0x1) << 31;  // imm[12] [31]
    return inst;
}

uint64_t statValueByName(const ICpuInterface::StatsList& stats, const std::string& name) {
    for (const auto& entry : stats) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return 0;
}

} // namespace

TEST(OutOfOrderControlFlowPredictionTest, JalCorrectPredictionDoesNotFlush) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    // 0x0: jal x0, +8  -> 0x8
    // 0x4: addi x1, x0, 1 (should be skipped)
    // 0x8: addi x1, x0, 2
    // 0xC: ecall
    memory->writeWord(0x0, createJType(Opcode::JAL, /*rd=*/0, /*imm=*/8));
    memory->writeWord(0x4, createIType(Opcode::OP_IMM, /*rd=*/1, /*rs1=*/0, /*imm=*/1, Funct3::ADD_SUB));
    memory->writeWord(0x8, createIType(Opcode::OP_IMM, /*rd=*/1, /*rs1=*/0, /*imm=*/2, Funct3::ADD_SUB));
    memory->writeWord(0xC, 0x00000073); // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 200 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    EXPECT_EQ(cpu->getRegister(1), 2u);

    const auto stats = cpu->getStats();
    EXPECT_EQ(statValueByName(stats, "cpu.pipeline.flushes"), 0u);
}

TEST(OutOfOrderControlFlowPredictionTest, JalrBtbTrainingAvoidsSecondMispredict) {
    auto memory = std::make_shared<Memory>(8192);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    // 0x0 : addi x1, x0, 0x20  (x1 = target)
    // 0x4 : jalr x0, x1, 0     (indirect jump, BTB miss first time)
    // 0x8 : addi x3, x0, 0x111 (fallthrough path; must be squashed)
    // 0xC : ecall              (fallthrough path; must be squashed)
    // 0x20: addi x2, x2, 1
    // 0x24: addi x4, x0, 2
    // 0x28: beq  x2, x4, +0x10 -> 0x38
    // 0x2C: jal  x0, -0x28     -> 0x4
    // 0x30: nop
    // 0x34: nop
    // 0x38: ecall
    memory->writeWord(0x0, createIType(Opcode::OP_IMM, /*rd=*/1, /*rs1=*/0, /*imm=*/0x20, Funct3::ADD_SUB));
    memory->writeWord(0x4, createIType(Opcode::JALR, /*rd=*/0, /*rs1=*/1, /*imm=*/0, Funct3::ADD_SUB));
    memory->writeWord(0x8, createIType(Opcode::OP_IMM, /*rd=*/3, /*rs1=*/0, /*imm=*/0x111, Funct3::ADD_SUB));
    memory->writeWord(0xC, 0x00000073); // ECALL

    memory->writeWord(0x20, createIType(Opcode::OP_IMM, /*rd=*/2, /*rs1=*/2, /*imm=*/1, Funct3::ADD_SUB));
    memory->writeWord(0x24, createIType(Opcode::OP_IMM, /*rd=*/4, /*rs1=*/0, /*imm=*/2, Funct3::ADD_SUB));
    memory->writeWord(0x28, createBType(Opcode::BRANCH, /*rs1=*/2, /*rs2=*/4, /*imm=*/0x10, Funct3::BEQ));
    memory->writeWord(0x2C, createJType(Opcode::JAL, /*rd=*/0, /*imm=*/-0x28));
    memory->writeWord(0x30, createIType(Opcode::OP_IMM, /*rd=*/0, /*rs1=*/0, /*imm=*/0, Funct3::ADD_SUB)); // NOP
    memory->writeWord(0x34, createIType(Opcode::OP_IMM, /*rd=*/0, /*rs1=*/0, /*imm=*/0, Funct3::ADD_SUB)); // NOP
    memory->writeWord(0x38, 0x00000073); // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 2000 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    EXPECT_EQ(cpu->getRegister(2), 2u);
    EXPECT_EQ(cpu->getRegister(3), 0u) << "JALR第一次miss的fallthrough路径不应被提交";

    const auto stats = cpu->getStats();
    EXPECT_GE(statValueByName(stats, "cpu.predictor.btb.hits"), 1u);
    EXPECT_EQ(statValueByName(stats, "cpu.predictor.jalr.mispredicts"), 1u);
}

} // namespace riscv

