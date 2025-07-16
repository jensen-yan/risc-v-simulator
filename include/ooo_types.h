#pragma once

#include "types.h"
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

// 物理寄存器状态
struct PhysicalRegister {
    uint32_t value;
    bool ready;             // 是否准备好
    ROBEntry producer_rob;  // 产生这个值的ROB表项
    
    PhysicalRegister() : value(0), ready(true), producer_rob(0) {}
};

// 重命名表项
struct RenameEntry {
    PhysRegNum physical_reg;
    bool valid;
    
    RenameEntry() : physical_reg(0), valid(false) {}
};

// 保留站表项
struct ReservationStationEntry {
    DecodedInstruction instruction;
    
    // 指令跟踪
    uint64_t instruction_id;    // 全局指令序号
    
    // 操作数信息
    bool src1_ready;
    bool src2_ready;
    uint32_t src1_value;
    uint32_t src2_value;
    PhysRegNum src1_reg;
    PhysRegNum src2_reg;
    
    // 目标寄存器
    PhysRegNum dest_reg;
    
    // 关联的ROB表项
    ROBEntry rob_entry;
    
    // 是否有效
    bool valid;
    
    // 指令地址
    uint32_t pc;
    
    ReservationStationEntry() : instruction_id(0), src1_ready(false), src2_ready(false), 
                               src1_value(0), src2_value(0), 
                               src1_reg(0), src2_reg(0), dest_reg(0),
                               rob_entry(0), valid(false), pc(0) {}
};

// 重排序缓冲表项
struct ReorderBufferEntry {
    DecodedInstruction instruction;
    
    // 指令跟踪
    uint64_t instruction_id;    // 全局指令序号
    
    // 指令状态
    enum class State {
        ALLOCATED,   // 已分配到ROB，等待发射
        ISSUED,      // 已发射到保留站，等待调度
        EXECUTING,   // 正在执行
        COMPLETED,   // 执行完成
        RETIRED      // 已退休
    } state;
    
    // 结果信息
    uint32_t result;
    bool result_ready;
    
    // 目标寄存器
    RegNum logical_dest;    // 逻辑寄存器
    PhysRegNum physical_dest;  // 物理寄存器
    
    // 异常信息
    bool has_exception;
    std::string exception_msg;
    
    // 指令地址
    uint32_t pc;
    
    // 跳转指令相关（is_jump=true表示此指令提交时需要改变PC）
    bool is_jump;           // 是否需要跳转（包括条件分支taken和无条件跳转）
    uint32_t jump_target;   // 跳转目标地址
    
    // 是否有效
    bool valid;
    
    ReorderBufferEntry() : instruction_id(0), state(State::ALLOCATED), result(0), result_ready(false),
                          logical_dest(0), physical_dest(0), has_exception(false),
                          pc(0), is_jump(false), jump_target(0), valid(false) {}
};

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