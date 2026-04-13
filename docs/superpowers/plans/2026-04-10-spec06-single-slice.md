# SPEC06 Single Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为当前模拟器增加“单个 SPEC06 `.zstd` 切片 + recipe 输入”的恢复执行路径，支持 `5M warmup + 5M measure`、提前停止失败分类、以及 `stats/result.json/summary.csv/status` 输出。

**Architecture:** 实现采用 `recipe parser -> checkpoint importer -> SnapshotBundle -> Simulator restore/window run -> checkpoint runner/result writer` 的分层结构。模拟器内部只恢复架构态，微架构状态通过 warmup 窗口建立；CLI 只负责参数解析和分派，结果落盘与失败归因统一由 `CheckpointRunner` 收口。

**Tech Stack:** C++17、GoogleTest、现有 `Simulator` / `ICpuInterface` stats reset 能力、`std::filesystem`、shell 外部 importer

---

## File Structure

- Create: `include/system/checkpoint_types.h`
- Create: `include/system/checkpoint_recipe.h`
- Create: `src/system/checkpoint_recipe.cpp`
- Create: `include/system/checkpoint_importer.h`
- Create: `src/system/checkpoint_importer.cpp`
- Create: `include/system/checkpoint_runner.h`
- Create: `src/system/checkpoint_runner.cpp`
- Modify: `include/system/simulator.h`
- Modify: `src/system/simulator.cpp`
- Modify: `src/system/CMakeLists.txt`
- Modify: `src/main.cpp`
- Create: `tests/test_checkpoint_recipe.cpp`
- Create: `tests/test_checkpoint_importer.cpp`
- Create: `tests/test_checkpoint_runner.cpp`
- Modify: `tests/test_simulator.cpp`

### Task 1: 固化 `checkpoint + recipe` 输入契约与元数据解析

**Files:**
- Create: `include/system/checkpoint_types.h`
- Create: `include/system/checkpoint_recipe.h`
- Create: `src/system/checkpoint_recipe.cpp`
- Create: `tests/test_checkpoint_recipe.cpp`
- Modify: `src/system/CMakeLists.txt`

- [ ] **Step 1: 写失败测试，锁定 checkpoint 路径与 recipe 解析语义**

```cpp
#include <gtest/gtest.h>

#include "system/checkpoint_recipe.h"

#include <filesystem>
#include <fstream>

namespace riscv {
namespace {

TEST(CheckpointRecipeTest, ParsesCheckpointPathAndRecipeScript) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "checkpoint_recipe_test";
    std::filesystem::create_directories(temp_dir / "bzip2_source" / "555");
    const auto checkpoint_path = temp_dir / "bzip2_source" / "555" / "_555_0.026526_.zstd";
    std::ofstream(checkpoint_path.string()).put('\n');

    const auto recipe_path = temp_dir / "bzip2_source_initramfs-spec.txt";
    std::ofstream recipe(recipe_path.string());
    recipe << "file /spec0/task0.sh /tmp/spec/bzip2_source_task0.sh 755 0 0\n";
    recipe << "file /spec0/run.sh /tmp/spec/bzip2_source_run.sh 755 0 0\n";
    recipe.close();

    const CheckpointRecipeSpec spec =
        loadCheckpointRecipeSpec(checkpoint_path.string(), recipe_path.string());

    EXPECT_EQ(spec.workload_name, "bzip2_source");
    EXPECT_EQ(spec.point_id, "555");
    EXPECT_DOUBLE_EQ(spec.weight, 0.026526);
    EXPECT_EQ(spec.task_script_path, "/tmp/spec/bzip2_source_task0.sh");
    EXPECT_EQ(spec.run_script_path, "/tmp/spec/bzip2_source_run.sh");
}

TEST(CheckpointRecipeTest, RejectsRecipeWithoutTaskScript) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "checkpoint_recipe_test_missing";
    std::filesystem::create_directories(temp_dir / "gcc_200" / "28");
    const auto checkpoint_path = temp_dir / "gcc_200" / "28" / "_28_0.123456_.zstd";
    std::ofstream(checkpoint_path.string()).put('\n');

    const auto recipe_path = temp_dir / "gcc_200_initramfs-spec.txt";
    std::ofstream recipe(recipe_path.string());
    recipe << "file /spec0/run.sh /tmp/spec/gcc_200_run.sh 755 0 0\n";
    recipe.close();

    EXPECT_THROW(loadCheckpointRecipeSpec(checkpoint_path.string(), recipe_path.string()),
                 SimulatorException);
}

} // namespace
} // namespace riscv
```

- [ ] **Step 2: 运行测试，确认它们先失败**

Run: `ctest --test-dir build --output-on-failure -R CheckpointRecipeTest`

