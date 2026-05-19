#pragma once

#include "common/types.h"

namespace riscv {

/**
 * 流水线阶段基础接口。
 *
 * 这里只保留稳定的观测面。各阶段的执行 Interface 可以按阶段逐步收窄，
 * 避免共同基类强制所有 Stage 暴露同一个 CPUState 级宽接口。
 */
class PipelineStage {
public:
    virtual ~PipelineStage() = default;

    /**
     * 获取该阶段的名称（用于调试）
     */
    virtual const char* get_stage_name() const = 0;
};

} // namespace riscv
