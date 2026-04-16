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

size_t inflightCountByType(const CPUState& state, ExecutionUnitType type) {
    size_t count = 0;
    for (const auto& entry : state.memory_access_inflight) {
        if (entry.valid && entry.unit_type == type) {
            ++count;
        }
    }
    return count;
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

TEST(OutOfOrderCacheTest, DCacheStoreRecoversAfterBlockedByInflightLoadMiss) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x200, 0x12345678);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x200, Funct3::ADD_SUB)); // addi x1, x0, 0x200
    memory->writeWord(0x4, createIType(Opcode::LOAD, 2, 1, 0, Funct3::LW));             // lw x2, 0(x1)
    memory->writeWord(0x8, createIType(Opcode::OP_IMM, 3, 0, 0x300, Funct3::ADD_SUB)); // addi x3, x0, 0x300
    memory->writeWord(0xC, createIType(Opcode::OP_IMM, 4, 0, 7, Funct3::ADD_SUB));      // addi x4, x0, 7
    memory->writeWord(0x10, createSType(Opcode::STORE, 3, 4, 0, Funct3::SW));           // sw x4, 0(x3)
    memory->writeWord(0x14, 0x00000073);                                                  // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 600 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    EXPECT_EQ(cpu->getRegister(2), 0x12345678u);
    EXPECT_EQ(memory->readWord(0x300), 7u);

    const auto stats = cpu->getStats();
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.stall_cycles_load"), 20u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.write_accesses"), 1u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.stall_cycles_store"), 20u);
}

TEST(OutOfOrderCacheTest, DCacheNextLinePrefetchStatsAreObservable) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x200, 0x12345678);
    memory->writeWord(0x240, 0xAABBCCDD);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x200, Funct3::ADD_SUB));   // addi x1, x0, 0x200
    memory->writeWord(0x4, createIType(Opcode::LOAD, 2, 1, 0, Funct3::LW));               // lw x2, 0(x1)
    memory->writeWord(0x8, createIType(Opcode::OP_IMM, 1, 1, 0x40, Funct3::ADD_SUB));     // addi x1, x1, 0x40
    memory->writeWord(0xC, createIType(Opcode::LOAD, 3, 1, 0, Funct3::LW));               // lw x3, 0(x1)
    memory->writeWord(0x10, 0x00000073);                                                   // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 500 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    EXPECT_EQ(cpu->getRegister(2), 0x12345678u);
    EXPECT_EQ(cpu->getRegister(3), 0xFFFFFFFFAABBCCDDULL);

    const auto stats = cpu->getStats();
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.prefetch_requests"), 1u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.prefetch_issued"), 1u);
    EXPECT_GE(statValueByName(stats, "cpu.cache.l1d.prefetch_useful_hits"), 1u);
}

TEST(OutOfOrderCacheTest, LoadMissMovesIntoInflightQueueAndFreesLoadUnit) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x200, 0x12345678);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x200, Funct3::ADD_SUB)); // addi x1, x0, 0x200
    memory->writeWord(0x4, createIType(Opcode::LOAD, 2, 1, 0, Funct3::LW));             // lw x2, 0(x1)
    memory->writeWord(0x8, 0x00000073);                                                  // ECALL

    cpu->setPC(0x0);

    bool observed_inflight_load = false;
    for (int i = 0; i < 80 && !cpu->isHalted(); ++i) {
        cpu->step();
        const auto& state = cpu->getCPUState();
        const size_t inflight_loads = inflightCountByType(state, ExecutionUnitType::LOAD);
        const bool any_load_unit_busy =
            state.load_units[0].busy || state.load_units[1].busy;
        if (inflight_loads > 0) {
            observed_inflight_load = true;
            EXPECT_FALSE(any_load_unit_busy)
                << "load miss 已进入 inflight 后，不应继续占用 LOAD 执行单元";
            ASSERT_NE(state.reservation_station, nullptr);
            EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 0u)
                << "load miss 已进入 inflight 后，不应继续占用 RS 表项";
            break;
        }
    }

    EXPECT_TRUE(observed_inflight_load) << "应观察到 load miss 进入独立 inflight 队列";
}