Expected:
- 编译失败，提示缺少 `system/checkpoint_recipe.h`
- 或测试失败，提示尚未实现 `loadCheckpointRecipeSpec(...)`

- [ ] **Step 3: 实现共享类型与最小 recipe 解析器**

```cpp
// include/system/checkpoint_types.h
#pragma once

#include "common/types.h"

#include <array>
#include <string>
#include <vector>

namespace riscv {

struct MemorySegment {
    Address base = 0;
    std::vector<uint8_t> bytes;
};

struct CheckpointRecipeSpec {
    std::string checkpoint_path;
    std::string recipe_path;
    std::string workload_name;
    std::string point_id;
    double weight = 0.0;
    std::string task_script_path;
    std::string run_script_path;
};

struct SnapshotBundle {
    uint64_t pc = 0;
    uint32_t enabled_extensions = static_cast<uint32_t>(Extension::I);
    std::array<uint64_t, 32> integer_regs{};
    std::array<uint64_t, 32> fp_regs{};
    std::vector<std::pair<uint32_t, uint64_t>> csr_values;
    std::vector<MemorySegment> memory_segments;
    CheckpointRecipeSpec recipe;
};

enum class CheckpointFailureReason {
    NONE,
    IMPORT_ERROR,
    RESTORE_ERROR,
    ILLEGAL_INSTRUCTION,
    UNIMPLEMENTED_SYSTEM_INSTRUCTION,
    TRAP,
    PROGRAM_EXIT,
    HALT_REQUESTED,
    WINDOW_NOT_REACHED,
    UNKNOWN,
};

} // namespace riscv
```

```cpp
// include/system/checkpoint_recipe.h
#pragma once

#include "system/checkpoint_types.h"

#include <string>

namespace riscv {

CheckpointRecipeSpec loadCheckpointRecipeSpec(const std::string& checkpoint_path,
                                              const std::string& recipe_path);

} // namespace riscv
```

```cpp
// src/system/checkpoint_recipe.cpp
#include "system/checkpoint_recipe.h"

#include <filesystem>
#include <fstream>
#include <regex>

namespace riscv {

CheckpointRecipeSpec loadCheckpointRecipeSpec(const std::string& checkpoint_path,
                                              const std::string& recipe_path) {
    namespace fs = std::filesystem;
    if (!fs::exists(checkpoint_path)) {
        throw SimulatorException("checkpoint 不存在: " + checkpoint_path);
    }
    if (!fs::exists(recipe_path)) {
        throw SimulatorException("checkpoint recipe 不存在: " + recipe_path);
    }

    CheckpointRecipeSpec spec;
    spec.checkpoint_path = checkpoint_path;
    spec.recipe_path = recipe_path;

    const fs::path checkpoint_fs_path(checkpoint_path);
    spec.workload_name = checkpoint_fs_path.parent_path().parent_path().filename().string();
    spec.point_id = checkpoint_fs_path.parent_path().filename().string();

    const std::regex filename_re(R"(_([0-9]+)_([0-9]+\.[0-9]+)_\.zstd$)");
    std::smatch match;
    const std::string filename = checkpoint_fs_path.filename().string();
    if (!std::regex_match(filename, match, filename_re)) {
        throw SimulatorException("无法从 checkpoint 文件名解析 point_id/weight: " + filename);
    }
    spec.weight = std::stod(match[2].str());

    std::ifstream recipe(recipe_path);
    std::string line;
    while (std::getline(recipe, line)) {
        if (line.rfind("file /spec0/task0.sh ", 0) == 0) {
            spec.task_script_path = line.substr(std::string("file /spec0/task0.sh ").size());
            spec.task_script_path = spec.task_script_path.substr(0, spec.task_script_path.find(" 755 "));
        } else if (line.rfind("file /spec0/run.sh ", 0) == 0) {
            spec.run_script_path = line.substr(std::string("file /spec0/run.sh ").size());
            spec.run_script_path = spec.run_script_path.substr(0, spec.run_script_path.find(" 755 "));
        }
    }

    if (spec.task_script_path.empty()) {
        throw SimulatorException("recipe 缺少 /spec0/task0.sh 定义: " + recipe_path);
    }
    return spec;
}

} // namespace riscv
```

- [ ] **Step 4: 接入构建并重新运行测试**

Run: `ctest --test-dir build --output-on-failure -R CheckpointRecipeTest`

Expected:
- `CheckpointRecipeTest` 全部通过

- [ ] **Step 5: 提交契约与 recipe 解析阶段**

```bash
git add include/system/checkpoint_types.h \
        include/system/checkpoint_recipe.h \
        src/system/checkpoint_recipe.cpp \
        src/system/CMakeLists.txt \
        tests/test_checkpoint_recipe.cpp
git commit -m "feat: 增加 SPEC06 checkpoint recipe 基础契约"
```

### Task 2: 给 Simulator 增加快照恢复与指令窗口执行 API

