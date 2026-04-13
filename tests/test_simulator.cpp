#include <gtest/gtest.h>

#include "system/checkpoint_types.h"
#include "system/elf_loader.h"
#include "system/simulator.h"

#include <filesystem>
#include <fstream>
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

uint32_t createLoadInstruction(int16_t imm, uint8_t rs1, uint8_t funct3, uint8_t rd) {
    return createITypeInstruction(imm, rs1, funct3, rd, 0x03);
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

std::vector<uint8_t> makeExitProgram(size_t addi_count) {
    std::vector<uint8_t> program;
    program.reserve((addi_count + 1) * 4);
    for (size_t i = 0; i < addi_count; ++i) {
        appendWord(program, createITypeInstruction(1, 1, 0x0, 1, 0x13));
    }
    appendWord(program, createECallInstruction());
    return program;
}

} // namespace

TEST(SimulatorTest, LoadSnapshotRestoresPcRegistersMemoryAndExtensions) {
    Simulator simulator(/*memorySize=*/4096, CpuType::IN_ORDER);

    SnapshotBundle snapshot;
    std::vector<uint8_t> code_bytes;
    appendWord(code_bytes, createLoadInstruction(0, 1, 0x2, 5));
    appendWord(code_bytes, createITypeInstruction(3, 5, 0x0, 6, 0x13));
    appendWord(code_bytes, createECallInstruction());

    MemorySegment code_segment;
    code_segment.base = 0x100;
    code_segment.bytes = code_bytes;

    MemorySegment data_segment;
    data_segment.base = 0x200;
    data_segment.bytes = {0x39, 0x00, 0x00, 0x00};

    snapshot.pc = 0x100;
    snapshot.enabled_extensions =
        static_cast<uint32_t>(Extension::I) | static_cast<uint32_t>(Extension::M);
    snapshot.integer_regs[1] = 0x200;
    snapshot.integer_regs[17] = 93;
    snapshot.fp_regs[3] = 0x1122334455667788ULL;
    snapshot.csr_values.push_back({0x001, 0x5});
    snapshot.memory_segments = {code_segment, data_segment};

    ASSERT_TRUE(simulator.loadSnapshot(snapshot));
    EXPECT_EQ(simulator.getCpu()->getPC(), 0x100u);
    EXPECT_EQ(simulator.getCpu()->getRegister(1), 0x200u);
    EXPECT_EQ(simulator.getCpu()->getFPRegister(3), 0x1122334455667788ULL);
    EXPECT_EQ(simulator.getCpu()->getCSR(0x001), 0x5u);
    EXPECT_EQ(simulator.getCpu()->getEnabledExtensions(), snapshot.enabled_extensions);

    simulator.step();
    simulator.step();

    EXPECT_EQ(simulator.getCpu()->getRegister(5), 57u);
    EXPECT_EQ(simulator.getCpu()->getRegister(6), 60u);
    EXPECT_EQ(simulator.getCpu()->getPC(), 0x108u);
}

TEST(SimulatorTest, LoadSnapshotRestoresGprDependenciesForOutOfOrderCpu) {
    Simulator simulator(/*memorySize=*/4096, CpuType::OUT_OF_ORDER);
    simulator.setMaxOutOfOrderCycles(1000);

    SnapshotBundle snapshot;
    std::vector<uint8_t> code_bytes;
    appendWord(code_bytes, createITypeInstruction(3, 1, 0x0, 5, 0x13));
    appendWord(code_bytes, createITypeInstruction(4, 1, 0x0, 6, 0x13));
    appendWord(code_bytes, createECallInstruction());

    MemorySegment code_segment;
    code_segment.base = 0x100;
    code_segment.bytes = code_bytes;

    snapshot.pc = 0x100;
    snapshot.integer_regs[1] = 39;
    snapshot.integer_regs[10] = 0;
    snapshot.integer_regs[17] = 93;
    snapshot.memory_segments = {code_segment};

    ASSERT_TRUE(simulator.loadSnapshot(snapshot));

    const InstructionWindowResult result =
        simulator.runInstructionWindow(/*warmup_instructions=*/0, /*measure_instructions=*/2);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(simulator.getCpu()->getRegister(5), 42u);
    EXPECT_EQ(simulator.getCpu()->getRegister(6), 43u);
}

TEST(SimulatorTest, LoadSnapshotSupportsFileBackedMemorySegments) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "simulator_file_backed_snapshot";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    const auto segment_path = temp_dir / "segment.bin";
    std::vector<uint8_t> code_bytes;
    appendWord(code_bytes, createITypeInstruction(5, 1, 0x0, 5, 0x13));
    appendWord(code_bytes, createECallInstruction());

    {
        std::ofstream stream(segment_path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(code_bytes.data()),
                     static_cast<std::streamsize>(code_bytes.size()));
    }

    Simulator simulator(/*memorySize=*/4096, CpuType::IN_ORDER);

    SnapshotBundle snapshot;
    MemorySegment code_segment;
    code_segment.base = 0x100;
    code_segment.file_path = segment_path.string();
    code_segment.size = code_bytes.size();

    snapshot.pc = 0x100;
    snapshot.integer_regs[1] = 37;
    snapshot.integer_regs[17] = 93;
    snapshot.memory_segments = {code_segment};

    ASSERT_TRUE(simulator.loadSnapshot(snapshot));
    simulator.step();

    EXPECT_EQ(simulator.getCpu()->getRegister(5), 42u);
}

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