TEST(OutOfOrderCacheTest, StoreMissMovesIntoInflightQueueAndFreesStoreUnit) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x240, Funct3::ADD_SUB)); // addi x1, x0, 0x240
    memory->writeWord(0x4, createIType(Opcode::OP_IMM, 2, 0, 7, Funct3::ADD_SUB));      // addi x2, x0, 7
    memory->writeWord(0x8, createSType(Opcode::STORE, 1, 2, 0, Funct3::SW));             // sw x2, 0(x1)
    memory->writeWord(0xC, 0x00000073);                                                   // ECALL

    cpu->setPC(0x0);

    bool observed_inflight_store = false;
    for (int i = 0; i < 80 && !cpu->isHalted(); ++i) {
        cpu->step();
        const auto& state = cpu->getCPUState();
        const size_t inflight_stores = inflightCountByType(state, ExecutionUnitType::STORE);
        const bool any_store_unit_busy =
            state.store_units[0].busy || state.store_units[1].busy;
        if (inflight_stores > 0) {
            observed_inflight_store = true;
            EXPECT_FALSE(any_store_unit_busy)
                << "store miss 已进入 inflight 后，不应继续占用 STORE 执行单元";
            ASSERT_NE(state.reservation_station, nullptr);
            EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 0u)
                << "store miss 已进入 inflight 后，不应继续占用 RS 表项";
            break;
        }
    }

    EXPECT_TRUE(observed_inflight_store) << "应观察到 store miss 进入独立 inflight 队列";
}

TEST(OutOfOrderCacheTest, TwoIndependentLoadMissesCanBeInflightTogether) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x200, 0x12345678);
    memory->writeWord(0x280, 0xAABBCCDD);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x200, Funct3::ADD_SUB)); // addi x1, x0, 0x200
    memory->writeWord(0x4, createIType(Opcode::OP_IMM, 2, 0, 0x280, Funct3::ADD_SUB)); // addi x2, x0, 0x280
    memory->writeWord(0x8, createIType(Opcode::LOAD, 3, 1, 0, Funct3::LW));             // lw x3, 0(x1)
    memory->writeWord(0xC, createIType(Opcode::LOAD, 4, 2, 0, Funct3::LW));             // lw x4, 0(x2)
    memory->writeWord(0x10, 0x00000073);                                                 // ECALL

    cpu->setPC(0x0);

    bool observed_two_inflight_loads = false;
    for (int i = 0; i < 120 && !cpu->isHalted(); ++i) {
        cpu->step();
        const auto& state = cpu->getCPUState();
        if (inflightCountByType(state, ExecutionUnitType::LOAD) >= 2) {
            observed_two_inflight_loads = true;
            break;
        }
    }

    EXPECT_TRUE(observed_two_inflight_loads)
        << "默认 OOO L1D 应允许两条独立 load miss 同时处于 inflight";
}

TEST(OutOfOrderCacheTest, InflightLoadWaitCountsAsBackendBlocked) {
    auto memory = std::make_shared<Memory>(4096);
    auto cpu = std::make_unique<OutOfOrderCPU>(memory);

    memory->writeWord(0x200, 0x12345678);

    memory->writeWord(0x0, createIType(Opcode::OP_IMM, 1, 0, 0x200, Funct3::ADD_SUB)); // addi x1, x0, 0x200
    memory->writeWord(0x4, createIType(Opcode::LOAD, 2, 1, 0, Funct3::LW));             // lw x2, 0(x1)
    memory->writeWord(0x8, 0x00000073);                                                  // ECALL

    cpu->setPC(0x0);
    for (int i = 0; i < 400 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted());
    const auto stats = cpu->getStats();
    EXPECT_GE(statValueByName(stats, "cpu.stall.execute_resource_blocked"), 1u);
}

} // namespace riscv