**Files:**
- Modify: `include/system/simulator.h`
- Modify: `src/system/simulator.cpp`
- Modify: `tests/test_simulator.cpp`

- [ ] **Step 1: 写失败测试，锁定“恢复架构态 + 窗口执行”行为**

```cpp
TEST(SimulatorTest, LoadSnapshotRestoresPcRegistersAndMemory) {
    Simulator simulator(/*memorySize=*/4096, CpuType::OUT_OF_ORDER);

    SnapshotBundle snapshot;
    snapshot.pc = 0x100;
    snapshot.integer_regs[1] = 0x1234;
    snapshot.integer_regs[2] = 0x5678;
    snapshot.memory_segments.push_back({0x100, makeWarmupProgram()});

    ASSERT_TRUE(simulator.loadSnapshot(snapshot));
    EXPECT_EQ(simulator.getCpu()->getPC(), 0x100u);
    EXPECT_EQ(simulator.getCpu()->getRegister(1), 0x1234u);
    EXPECT_EQ(simulator.getCpu()->getRegister(2), 0x5678u);
}

TEST(SimulatorTest, InstructionWindowReportsEarlyProgramExitAsFailure) {
    Simulator simulator(/*memorySize=*/4096, CpuType::OUT_OF_ORDER);
    simulator.setMaxOutOfOrderCycles(2000);

    SnapshotBundle snapshot;
    snapshot.pc = 0;
    snapshot.memory_segments.push_back({0, makeWarmupProgram()});
    ASSERT_TRUE(simulator.loadSnapshot(snapshot));

    const auto result = simulator.runInstructionWindow(/*warmupInstructions=*/16,
                                                       /*measureInstructions=*/1024);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.warmup_completed);
    EXPECT_FALSE(result.measure_completed);
    EXPECT_EQ(result.failure_reason, CheckpointFailureReason::PROGRAM_EXIT);
}
```

- [ ] **Step 2: 运行测试，确认它们先失败**

Run: `ctest --test-dir build --output-on-failure -R SimulatorTest`

Expected:
- 编译失败，提示 `loadSnapshot(...)` / `runInstructionWindow(...)` 未定义

- [ ] **Step 3: 在 Simulator 中实现快照恢复与窗口执行结果结构**

```cpp
// include/system/simulator.h
struct InstructionWindowResult {
    bool success = false;
    bool warmup_completed = false;
    bool measure_completed = false;
    CheckpointFailureReason failure_reason = CheckpointFailureReason::NONE;
    std::string message;
    uint64_t stop_pc = 0;
    uint64_t total_instructions = 0;
    uint64_t total_cycles = 0;
    uint64_t warmup_instructions_completed = 0;
    uint64_t measure_instructions_completed = 0;
};

bool loadSnapshot(const SnapshotBundle& snapshot);
InstructionWindowResult runInstructionWindow(uint64_t warmup_instructions,
                                             uint64_t measure_instructions);
```

```cpp
// src/system/simulator.cpp
bool Simulator::loadSnapshot(const SnapshotBundle& snapshot) {
    reset();
    memory_->resetExitStatus();
    memory_->clear();

    for (const auto& segment : snapshot.memory_segments) {
        memory_->loadProgram(segment.bytes, segment.base);
    }

    cpu_->reset();
    cpu_->setEnabledExtensions(snapshot.enabled_extensions);
    for (size_t i = 0; i < snapshot.integer_regs.size(); ++i) {
        cpu_->setRegister(static_cast<RegNum>(i), snapshot.integer_regs[i]);
        cpu_->setFPRegister(static_cast<RegNum>(i), snapshot.fp_regs[i]);
    }
    for (const auto& [csr_addr, csr_value] : snapshot.csr_values) {
        cpu_->setCSR(csr_addr, csr_value);
    }
    cpu_->setPC(snapshot.pc);

    cycle_count_ = 0;
    halted_by_instruction_limit_ = false;
    halted_by_cycle_limit_ = false;
    DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
    return true;
}

InstructionWindowResult Simulator::runInstructionWindow(uint64_t warmup_instructions,
                                                        uint64_t measure_instructions) {
    InstructionWindowResult result;
    const uint64_t start_instructions = cpu_->getInstructionCount();
    const uint64_t warmup_target = start_instructions + warmup_instructions;
    const uint64_t total_target = warmup_target + measure_instructions;

    auto classify_exception = [](const SimulatorException& e) {
        const std::string message = e.what();
        if (dynamic_cast<const IllegalInstructionException*>(&e) != nullptr) {
            if (message.find("系统指令") != std::string::npos || message.find("CSR") != std::string::npos) {
                return CheckpointFailureReason::UNIMPLEMENTED_SYSTEM_INSTRUCTION;
            }
            return CheckpointFailureReason::ILLEGAL_INSTRUCTION;
        }
        return CheckpointFailureReason::UNKNOWN;
    };

    try {
        while (!cpu_->isHalted() && !memory_->shouldExit()) {
            step();

            const uint64_t retired = cpu_->getInstructionCount();
            if (!result.warmup_completed && retired >= warmup_target) {
                result.warmup_completed = true;
                result.warmup_instructions_completed = retired - start_instructions;
                cpu_->resetStats();
            }
            if (result.warmup_completed && retired >= total_target) {
                result.success = true;
                result.measure_completed = true;
                break;
            }
        }
    } catch (const SimulatorException& e) {
        result.failure_reason = classify_exception(e);
        result.message = e.what();
    }

    result.stop_pc = cpu_->getPC();
    result.total_instructions = cpu_->getInstructionCount() - start_instructions;
    result.total_cycles = cycle_count_;
    if (result.warmup_completed) {
        result.measure_instructions_completed =
            result.total_instructions - result.warmup_instructions_completed;
    } else {
        result.warmup_instructions_completed = result.total_instructions;
    }

    if (!result.success && result.failure_reason == CheckpointFailureReason::NONE) {
        if (memory_->shouldExit()) {
            result.failure_reason = CheckpointFailureReason::PROGRAM_EXIT;
            result.message = "checkpoint 在达到 measure 窗口前自行退出";
        } else if (cpu_->isHalted()) {
            result.failure_reason = CheckpointFailureReason::WINDOW_NOT_REACHED;
            result.message = "checkpoint 在达到目标窗口前停止";
        }
    }
    return result;
}
```

