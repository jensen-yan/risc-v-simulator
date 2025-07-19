#pragma once

#include "common/types.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>

namespace riscv {

// 物理寄存器编号类型
using PhysRegNum = uint8_t;

// 重排序缓冲表项编号
using ROBEntry = uint16_t;

// 保留站编号
using RSEntry = uint8_t;

// 乱序执行相关的数据结构

// 执行单元类型
enum class ExecutionUnitType {
    ALU,        // 算术逻辑单元
    BRANCH,     // 分支单元
    LOAD,       // 加载单元
    STORE       // 存储单元
};

// 公共数据总线项
struct CommonDataBusEntry {
    PhysRegNum dest_reg;
    uint32_t value;
    ROBEntry rob_entry;
    bool valid;
    // 跳转指令相关（从执行单元传递到ROB的控制流信息）
    bool is_jump;           // 是否需要跳转
    uint32_t jump_target;   // 跳转目标地址
    
    CommonDataBusEntry() : dest_reg(0), value(0), rob_entry(0), valid(false), 
                          is_jump(false), jump_target(0) {}
};

// 分支预测结果
struct BranchPrediction {
    bool taken;
    uint32_t target;
    
    BranchPrediction() : taken(false), target(0) {}
};

// 乱序执行统计信息
struct OOOStatistics {
    uint64_t total_instructions;
    uint64_t branch_mispredictions;
    uint64_t pipeline_flushes;
    uint64_t rob_full_stalls;
    uint64_t rs_full_stalls;
    uint64_t rename_stalls;
    
    OOOStatistics() : total_instructions(0), branch_mispredictions(0),
                     pipeline_flushes(0), rob_full_stalls(0), 
                     rs_full_stalls(0), rename_stalls(0) {}
};


} // namespace riscv