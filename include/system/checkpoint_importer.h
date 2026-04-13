#pragma once

#include "system/checkpoint_types.h"

#include <memory>
#include <string>

namespace riscv {

struct CheckpointRunConfig {
    std::string checkpoint_path;
    std::string recipe_path;
    std::string importer_name;
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

class ExternalProcessCheckpointImporter : public ICheckpointImporter {
public:
    explicit ExternalProcessCheckpointImporter(std::string importer_command);

    SnapshotBundle importCheckpoint(const CheckpointRunConfig& config) const override;

private:
    std::string importer_command_;
};

std::unique_ptr<ICheckpointImporter> createCheckpointImporter(const std::string& importer_name);

} // namespace riscv
