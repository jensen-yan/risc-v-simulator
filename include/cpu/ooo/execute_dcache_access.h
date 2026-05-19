#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class ExecuteDCacheAccess {
public:
    static void recordResult(CPUState& state,
                             CacheAccessType access_type,
                             const CacheAccessResult& cache_result);

    static bool startOrWait(ExecutionUnit& unit,
                            CPUState& state,
                            CacheAccessType access_type,
                            PerfCounterId stall_counter_id);
};

} // namespace riscv
