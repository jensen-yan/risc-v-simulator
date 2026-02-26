#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 提交阶段实现
 * 负责按程序顺序提交完成的指令，维护精确异常语义
 */
class CommitStage : public PipelineStage {
public:
    CommitStage();
    virtual ~CommitStage() = default;
    
    // 实现PipelineStage接口
    void execute(CPUState& state) override;
    void flush() override;
    void reset() override;
    const char* get_stage_name() const override { return "COMMIT"; }

private:
    enum class FlushReason {
        BranchMispredict,
        UnconditionalRedirect,
        Trap,
        Mret,
        FenceI,
        Exception,
        Other
    };

    // 处理系统调用
    bool handle_ecall(CPUState& state, uint64_t instruction_pc);
    bool handle_ebreak(CPUState& state, uint64_t instruction_pc);
    bool handle_mret(CPUState& state);
    bool handle_fencei(CPUState& state, uint64_t instruction_pc, bool is_compressed);
    void enter_machine_trap(CPUState& state, uint64_t instruction_pc, uint64_t cause, uint64_t tval);
    
    // 异常处理
    void handle_exception(CPUState& state, const std::string& exception_msg, uint64_t pc);
    
    // 流水线刷新（用于跳转/异常返回/fence.i 等需要重新取指的场景）
    void flush_pipeline_after_commit(CPUState& state, FlushReason reason);
};

} // namespace riscv 
