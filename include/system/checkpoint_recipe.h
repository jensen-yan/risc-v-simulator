#pragma once

#include "system/checkpoint_types.h"

#include <string>

namespace riscv {

std::string resolveCheckpointRecipePath(const std::string& checkpoint_path,
                                        const std::string& recipe_path);

CheckpointRecipeSpec loadCheckpointRecipeSpec(const std::string& checkpoint_path,
                                              const std::string& recipe_path);

} // namespace riscv