- [ ] **Step 4: 运行 Simulator 聚焦测试**

Run: `ctest --test-dir build --output-on-failure -R SimulatorTest`

Expected:
- 新增恢复 / 窗口测试通过
- 现有 `RunWithWarmup...` 测试继续通过

- [ ] **Step 5: 提交 Simulator 恢复与窗口执行阶段**

```bash
git add include/system/simulator.h \
        src/system/simulator.cpp \
        tests/test_simulator.cpp
git commit -m "feat: 增加 checkpoint 快照恢复与指令窗口执行 API"
```

### Task 3: 增加可插拔 importer，并用外部进程 manifest 接口承接真实 checkpoint

**Files:**
- Create: `include/system/checkpoint_importer.h`
- Create: `src/system/checkpoint_importer.cpp`
- Create: `tests/test_checkpoint_importer.cpp`
- Modify: `src/system/CMakeLists.txt`

- [ ] **Step 1: 写失败测试，锁定外部 importer 合同**

```cpp
TEST(CheckpointImporterTest, ExternalImporterBuildsSnapshotFromManifest) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "checkpoint_importer_test";
    std::filesystem::create_directories(temp_dir);

    const auto segment_path = temp_dir / "segment.bin";
    std::ofstream segment(segment_path.string(), std::ios::binary);
    const auto program = makeWarmupProgram();
    segment.write(reinterpret_cast<const char*>(program.data()),
                  static_cast<std::streamsize>(program.size()));
    segment.close();

    const auto script_path = temp_dir / "fake_importer.sh";
    std::ofstream script(script_path.string());
    script << "#!/bin/sh\n";
    script << "echo 'pc=0x100'\n";
    script << "echo 'extensions=0x1'\n";
    script << "echo 'gpr[1]=0x99'\n";
    script << "echo 'segment=0x100:" << segment_path.string() << "'\n";
    script.close();
    std::filesystem::permissions(script_path,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write);

    CheckpointRunConfig config;
    config.checkpoint_path = "/tmp/fake.zstd";
    config.recipe_path = "/tmp/fake_initramfs-spec.txt";
    config.importer_name = "external-process";
    config.restorer_path = script_path.string();

    ExternalProcessCheckpointImporter importer;
    const SnapshotBundle bundle = importer.importCheckpoint(config);

    EXPECT_EQ(bundle.pc, 0x100u);
    EXPECT_EQ(bundle.integer_regs[1], 0x99u);
    ASSERT_EQ(bundle.memory_segments.size(), 1u);
    EXPECT_EQ(bundle.memory_segments[0].base, 0x100u);
}
```

- [ ] **Step 2: 运行测试，确认它先失败**

Run: `ctest --test-dir build --output-on-failure -R CheckpointImporterTest`

Expected:
- 编译失败，提示 importer 相关类型不存在

- [ ] **Step 3: 实现 importer 接口、运行配置和 line-based manifest 解析**

