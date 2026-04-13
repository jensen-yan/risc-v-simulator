#pragma once

#include "common/cpu_interface.h"
#include "system/checkpoint_importer.h"
#include "system/checkpoint_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace riscv {

struct CheckpointRunResult {
    std::string status = "abort";
    bool success = false;
    CheckpointFailureReason failure_reason = CheckpointFailureReason::UNKNOWN;
    std::string message;
    std::string benchmark;
    std::string workload_name;
    std::string slice_id;
    double weight = 0.0;
    uint64_t instructions_measure = 0;
    uint64_t cycles_measure = 0;
    double ipc_measure = 0.0;
};

class CheckpointRunner {
public:
    using ImporterFactory =
        std::function<std::unique_ptr<ICheckpointImporter>(const std::string& importer_name)>;

    CheckpointRunner(CpuType cpu_type, size_t memory_size, ImporterFactory importer_factory = {});

    void setMaxInOrderInstructions(uint64_t limit);
    void setMaxOutOfOrderCycles(uint64_t limit);

    CheckpointRunResult run(const CheckpointRunConfig& config) const;

private:
    CpuType cpu_type_;
    size_t memory_size_;
    uint64_t max_in_order_instructions_ = 5000000;
    uint64_t max_out_of_order_cycles_ = 50000;
    ImporterFactory importer_factory_;
};

} // namespace riscv
