#pragma once

#include "cpu/ooo/cpu_state.h"

namespace riscv {

/**
 * 乱序执行语义模块
 * 仅负责单条指令语义计算，不处理调度、执行单元分配和流水线推进。
 */
class OOOExecuteSemantics {
public:
    static void executeInstruction(ExecutionUnit& unit, const DynamicInstPtr& instruction, CPUState& state);
};

} // namespace riscv