```cpp
// include/system/checkpoint_importer.h
#pragma once

#include "system/checkpoint_types.h"

#include <memory>
#include <string>

namespace riscv {

struct CheckpointRunConfig {
    std::string checkpoint_path;
    std::string recipe_path;
    std::string importer_name = "external-process";
    std::string restorer_path;
    std::string output_dir;
    uint64_t warmup_instructions = 5'000'000;
    uint64_t measure_instructions = 5'000'000;
};

class ICheckpointImporter {
public:
    virtual ~ICheckpointImporter() = default;
    virtual SnapshotBundle importCheckpoint(const CheckpointRunConfig& config) const = 0;
};

class ExternalProcessCheckpointImporter final : public ICheckpointImporter {
public:
    SnapshotBundle importCheckpoint(const CheckpointRunConfig& config) const override;
};

std::unique_ptr<ICheckpointImporter> createCheckpointImporter(const std::string& importer_name);

} // namespace riscv
```

```cpp
// src/system/checkpoint_importer.cpp
#include "system/checkpoint_importer.h"

#include "system/checkpoint_recipe.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace riscv {

SnapshotBundle ExternalProcessCheckpointImporter::importCheckpoint(
    const CheckpointRunConfig& config) const {
    if (config.restorer_path.empty()) {
        throw SimulatorException("external-process importer 需要 --checkpoint-restorer");
    }

    SnapshotBundle bundle;
    bundle.recipe = loadCheckpointRecipeSpec(config.checkpoint_path, config.recipe_path);

    const std::string command =
        config.restorer_path + " --checkpoint " + config.checkpoint_path + " --recipe " + config.recipe_path;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw SimulatorException("无法启动 checkpoint importer: " + config.restorer_path);
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        const std::string line(buffer);
        if (line.rfind("pc=", 0) == 0) {
            bundle.pc = std::stoull(line.substr(3), nullptr, 0);
        } else if (line.rfind("extensions=", 0) == 0) {
            bundle.enabled_extensions = static_cast<uint32_t>(std::stoul(line.substr(11), nullptr, 0));
        } else if (line.rfind("gpr[", 0) == 0) {
            const auto close = line.find(']');
            const auto eq = line.find('=');
            const auto index = static_cast<size_t>(std::stoul(line.substr(4, close - 4)));
            bundle.integer_regs[index] = std::stoull(line.substr(eq + 1), nullptr, 0);
        } else if (line.rfind("segment=", 0) == 0) {
            const auto colon = line.find(':', 8);
            const Address base = std::stoull(line.substr(8, colon - 8), nullptr, 0);
            const std::filesystem::path file_path(line.substr(colon + 1));
            std::ifstream segment(file_path, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(segment)),
                                       std::istreambuf_iterator<char>());
            bundle.memory_segments.push_back({base, std::move(bytes)});
        }
    }
    if (pclose(pipe) != 0) {
        throw SimulatorException("checkpoint importer 返回非零退出码");
    }
    if (bundle.memory_segments.empty()) {
        throw SimulatorException("checkpoint importer 未输出任何 memory segment");
    }
    return bundle;
}

std::unique_ptr<ICheckpointImporter> createCheckpointImporter(const std::string& importer_name) {
    if (importer_name == "external-process") {
        return std::make_unique<ExternalProcessCheckpointImporter>();
    }
    throw SimulatorException("未知 checkpoint importer: " + importer_name);
}

} // namespace riscv
```

- [ ] **Step 4: 运行 importer 聚焦测试**

Run: `ctest --test-dir build --output-on-failure -R CheckpointImporterTest`

Expected:
- 外部脚本 manifest 可被转换为 `SnapshotBundle`
- 缺少 `--checkpoint-restorer` 的路径会抛出异常

- [ ] **Step 5: 提交 importer 阶段**

```bash
git add include/system/checkpoint_importer.h \
        src/system/checkpoint_importer.cpp \
        src/system/CMakeLists.txt \
        tests/test_checkpoint_importer.cpp
git commit -m "feat: 增加可插拔 checkpoint importer 接口"
```

### Task 4: 实现单切片 runner、结果落盘与 CLI 分派

**Files:**
- Create: `include/system/checkpoint_runner.h`
- Create: `src/system/checkpoint_runner.cpp`
- Create: `tests/test_checkpoint_runner.cpp`
- Modify: `src/main.cpp`
- Modify: `src/system/CMakeLists.txt`

- [ ] **Step 1: 写失败测试，锁定 completed/abort、result.json 和 summary.csv 输出**

