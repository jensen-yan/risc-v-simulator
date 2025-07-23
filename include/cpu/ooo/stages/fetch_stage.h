#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 取指阶段实现
 * 负责从内存中取指令并放入取指缓冲区
 */
class FetchStage : public PipelineStage {
public:
    FetchStage();
    virtual ~FetchStage() = default;
    
    // 实现PipelineStage接口
    void execute(CPUState& state) override;
    void flush() override;
    void reset() override;
    const char* get_stage_name() const override { return "FETCH"; }

private:
    static constexpr size_t MAX_FETCH_BUFFER_SIZE = 4;  // 取指缓冲区最大大小
};

} // namespace riscv 