#pragma once

#include "common/types.h"

#include <cstdint>

namespace riscv {

class ExecuteLoadValue {
public:
    static uint64_t format(const DecodedInstruction& inst, uint8_t access_size, uint64_t raw_value);
};

} // namespace riscv
