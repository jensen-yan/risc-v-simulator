#pragma once

#include "cpu/ooo/pipeline_stage.h"
#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 写回阶段实现
 * 负责处理CDB队列中的写回请求，更新保留站、寄存器重命名和ROB状态
 */
class WritebackStage : public PipelineStage {
public:
    WritebackStage();
    virtual ~WritebackStage() = default;
    
    // 实现PipelineStage接口
    void execute(CPUState& state) override;
    void flush() override;
    void reset() override;
    const char* get_stage_name() const override { return "WRITEBACK"; }

private:
    // 调试辅助方法
    void print_stage_activity(const std::string& activity, uint64_t cycle, uint32_t pc);
};

} // namespace riscv 