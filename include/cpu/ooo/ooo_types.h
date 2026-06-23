#pragma once

#include "common/types.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <memory>

namespace riscv {

// 物理寄存器编号类型
using PhysRegNum = uint8_t;

enum class RegisterFileKind : uint8_t {
    None = 0,
    Integer,
    FloatingPoint,
};

// 重排序缓冲表项编号
using ROBEntry = uint16_t;

// 保留站编号
using RSEntry = uint8_t;

// 乱序执行相关的数据结构

// 执行单元类型
enum class ExecutionUnitType {
    ALU,        // 算术逻辑单元
    FP,         // 浮点算术单元
    BRANCH,     // 分支单元
    LOAD,       // 加载单元
    STORE       // 存储单元
};

// 前向声明
class DynamicInst;
using DynamicInstPtr = std::shared_ptr<DynamicInst>;

// 执行完成事件 - 使用DynamicInst指针保持数据一致性
struct CompletionEvent {
    DynamicInstPtr instruction;    // 直接使用DynamicInst指针作为数据源
    bool valid;
    
    CompletionEvent() : instruction(nullptr), valid(false) {}
    explicit CompletionEvent(DynamicInstPtr inst) : instruction(inst), valid(true) {}
};

// 分支预测结果
struct BranchPrediction {
    bool taken;
    uint64_t target;
    
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

struct OOOPipelineConfig {
    static constexpr size_t FETCH_WIDTH = 4;
    static constexpr size_t DECODE_WIDTH = 4;
    static constexpr size_t DISPATCH_WIDTH = 4;
    static constexpr size_t ISSUE_WIDTH = 4;
    static constexpr size_t WRITEBACK_WIDTH = 2;
    static constexpr size_t COMPLETION_WIDTH = WRITEBACK_WIDTH;
    static constexpr size_t COMMIT_WIDTH = 4;
    static constexpr size_t STORE_COMMIT_WIDTH = 1;
    static constexpr size_t RECOVERY_REDIRECT_LATENCY = 2;
    static constexpr size_t MEMORY_REPLAY_WIDTH = 2;
    static constexpr size_t FETCH_BUFFER_SIZE = 48;
    static constexpr size_t ROB_ENTRIES = 192;
    static constexpr size_t RS_ENTRIES = 96;
    static constexpr size_t PHYSICAL_REGS = 256;
    static constexpr size_t ALU_UNITS = 4;
    static constexpr size_t FP_UNITS = 4;
    static constexpr size_t BRANCH_UNITS = 2;
    static constexpr size_t LOAD_UNITS = 4;
    static constexpr size_t STORE_UNITS = 4;
    static constexpr size_t MEMORY_INFLIGHT_ENTRIES = 16;
    static constexpr size_t STORE_BUFFER_ENTRIES = ROB_ENTRIES;
};


} // namespace riscv
