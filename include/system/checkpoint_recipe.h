#pragma once

#include "system/checkpoint_types.h"

#include <string>

namespace riscv {

CheckpointRecipeSpec loadCheckpointRecipeSpec(const std::string& checkpoint_path,
                                              const std::string& recipe_path);

} // namespace riscv
