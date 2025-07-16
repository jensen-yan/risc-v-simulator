#pragma once

#include "common/types.h"

namespace riscv {

// 前向声明
struct CPUState;

/**
 * 流水线阶段基础接口
 * 所有流水线阶段都必须实现这个接口
 */
class PipelineStage {
public:
    virtual ~PipelineStage() = default;
    
    /**
     * 执行该阶段的逻辑
     * @param state CPU共享状态
     */
    virtual void execute(CPUState& state) = 0;
    
    /**
     * 刷新该阶段的状态（用于分支预测错误等情况）
     */
    virtual void flush() = 0;
    
    /**
     * 重置该阶段到初始状态
     */
    virtual void reset() = 0;
    
    /**
     * 获取该阶段的名称（用于调试）
     */
    virtual const char* get_stage_name() const = 0;
};

} // namespace riscv 