TEST(SimulatorTest, RunInstructionWindowReturnsProgramExitWhenMeasureEndsEarly) {
    Simulator simulator(/*memorySize=*/4096, CpuType::IN_ORDER);

    ASSERT_TRUE(simulator.loadProgramFromBytes(makeExitProgram(/*addi_count=*/2), /*startAddr=*/0));
    simulator.getCpu()->setRegister(17, 93);
    simulator.getCpu()->setRegister(10, 0);

    const InstructionWindowResult result =
        simulator.runInstructionWindow(/*warmup_instructions=*/1, /*measure_instructions=*/5);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.warmup_completed);
    EXPECT_FALSE(result.measure_completed);
    EXPECT_EQ(result.failure_reason, CheckpointFailureReason::PROGRAM_EXIT);
    EXPECT_EQ(result.warmup_instructions_completed, 1u);
    EXPECT_LT(result.measure_instructions_completed, 5u);
    EXPECT_EQ(result.stop_pc, 8u);
}

TEST(SimulatorTest, RunInstructionWindowClassifiesOutOfOrderIllegalInstruction) {
    Simulator simulator(/*memorySize=*/4096, CpuType::OUT_OF_ORDER);
    simulator.setMaxOutOfOrderCycles(1000);

    std::vector<uint8_t> program;
    appendWord(program, 0xffffffffU);
    ASSERT_TRUE(simulator.loadProgramFromBytes(program, /*startAddr=*/0));

    const InstructionWindowResult result =
        simulator.runInstructionWindow(/*warmup_instructions=*/0, /*measure_instructions=*/1);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.failure_reason, CheckpointFailureReason::ILLEGAL_INSTRUCTION);
}

TEST(SimulatorTest, CpuInterfaceExposesNextStepRetireLimitHooks) {
    Simulator simulator(/*memorySize=*/4096, CpuType::IN_ORDER);
    simulator.getCpu()->setNextStepRetireLimit(1);
    simulator.getCpu()->clearNextStepRetireLimit();
    SUCCEED();
}

TEST(SimulatorTest, RunInstructionWindowWarmupBoundaryIsPreciselySplitInOooMode) {
    Simulator simulator(/*memorySize=*/4096, CpuType::OUT_OF_ORDER);
    simulator.setMaxOutOfOrderCycles(1000);

    ASSERT_TRUE(simulator.loadProgramFromBytes(makeWarmupProgram(), /*startAddr=*/0));

    const InstructionWindowResult result =
        simulator.runInstructionWindow(/*warmup_instructions=*/41, /*measure_instructions=*/87);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.warmup_completed);
    EXPECT_TRUE(result.measure_completed);
    EXPECT_EQ(result.warmup_instructions_completed, 41u);
    EXPECT_EQ(result.measure_instructions_completed, 87u);
    EXPECT_EQ(result.total_instructions, 128u);
    EXPECT_EQ(simulator.getCpu()->getRegister(1), 128u);
    EXPECT_EQ(result.failure_reason, CheckpointFailureReason::NONE);
    EXPECT_EQ(statValue(simulator.getCpu()->getStats(), "instructions"), 87u)
        << "measure 统计应只覆盖 reset 之后的窗口";
    EXPECT_GT(statValue(simulator.getCpu()->getStats(), "cycles"), 0u);
}

TEST(SimulatorTest, ElfAutoSizingUsesLargerDefaultStackReserveForBaremetalWorkloads) {
    const auto repo_root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto elf_path = repo_root / "runtime" / "test_simple_printf.elf";
    if (!std::filesystem::exists(elf_path)) {
        GTEST_SKIP() << "缺少测试 ELF: " << elf_path;
    }

    const size_t with_small_reserve =
        ElfLoader::getRequiredMemorySize(elf_path.string(), /*minSize=*/0, /*stackReserve=*/0x10000);
    const size_t with_default_reserve =
        ElfLoader::getRequiredMemorySize(elf_path.string(), /*minSize=*/0);

    ASSERT_GE(with_default_reserve, with_small_reserve);
    EXPECT_EQ(with_default_reserve - with_small_reserve,
              ElfLoader::kDefaultStackReserve - static_cast<size_t>(0x10000))
        << "默认 ELF 自动 sizing 应比旧的 64 KiB 预留提供更充足的 stack/TLS 空间";
}

} // namespace riscv
