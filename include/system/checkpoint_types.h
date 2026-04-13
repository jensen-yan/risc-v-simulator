#pragma once

#include "common/types.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace riscv {

struct MemorySegment {
    Address base = 0;
    std::vector<uint8_t> bytes;
    std::string file_path;
    uint64_t size = 0;
    bool ephemeral = false;

    bool isFileBacked() const { return !file_path.empty(); }
    uint64_t byteSize() const { return isFileBacked() ? size : static_cast<uint64_t>(bytes.size()); }
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
