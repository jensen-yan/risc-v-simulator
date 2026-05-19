#include "cpu/ooo/execute_dcache_access.h"

#include <algorithm>

namespace riscv {

void ExecuteDCacheAccess::recordResult(CPUState& state,
                                       CacheAccessType access_type,
                                       const CacheAccessResult& cache_result) {
    state.perf_counters.increment(PerfCounterId::CACHE_L1D_ACCESSES);
    if (access_type == CacheAccessType::Read) {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_READ_ACCESSES);
    } else {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_WRITE_ACCESSES);
    }

    if (cache_result.hit) {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_HITS);
    } else {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_MISSES);
    }

    if (cache_result.dirty_eviction) {
        state.perf_counters.increment(PerfCounterId::CACHE_L1D_DIRTY_EVICTIONS);
    }
}

bool ExecuteDCacheAccess::startOrWait(ExecutionUnit& unit,
                                      CPUState& state,
                                      CacheAccessType access_type,
                                      PerfCounterId stall_counter_id) {
    if (!state.l1d_cache) {
        unit.dcache.waiting = false;
        return true;
    }

    if (unit.dcache.request_sent) {
        unit.dcache.waiting = false;
        return true;
    }

    CacheAccessResult cache_result{};
    try {
        cache_result = state.l1d_cache->access(
            state.memory, unit.load_address, unit.load_size, access_type);
    } catch (const SimulatorException& e) {
        unit.has_exception = true;
        unit.exception_msg = e.what();
        unit.result = 0;
        unit.dcache.reset();
        return true;
    }
    if (cache_result.blocked) {
        unit.dcache.waiting = true;
        unit.remaining_cycles = 1;
        state.perf_counters.increment(stall_counter_id);
        return false;
    }

    recordResult(state, access_type, cache_result);

    unit.dcache.request_sent = true;
    unit.dcache.waiting = true;

    const int extra_cycles = std::max(0, cache_result.latency_cycles - 1);
    if (extra_cycles > 0) {
        unit.remaining_cycles = extra_cycles;
        state.perf_counters.increment(stall_counter_id, static_cast<uint64_t>(extra_cycles));
        return false;
    }

    unit.dcache.waiting = false;
    return true;
}

} // namespace riscv
