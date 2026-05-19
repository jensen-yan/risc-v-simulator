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

    struct YoungerThanRequest {
        uint64_t instruction_id = 0;
        ROBEntry rob_entry = 0;
        ExecutionUnitType current_unit_type = ExecutionUnitType::ALU;
        size_t current_unit_index = 0;
        bool has_redirect_pc = false;
        uint64_t redirect_pc = 0;
        const RegisterRenameUnit::Checkpoint* rename_checkpoint = nullptr;
    };

    struct Result {
        uint64_t flushed_rob_entries = 0;
        uint64_t fetch_buffer_dropped = 0;
        uint64_t flushed_cdb_entries = 0;
        bool flushed_l1d_inflight = false;
    };

    static const char* reasonName(Reason reason);
    static Result recoverFullPipeline(CPUState& state, const FullPipelineRequest& request);
    static Result recoverYoungerThan(CPUState& state, const YoungerThanRequest& request);

private:
    static void recordFlushCounters(CPUState& state, Reason reason, uint64_t flushed_rob_entries);
    static uint64_t flushYoungerCdbEntries(CPUState& state, uint64_t instruction_id);
    static bool flushYoungerExecutionUnits(CPUState& state, const YoungerThanRequest& request);
    static void restoreRenameCheckpointForSurvivingWork(CPUState& state,
                                                        uint64_t instruction_id,
                                                        const RegisterRenameUnit::Checkpoint& checkpoint);
};

} // namespace riscv
