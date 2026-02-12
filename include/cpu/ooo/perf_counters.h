#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace riscv {

enum class PerfCounterId : uint16_t {
#define PERF_COUNTER_DEF(id, name, desc) id,
#include "cpu/ooo/perf_counter_defs.inc"
#undef PERF_COUNTER_DEF

    COUNT
};

struct PerfCounterMeta {
    const char* name;
    const char* description;
};

class PerfCounterBank {
public:
    static constexpr size_t NUM_COUNTERS = static_cast<size_t>(PerfCounterId::COUNT);

    void reset();
    void increment(PerfCounterId id, uint64_t delta = 1);
    uint64_t value(PerfCounterId id) const;

    const std::array<uint64_t, NUM_COUNTERS>& raw() const { return counters_; }

    static const PerfCounterMeta& meta(PerfCounterId id);
    static const std::array<PerfCounterMeta, NUM_COUNTERS>& metadata();

private:
    static constexpr size_t toIndex(PerfCounterId id) {
        return static_cast<size_t>(id);
    }

    std::array<uint64_t, NUM_COUNTERS> counters_{};
};

} // namespace riscv
