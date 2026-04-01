#include <gtest/gtest.h>

#include "system/simulator.h"

#include <vector>

namespace riscv {

namespace {

uint32_t createITypeInstruction(int16_t imm, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(imm)) << 20) |
           (static_cast<uint32_t>(rs1) << 15) |
           (static_cast<uint32_t>(funct3) << 12) |
           (static_cast<uint32_t>(rd) << 7) |
           opcode;
}

uint32_t createECallInstruction() {
    return 0x00000073U;
}

void appendWord(std::vector<uint8_t>& program, uint32_t instruction) {
    program.push_back(static_cast<uint8_t>(instruction & 0xFFU));
    program.push_back(static_cast<uint8_t>((instruction >> 8) & 0xFFU));
    program.push_back(static_cast<uint8_t>((instruction >> 16) & 0xFFU));
    program.push_back(static_cast<uint8_t>((instruction >> 24) & 0xFFU));
}

uint64_t statValue(const ICpuInterface::StatsList& stats, const std::string& name) {
    for (const auto& entry : stats) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return 0;
}

std::vector<uint8_t> makeWarmupProgram() {
    std::vector<uint8_t> program;
    program.reserve(129 * 4);
    for (int i = 0; i < 128; ++i) {
        appendWord(program, createITypeInstruction(1, 1, 0x0, 1, 0x13));
    }
    appendWord(program, createECallInstruction());
    return program;
}

} // namespace

TEST(SimulatorTest, RunWithWarmupTriggersCallbackOnceAndKeepsSteadyStateWindow) {
    Simulator simulator(/*memorySize=*/4096, CpuType::OUT_OF_ORDER);
    simulator.setMaxOutOfOrderCycles(1000);

    const auto program = makeWarmupProgram();
    ASSERT_TRUE(simulator.loadProgramFromBytes(program, /*startAddr=*/0));

    int callback_count = 0;
    uint64_t callback_cycle = 0;
    uint64_t pre_reset_cycles = 0;

    const bool warmup_triggered = simulator.runWithWarmup(/*warmupCycles=*/40, [&]() {
        ++callback_count;
        callback_cycle = simulator.getCycleCount();
        const auto warmup_stats = simulator.getCpu()->getStats();
        pre_reset_cycles = statValue(warmup_stats, "cycles");
        simulator.getCpu()->resetStats();
    });

    ASSERT_TRUE(warmup_triggered);
    ASSERT_EQ(callback_count, 1);
    EXPECT_GE(callback_cycle, 40u);
    EXPECT_EQ(pre_reset_cycles, callback_cycle)
        << "warmup 回调读到的 cycles 应与 simulator 周期点一致";
    EXPECT_TRUE(simulator.isHalted());
    EXPECT_GT(simulator.getCycleCount(), callback_cycle)
        << "warmup 之后还应继续执行 steady-state 窗口";

    const auto post_stats = simulator.getCpu()->getStats();
    const uint64_t post_cycles = statValue(post_stats, "cycles");
    const uint64_t post_instructions = statValue(post_stats, "instructions");
    EXPECT_GT(post_cycles, 0u);
    EXPECT_GT(post_instructions, 0u);
    EXPECT_EQ(post_cycles, simulator.getCycleCount() - callback_cycle)
        << "post-warmup cycles 应只覆盖 reset 之后的 steady-state 窗口";
    EXPECT_LE(post_instructions, simulator.getInstructionCount())
        << "post-warmup instructions 应只统计 reset 之后的窗口";
    EXPECT_EQ(simulator.getCpu()->getRegister(1), 128u)
        << "warmup 统计重置不应影响架构执行结果";
}

} // namespace riscv
