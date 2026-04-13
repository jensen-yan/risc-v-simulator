#include <gtest/gtest.h>

#include "system/checkpoint_importer.h"
#include "system/checkpoint_runner.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace riscv {

namespace {

std::filesystem::path resetTempDir(const std::string& name) {
    const auto temp_dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);
    return temp_dir;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

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

SnapshotBundle makeSnapshot(std::vector<uint8_t> program, const std::string& workload_name, const std::string& point_id) {
    SnapshotBundle snapshot;
    snapshot.pc = 0;
    snapshot.integer_regs[17] = 93;
    snapshot.integer_regs[10] = 0;
    snapshot.recipe.workload_name = workload_name;
    snapshot.recipe.point_id = point_id;
    snapshot.recipe.weight = 0.25;

    MemorySegment code_segment;
    code_segment.base = 0;
    code_segment.bytes = std::move(program);
    snapshot.memory_segments.push_back(std::move(code_segment));
    return snapshot;
}

class StaticSnapshotImporter : public ICheckpointImporter {
public:
    explicit StaticSnapshotImporter(SnapshotBundle snapshot) : snapshot_(std::move(snapshot)) {}

    SnapshotBundle importCheckpoint(const CheckpointRunConfig&) const override {
        return snapshot_;
    }

private:
    SnapshotBundle snapshot_;
};

class ThrowingImporter : public ICheckpointImporter {
public:
    explicit ThrowingImporter(std::string message) : message_(std::move(message)) {}

    SnapshotBundle importCheckpoint(const CheckpointRunConfig&) const override {
        throw SimulatorException(message_);
    }

private:
    std::string message_;
};

CheckpointRunConfig makeRunConfig(const std::filesystem::path& output_dir,
                                  uint64_t warmup_instructions,
                                  uint64_t measure_instructions) {
    CheckpointRunConfig config;
    config.checkpoint_path = "/tmp/fake-checkpoint.zstd";
    config.recipe_path = "/tmp/fake-recipe.txt";
    config.importer_name = "fake-importer";
    config.restorer_path = "/tmp/fake-restorer";
    config.output_dir = output_dir.string();
    config.warmup_instructions = warmup_instructions;
    config.measure_instructions = measure_instructions;
    return config;
}

} // namespace

TEST(CheckpointRunnerTest, SuccessfulRunWritesCompletedResultAndStatsFiles) {
    auto temp_dir = resetTempDir("checkpoint_runner_success");

    std::vector<uint8_t> program;
    appendWord(program, createITypeInstruction(1, 1, 0x0, 1, 0x13));
    appendWord(program, createITypeInstruction(2, 1, 0x0, 2, 0x13));
    appendWord(program, createECallInstruction());

    CheckpointRunner runner(
        CpuType::IN_ORDER,
        /*memory_size=*/4096,
        [&](const std::string&) {
            return std::make_unique<StaticSnapshotImporter>(makeSnapshot(std::move(program), "bzip2_source", "555"));
        });

    const CheckpointRunResult result = runner.run(makeRunConfig(temp_dir / "output", 0, 2));

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.status, "completed");
    EXPECT_EQ(result.failure_reason, CheckpointFailureReason::NONE);
    EXPECT_EQ(result.workload_name, "bzip2_source");
    EXPECT_EQ(result.slice_id, "555");
    EXPECT_EQ(result.instructions_measure, 2u);
    EXPECT_GT(result.cycles_measure, 0u);
    EXPECT_GT(result.ipc_measure, 0.0);

    const auto output_dir = temp_dir / "output";
    EXPECT_TRUE(std::filesystem::exists(output_dir / "completed"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "stats.txt"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "result.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "summary.csv"));
    EXPECT_FALSE(std::filesystem::exists(output_dir / "abort"));

    const std::string stats_text = readTextFile(output_dir / "stats.txt");
    EXPECT_NE(stats_text.find("Execution Stats"), std::string::npos);

    const std::string result_json = readTextFile(output_dir / "result.json");
    EXPECT_NE(result_json.find("\"status\":\"completed\""), std::string::npos);
    EXPECT_NE(result_json.find("\"workload_name\":\"bzip2_source\""), std::string::npos);
    EXPECT_NE(result_json.find("\"slice_id\":\"555\""), std::string::npos);
    EXPECT_NE(result_json.find("\"instructions_measure\":2"), std::string::npos);

    const std::string summary_csv = readTextFile(output_dir / "summary.csv");
    EXPECT_NE(summary_csv.find("status,success"), std::string::npos);
    EXPECT_NE(summary_csv.find("completed,true"), std::string::npos);
}

TEST(CheckpointRunnerTest, ImportFailureWritesAbortMarkerAndErrorFile) {
    auto temp_dir = resetTempDir("checkpoint_runner_import_failure");

    CheckpointRunner runner(
        CpuType::IN_ORDER,
        /*memory_size=*/4096,
        [&](const std::string&) {
            return std::make_unique<ThrowingImporter>("fake importer failed");
        });

    const CheckpointRunResult result = runner.run(makeRunConfig(temp_dir / "output", 0, 2));

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status, "abort");
    EXPECT_EQ(result.failure_reason, CheckpointFailureReason::IMPORT_ERROR);
    EXPECT_TRUE(std::filesystem::exists(temp_dir / "output" / "abort"));
    EXPECT_TRUE(std::filesystem::exists(temp_dir / "output" / "error.txt"));
    EXPECT_FALSE(std::filesystem::exists(temp_dir / "output" / "completed"));

    const std::string error_text = readTextFile(temp_dir / "output" / "error.txt");
    EXPECT_NE(error_text.find("fake importer failed"), std::string::npos);
}

