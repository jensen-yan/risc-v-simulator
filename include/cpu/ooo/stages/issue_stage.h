#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 发射阶段实现
 * 负责从ROB中获取待发射指令，进行寄存器重命名，并发射到保留站
 */
class IssueStage : public PipelineStage {
public:
    IssueStage();
    virtual ~IssueStage() = default;
    
    // 实现PipelineStage接口
    void execute(CPUState& state) override;
    void flush() override;
    void reset() override;
    const char* get_stage_name() const override { return "ISSUE"; }

private:
    // 调试辅助方法
    void print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc);
};

} // namespace riscv 