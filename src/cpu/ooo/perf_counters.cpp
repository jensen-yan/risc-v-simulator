#include "cpu/ooo/perf_counters.h"

namespace riscv {

namespace {

const std::array<PerfCounterMeta, PerfCounterBank::NUM_COUNTERS> kCounterMeta = {{
#define PERF_COUNTER_DEF(id, name, desc) {name, desc},
#include "cpu/ooo/perf_counter_defs.inc"
#undef PERF_COUNTER_DEF
}};

static_assert(kCounterMeta.size() == PerfCounterBank::NUM_COUNTERS,
              "perf counter metadata and enum definition are out of sync");

} // namespace

void PerfCounterBank::reset() {
    counters_.fill(0);
}

void PerfCounterBank::increment(PerfCounterId id, uint64_t delta) {
    counters_[toIndex(id)] += delta;
}

uint64_t PerfCounterBank::value(PerfCounterId id) const {
    return counters_[toIndex(id)];
}

const PerfCounterMeta& PerfCounterBank::meta(PerfCounterId id) {
    return kCounterMeta[toIndex(id)];
}

const std::array<PerfCounterMeta, PerfCounterBank::NUM_COUNTERS>& PerfCounterBank::metadata() {
    return kCounterMeta;
}

} // namespace riscv
