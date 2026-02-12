#include <gtest/gtest.h>

#include "cpu/ooo/ooo_cpu.h"
#include "core/memory.h"

#include <memory>
#include <string>

namespace riscv {

namespace {

uint32_t createIType(Opcode opcode, RegNum rd, RegNum rs1, int32_t imm, Funct3 funct3) {
    uint32_t inst = 0;
    inst |= static_cast<uint32_t>(opcode) & 0x7F;
    inst |= (rd & 0x1F) << 7;
    inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;
    inst |= (rs1 & 0x1F) << 15;
    inst |= (static_cast<uint32_t>(imm) & 0xFFF) << 20;
    return inst;
}

uint32_t createSType(Opcode opcode, RegNum rs1, RegNum rs2, int32_t imm, Funct3 funct3) {
    uint32_t inst = 0;
    const uint32_t uimm = static_cast<uint32_t>(imm) & 0xFFF;
    inst |= static_cast<uint32_t>(opcode) & 0x7F;
    inst |= (uimm & 0x1F) << 7;
    inst |= (static_cast<uint32_t>(funct3) & 0x7) << 12;
    inst |= (rs1 & 0x1F) << 15;
    inst |= (rs2 & 0x1F) << 20;
    inst |= ((uimm >> 5) & 0x7F) << 25;
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

TEST(OutOfOrderCacheTest, ICacheMissIsObservable) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 1, Funct3::ADD_SUB));
    memory->writeWord(0x4, 0x00000073); // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 400 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    const auto stats = cpu->getStats();
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1i.accesses"), 1u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1i.misses"), 1u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1i.stall_cycles"), 20u);
}

TEST(OutOfOrderCacheTest, DCacheLoadMissThenHit) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x200, 0x12345678);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x200, Funct3::ADD_SUB)); // addi x1, x0, 0x200
    memory->writeWord(0x4, createIType(Opcode::LOAD, 2, 1, 0, Funct3::LW));             // lw x2, 0(x1)
    memory->writeWord(0x8, createIType(Opcode::LOAD, 3, 1, 0, Funct3::LW));             // lw x3, 0(x1)
    memory->writeWord(0xC, 0x00000073);                                                   // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 500 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    EXPECT_EQ(cpu->getRegister(2), 0x12345678u);
    EXPECT_EQ(cpu->getRegister(3), 0x12345678u);

    const auto stats = cpu->getStats();
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.read_accesses"), 2u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.misses"), 1u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.hits"), 1u);
}

TEST(OutOfOrderCacheTest, StorePenaltyCountedInExecuteStage) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x240, Funct3::ADD_SUB)); // addi x1, x0, 0x240
    memory->writeWord(0x4, createIType(Opcode::OP_IMM, 2, 0, 7, Funct3::ADD_SUB));      // addi x2, x0, 7
    memory->writeWord(0x8, createSType(Opcode::STORE, 1, 2, 0, Funct3::SW));             // sw x2, 0(x1)
    memory->writeWord(0xC, 0x00000073);                                                   // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 500 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    EXPECT_EQ(memory->readWord(0x240), 7u);

    const auto stats = cpu->getStats();
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.write_accesses"), 1u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.stall_cycles_store"), 20u);
}

} // namespace riscv