TEST(CheckpointRunnerTest, WindowFailureWritesAbortResultJsonAndFailureReason) {
    auto temp_dir = resetTempDir("checkpoint_runner_window_failure");

    std::vector<uint8_t> program;
    appendWord(program, createITypeInstruction(1, 1, 0x0, 1, 0x13));
    appendWord(program, createECallInstruction());

    CheckpointRunner runner(
        CpuType::IN_ORDER,
        /*memory_size=*/4096,
        [&](const std::string&) {
            return std::make_unique<StaticSnapshotImporter>(makeSnapshot(std::move(program), "mcf_base", "99"));
        });

    const CheckpointRunResult result = runner.run(makeRunConfig(temp_dir / "output", 0, 4));

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status, "abort");
    EXPECT_EQ(result.failure_reason, CheckpointFailureReason::PROGRAM_EXIT);
    EXPECT_TRUE(std::filesystem::exists(temp_dir / "output" / "abort"));
    EXPECT_TRUE(std::filesystem::exists(temp_dir / "output" / "result.json"));

    const std::string result_json = readTextFile(temp_dir / "output" / "result.json");
    EXPECT_NE(result_json.find("\"failure_reason\":\"program_exit\""), std::string::npos);
    EXPECT_NE(result_json.find("\"message\":"), std::string::npos);
}

TEST(CheckpointRunnerTest, InOrderCyclesMeasureDoesNotIncludeWarmupCycles) {
    auto temp_dir = resetTempDir("checkpoint_runner_inorder_cycles");

    std::vector<uint8_t> program;
    appendWord(program, createITypeInstruction(1, 1, 0x0, 1, 0x13));
    appendWord(program, createITypeInstruction(2, 1, 0x0, 2, 0x13));
    appendWord(program, createECallInstruction());

    CheckpointRunner runner(
        CpuType::IN_ORDER,
        /*memory_size=*/4096,
        [&](const std::string&) {
            return std::make_unique<StaticSnapshotImporter>(
                makeSnapshot(std::move(program), "inorder_cycles", "11"));
        });

    const CheckpointRunResult result = runner.run(makeRunConfig(temp_dir / "output", 1, 1));

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.instructions_measure, 1u);
    EXPECT_EQ(result.cycles_measure, 1u);
}

TEST(CheckpointRunnerTest, HighGuestAddressSnapshotUsesGuestBaseMappedMemory) {
    auto temp_dir = resetTempDir("checkpoint_runner_high_guest_base");

    std::vector<uint8_t> program;
    appendWord(program, createITypeInstruction(1, 1, 0x0, 1, 0x13));
    appendWord(program, createECallInstruction());

    SnapshotBundle snapshot;
    snapshot.pc = 0x80000100ULL;
    snapshot.integer_regs[1] = 41;
    snapshot.integer_regs[17] = 93;
    snapshot.integer_regs[10] = 0;
    snapshot.recipe.workload_name = "high_base";
    snapshot.recipe.point_id = "7";
    snapshot.recipe.weight = 0.5;

    MemorySegment code_segment;
    code_segment.base = 0x80000100ULL;
    code_segment.bytes = std::move(program);
    snapshot.memory_segments.push_back(std::move(code_segment));

    CheckpointRunner runner(
        CpuType::IN_ORDER,
        /*memory_size=*/4096,
        [snapshot = std::move(snapshot)](const std::string&) mutable {
            return std::make_unique<StaticSnapshotImporter>(std::move(snapshot));
        });

    const CheckpointRunResult result = runner.run(makeRunConfig(temp_dir / "output", 0, 1));

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.instructions_measure, 1u);
    EXPECT_TRUE(std::filesystem::exists(temp_dir / "output" / "completed"));
}

} // namespace riscv