```cpp
TEST(CheckpointRunnerTest, WritesCompletedArtifactsWhenMeasureWindowIsReached) {
    auto makeLongRunningProgram = [](size_t instruction_count) {
        std::vector<uint8_t> program;
        program.reserve((instruction_count + 1) * 4);
        for (size_t i = 0; i < instruction_count; ++i) {
            appendWord(program, createITypeInstruction(1, 1, 0x0, 1, 0x13));
        }
        appendWord(program, createECallInstruction());
        return program;
    };

    class FakeImporter final : public ICheckpointImporter {
    public:
        SnapshotBundle importCheckpoint(const CheckpointRunConfig& config) const override {
            SnapshotBundle bundle;
            bundle.recipe.checkpoint_path = config.checkpoint_path;
            bundle.recipe.recipe_path = config.recipe_path;
            bundle.recipe.workload_name = "fake_workload";
            bundle.recipe.point_id = "7";
            bundle.recipe.weight = 0.5;
            bundle.pc = 0;
            bundle.memory_segments.push_back({0, makeLongRunningProgram(/*instruction_count=*/128)});
            return bundle;
        }
    };

    const auto output_dir = std::filesystem::temp_directory_path() / "checkpoint_runner_success";
    CheckpointRunConfig config;
    config.checkpoint_path = "/tmp/fake.zstd";
    config.recipe_path = "/tmp/fake_initramfs-spec.txt";
    config.output_dir = output_dir.string();
    config.warmup_instructions = 16;
    config.measure_instructions = 16;

    CheckpointRunner runner(std::make_unique<FakeImporter>());
    const auto result = runner.runSingle(config);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(std::filesystem::exists(output_dir / "completed"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "stats.txt"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "result.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "summary.csv"));
    EXPECT_FALSE(std::filesystem::exists(output_dir / "abort"));
}
```

- [ ] **Step 2: 运行测试，确认它先失败**

Run: `ctest --test-dir build --output-on-failure -R CheckpointRunnerTest`

Expected:
- 编译失败，提示 `CheckpointRunner` 未实现

- [ ] **Step 3: 实现 runner、结果结构与文件写出**

```cpp
// include/system/checkpoint_runner.h
#pragma once

#include "system/checkpoint_importer.h"
#include "system/simulator.h"

#include <memory>

namespace riscv {

struct CheckpointRunResult {
    bool success = false;
    std::string status = "abort";
    CheckpointFailureReason failure_reason = CheckpointFailureReason::NONE;
    std::string message;
    std::string stats_path;
    std::string result_json_path;
    std::string summary_csv_path;
    uint64_t instructions_measure = 0;
    uint64_t cycles_measure = 0;
    double ipc_measure = 0.0;
};

class CheckpointRunner {
public:
    explicit CheckpointRunner(std::unique_ptr<ICheckpointImporter> importer);

    CheckpointRunResult runSingle(const CheckpointRunConfig& config);

private:
    std::unique_ptr<ICheckpointImporter> importer_;

    void writeResultFiles(const CheckpointRunConfig& config,
                          const SnapshotBundle& bundle,
                          const InstructionWindowResult& window,
                          const ICpuInterface::StatsList& stats,
                          CheckpointRunResult& result) const;
};

} // namespace riscv
```

