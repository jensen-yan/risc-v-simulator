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
    // 处理系统调用
    void handle_ecall(CPUState& state);
    void handle_ebreak(CPUState& state);
    
    // 异常处理
    void handle_exception(CPUState& state, const std::string& exception_msg, uint32_t pc);
    
    // 调试辅助方法
    void print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc);
};

} // namespace riscv 