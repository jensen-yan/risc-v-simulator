#include "cpu/ooo/execute_load_value.h"

namespace riscv {

uint64_t ExecuteLoadValue::format(const DecodedInstruction& inst,
                                  uint8_t access_size,
                                  uint64_t raw_value) {
    if (inst.opcode == Opcode::LOAD_FP) {
        if (access_size == 4) {
            return 0xFFFFFFFF00000000ULL | (raw_value & 0xFFFFFFFFULL);
        }
        return raw_value;
    }

    if (inst.is_signed_load) {
        switch (access_size) {
            case 1:
                return static_cast<uint64_t>(static_cast<int8_t>(raw_value & 0xFF));
            case 2:
                return static_cast<uint64_t>(static_cast<int16_t>(raw_value & 0xFFFF));
            case 4:
                return static_cast<uint64_t>(static_cast<int32_t>(raw_value & 0xFFFFFFFF));
            case 8:
            default:
                return raw_value;
        }
    }

    switch (access_size) {
        case 1:
            return raw_value & 0xFF;
        case 2:
            return raw_value & 0xFFFF;
        case 4:
            return raw_value & 0xFFFFFFFF;
        case 8:
        default:
            return raw_value;
    }
}

} // namespace riscv