```cpp
// src/system/checkpoint_runner.cpp
#include "system/checkpoint_runner.h"

#include <filesystem>
#include <fstream>
#include <iomanip>

namespace riscv {

namespace {

uint64_t statValue(const ICpuInterface::StatsList& stats, const std::string& name) {
    for (const auto& entry : stats) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return 0;
}

std::string failureReasonName(CheckpointFailureReason reason) {
    switch (reason) {
        case CheckpointFailureReason::NONE: return "NONE";
        case CheckpointFailureReason::IMPORT_ERROR: return "IMPORT_ERROR";
        case CheckpointFailureReason::RESTORE_ERROR: return "RESTORE_ERROR";
        case CheckpointFailureReason::ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case CheckpointFailureReason::UNIMPLEMENTED_SYSTEM_INSTRUCTION: return "UNIMPLEMENTED_SYSTEM_INSTRUCTION";
        case CheckpointFailureReason::TRAP: return "TRAP";
        case CheckpointFailureReason::PROGRAM_EXIT: return "PROGRAM_EXIT";
        case CheckpointFailureReason::HALT_REQUESTED: return "HALT_REQUESTED";
        case CheckpointFailureReason::WINDOW_NOT_REACHED: return "WINDOW_NOT_REACHED";
        case CheckpointFailureReason::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

} // namespace

CheckpointRunner::CheckpointRunner(std::unique_ptr<ICheckpointImporter> importer)
    : importer_(std::move(importer)) {}

CheckpointRunResult CheckpointRunner::runSingle(const CheckpointRunConfig& config) {
    namespace fs = std::filesystem;
    fs::create_directories(config.output_dir);

    CheckpointRunResult result;
    std::ofstream log(fs::path(config.output_dir) / "run.log");

    SnapshotBundle bundle;
    try {
        bundle = importer_->importCheckpoint(config);
    } catch (const SimulatorException& e) {
        result.failure_reason = CheckpointFailureReason::IMPORT_ERROR;
        result.message = e.what();
        std::ofstream(fs::path(config.output_dir) / "abort").put('\n');
        std::ofstream(fs::path(config.output_dir) / "error.txt") << e.what() << "\n";
        return result;
    }

    Simulator simulator(/*memorySize=*/2ULL * 1024 * 1024 * 1024, CpuType::OUT_OF_ORDER);
    try {
        simulator.loadSnapshot(bundle);
    } catch (const SimulatorException& e) {
        result.failure_reason = CheckpointFailureReason::RESTORE_ERROR;
        result.message = e.what();
        std::ofstream(fs::path(config.output_dir) / "abort").put('\n');
        std::ofstream(fs::path(config.output_dir) / "error.txt") << e.what() << "\n";
        return result;
    }

    const auto window = simulator.runInstructionWindow(config.warmup_instructions,
                                                       config.measure_instructions);
    const auto stats = simulator.getCpu()->getStats();
    writeResultFiles(config, bundle, window, stats, result);
    return result;
}

void CheckpointRunner::writeResultFiles(const CheckpointRunConfig& config,
                                        const SnapshotBundle& bundle,
                                        const InstructionWindowResult& window,
                                        const ICpuInterface::StatsList& stats,
                                        CheckpointRunResult& result) const {
    namespace fs = std::filesystem;
    const fs::path root(config.output_dir);
    const fs::path stats_path = root / "stats.txt";
    const fs::path json_path = root / "result.json";
    const fs::path csv_path = root / "summary.csv";

    result.success = window.success;
    result.status = window.success ? "completed" : "abort";
    result.failure_reason = window.failure_reason;
    result.message = window.message;
    result.instructions_measure = statValue(stats, "instructions");
    result.cycles_measure = statValue(stats, "cycles");
    result.ipc_measure = result.cycles_measure == 0
        ? 0.0
        : static_cast<double>(result.instructions_measure) /
              static_cast<double>(result.cycles_measure);

    std::ofstream(stats_path) << "Instructions: " << result.instructions_measure << "\n"
                              << "Cycles: " << result.cycles_measure << "\n"
                              << "IPC: " << std::fixed << std::setprecision(6) << result.ipc_measure << "\n";

    std::ofstream(json_path)
        << "{\n"
        << "  \"status\": \"" << result.status << "\",\n"
        << "  \"success\": " << (result.success ? "true" : "false") << ",\n"
        << "  \"failure_reason\": \"" << failureReasonName(result.failure_reason) << "\",\n"
        << "  \"message\": \"" << result.message << "\",\n"
        << "  \"benchmark\": \"" << bundle.recipe.workload_name << "\",\n"
        << "  \"slice_id\": \"" << bundle.recipe.point_id << "\",\n"
        << "  \"weight\": " << bundle.recipe.weight << ",\n"
        << "  \"instructions_measure\": " << result.instructions_measure << ",\n"
        << "  \"cycles_measure\": " << result.cycles_measure << ",\n"
        << "  \"ipc_measure\": " << std::fixed << std::setprecision(6) << result.ipc_measure << "\n"
        << "}\n";

    std::ofstream(csv_path)
        << "benchmark,slice_id,weight,status,failure_reason,instructions_measure,cycles_measure,ipc_measure,result_json\n"
        << bundle.recipe.workload_name << ","
        << bundle.recipe.point_id << ","
        << bundle.recipe.weight << ","
        << result.status << ","
        << failureReasonName(result.failure_reason) << ","
        << result.instructions_measure << ","
        << result.cycles_measure << ","
        << result.ipc_measure << ","
        << json_path.string() << "\n";

    std::ofstream(root / (window.success ? "completed" : "abort")).put('\n');
    if (!window.success) {
        std::ofstream(root / "error.txt") << result.message << "\n";
    }
}

} // namespace riscv
```

- [ ] **Step 4: 在 `main.cpp` 中加入 checkpoint CLI 并分派到 runner**

```cpp
// 参数
std::string checkpointPath;
std::string checkpointRecipePath;
std::string checkpointImporterName = "external-process";
std::string checkpointRestorerPath;
std::string checkpointOutputDir;
uint64_t checkpointWarmupInstructions = 5'000'000;
uint64_t checkpointMeasureInstructions = 5'000'000;

// 解析
} else if (arg.find("--checkpoint=") == 0) {
    checkpointPath = arg.substr(13);
} else if (arg.find("--checkpoint-recipe=") == 0) {
    checkpointRecipePath = arg.substr(20);
} else if (arg.find("--checkpoint-importer=") == 0) {
    checkpointImporterName = arg.substr(22);
} else if (arg.find("--checkpoint-restorer=") == 0) {
    checkpointRestorerPath = arg.substr(22);
} else if (arg.find("--checkpoint-output-dir=") == 0) {
    checkpointOutputDir = arg.substr(24);
} else if (arg.find("--warmup-instructions=") == 0) {
    checkpointWarmupInstructions = std::stoull(arg.substr(22), nullptr, 0);
} else if (arg.find("--measure-instructions=") == 0) {
    checkpointMeasureInstructions = std::stoull(arg.substr(23), nullptr, 0);
}

// 分派
if (!checkpointPath.empty()) {
    if (checkpointRecipePath.empty()) {
        std::cerr << "Error: checkpoint 模式要求 --checkpoint-recipe\n";
        return 1;
    }

    CheckpointRunConfig config;
    config.checkpoint_path = checkpointPath;
    config.recipe_path = checkpointRecipePath;
    config.importer_name = checkpointImporterName;
    config.restorer_path = checkpointRestorerPath;
    config.output_dir = checkpointOutputDir.empty()
        ? "build/spec06-single/latest"
        : checkpointOutputDir;
    config.warmup_instructions = checkpointWarmupInstructions;
    config.measure_instructions = checkpointMeasureInstructions;

    CheckpointRunner runner(createCheckpointImporter(config.importer_name));
    const auto result = runner.runSingle(config);
    return result.success ? 0 : 1;
}
```

