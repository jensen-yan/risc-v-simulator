#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 译码阶段实现
 * 负责从取指缓冲区中取指令，进行译码并分配ROB表项
 */
class DecodeStage : public PipelineStage {
public:
    DecodeStage();
    virtual ~DecodeStage() = default;
    
    // 实现PipelineStage接口
    void execute(CPUState& state) override;
    void flush() override;
    void reset() override;
    const char* get_stage_name() const override { return "DECODE"; }

private:
    // 调试辅助方法
    void print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc);
};

} // namespace riscv 