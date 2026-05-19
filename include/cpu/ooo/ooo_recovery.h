#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

class OooRecovery {
public:
    enum class Reason {
        BranchMispredict,
        UnconditionalRedirect,
        Trap,
        Mret,
        FenceI,
        MemoryOrderViolation,
        Exception,
        Other
    };

    struct FullPipelineRequest {
        Reason reason = Reason::Other;
        bool clear_reservation = true;
        bool reset_execution_units = true;
    };

    struct Result {
        uint64_t flushed_rob_entries = 0;
        uint64_t fetch_buffer_dropped = 0;
        uint64_t flushed_cdb_entries = 0;
        bool flushed_l1d_inflight = false;
    };

    static const char* reasonName(Reason reason);
    static Result recoverFullPipeline(CPUState& state, const FullPipelineRequest& request);

private:
    static void recordFlushCounters(CPUState& state, Reason reason, uint64_t flushed_rob_entries);
};

} // namespace riscv