- [ ] **Step 5: 运行 runner/CLI 聚焦测试**

Run: `ctest --test-dir build --output-on-failure -R "CheckpointRunnerTest|SimulatorTest"`

Expected:
- `CheckpointRunnerTest` 通过
- `SimulatorTest` 继续通过

- [ ] **Step 6: 提交 runner 与 CLI 阶段**

```bash
git add include/system/checkpoint_runner.h \
        src/system/checkpoint_runner.cpp \
        src/system/CMakeLists.txt \
        src/main.cpp \
        tests/test_checkpoint_runner.cpp
git commit -m "feat: 接入 SPEC06 单切片 runner 与 CLI"
```

### Task 5: 构建验证与 smoke 跑通

**Files:**
- Modify: `docs/superpowers/plans/2026-04-10-spec06-single-slice.md`（勾选执行状态）

- [ ] **Step 1: 全量编译项目**

Run: `cmake --build build -j`

Expected:
- `risc-v-sim` 与 `risc-v-tests` 编译通过

- [ ] **Step 2: 运行新增聚焦测试**

Run: `ctest --test-dir build --output-on-failure -R "CheckpointRecipeTest|CheckpointImporterTest|CheckpointRunnerTest|SimulatorTest"`

Expected:
- 新增 checkpoint 相关单测全部通过
- 现有 `SimulatorTest` 无回退

- [ ] **Step 3: 用 fake importer 做一次端到端 smoke**

Run:

```bash
tmpdir=$(mktemp -d)
python3 - <<'PY' >"$tmpdir/program.bin"
import struct
import sys
for _ in range(64):
    sys.stdout.buffer.write(struct.pack("<I", 0x00108093))
sys.stdout.buffer.write(struct.pack("<I", 0x00000073))
PY
printf '\n' >"$tmpdir/fake.zstd"
cat >"$tmpdir/fake_initramfs-spec.txt" <<'EOF'
file /spec0/task0.sh /tmp/spec/fake_task0.sh 755 0 0
file /spec0/run.sh /tmp/spec/fake_run.sh 755 0 0
EOF
cat >"$tmpdir/fake-importer.sh" <<EOF
#!/bin/sh
echo "pc=0x0"
echo "extensions=0x1"
echo "segment=0x0:$tmpdir/program.bin"
EOF
chmod +x "$tmpdir/fake-importer.sh"
```

```bash
./build/risc-v-sim \
  --ooo \
  --checkpoint="$tmpdir/fake.zstd" \
  --checkpoint-recipe="$tmpdir/fake_initramfs-spec.txt" \
  --checkpoint-restorer="$tmpdir/fake-importer.sh" \
  --checkpoint-output-dir="$tmpdir/out" \
  --warmup-instructions=16 \
  --measure-instructions=16
```

Expected:
- 退出码为 `0`
- `$tmpdir/out/completed` 存在
- `$tmpdir/out/result.json` 中 `status=completed`

- [ ] **Step 4: 提交验证与收尾阶段**

```bash
git add docs/superpowers/plans/2026-04-10-spec06-single-slice.md
git commit -m "docs: 勾选 SPEC06 单切片实现计划执行状态"
```

## Self-Review

- **Spec coverage:** 计划覆盖了 spec 中的五个核心面：`checkpoint + recipe` 输入契约、标准化快照、恢复 API、窗口执行、结果落盘与失败分类。
- **Placeholder scan:** 未留下 `TODO/TBD/类似 Task N` 这类占位词；smoke 步骤只依赖计划内构造的 fake importer，不依赖外部环境路径。
- **Type consistency:** `CheckpointRunConfig`、`SnapshotBundle`、`InstructionWindowResult`、`CheckpointRunResult` 在任务之间保持统一命名，没有后续步骤改名。
