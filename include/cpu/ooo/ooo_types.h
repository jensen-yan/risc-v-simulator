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

// 前向声明
class DynamicInst;
using DynamicInstPtr = std::shared_ptr<DynamicInst>;

// 公共数据总线项 - 使用DynamicInst指针保持数据一致性
struct CommonDataBusEntry {
    DynamicInstPtr instruction;    // 直接使用DynamicInst指针作为数据源
    bool valid;
    
    CommonDataBusEntry() : instruction(nullptr), valid(false) {}
    explicit CommonDataBusEntry(DynamicInstPtr inst) : instruction(inst), valid(true) {}
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