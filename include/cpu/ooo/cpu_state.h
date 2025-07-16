#pragma once

#include "common/types.h"
#include "core/memory.h"
#include "core/decoder.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/ooo_types.h"
#include "system/syscall_handler.h"
#include <array>
#include <memory>
#include <queue>

namespace riscv {

/**
 * 取指后的指令结构
 */
struct FetchedInstruction {
    uint32_t pc;
    Instruction instruction;
    bool is_compressed;
};

/**
 * 执行单元状态
 */
struct ExecutionUnit {
    bool busy;
    int remaining_cycles;
    ReservationStationEntry instruction;
    uint32_t result;
    bool has_exception;
    std::string exception_msg;
    // 跳转指令相关字段
    uint32_t jump_target;
    bool is_jump;
};

/**
 * CPU共享状态结构
 * 包含所有流水线阶段需要访问的共享数据
 */
struct CPUState {
    // 基本CPU状态
    uint32_t pc;                    // 程序计数器
    bool halted;                    // 停机标志
    uint64_t instruction_count;     // 指令计数器
    uint64_t cycle_count;          // 周期计数器
    uint32_t enabled_extensions;   // 启用的扩展
    
    // 寄存器文件
    static constexpr size_t NUM_REGISTERS = 32;
    static constexpr size_t NUM_FP_REGISTERS = 32;
    
    std::array<uint32_t, NUM_REGISTERS> arch_registers;     // 架构寄存器
    std::array<uint32_t, NUM_FP_REGISTERS> arch_fp_registers; // 架构浮点寄存器
    std::array<uint32_t, RegisterRenameUnit::NUM_PHYSICAL_REGS> physical_registers;    // 物理寄存器
    std::array<uint32_t, RegisterRenameUnit::NUM_PHYSICAL_REGS> physical_fp_registers; // 物理浮点寄存器
    
    // 流水线缓冲区
    std::queue<FetchedInstruction> fetch_buffer;  // 取指缓冲区
    std::queue<CommonDataBusEntry> cdb_queue;    // 通用数据总线队列
    
    // 核心组件（共享引用）
    std::shared_ptr<Memory> memory;
    Decoder decoder;
    std::unique_ptr<SyscallHandler> syscall_handler;
    
    // 乱序执行组件
    std::unique_ptr<RegisterRenameUnit> register_rename;
    std::unique_ptr<ReservationStation> reservation_station;
    std::unique_ptr<ReorderBuffer> reorder_buffer;
    
    // 执行单元
    std::array<ExecutionUnit, 2> alu_units;      // 2个ALU单元
    std::array<ExecutionUnit, 1> branch_units;   // 1个分支单元
    std::array<ExecutionUnit, 1> load_units;     // 1个加载单元
    std::array<ExecutionUnit, 1> store_units;    // 1个存储单元
    
    // 性能统计
    uint64_t branch_mispredicts;   // 分支预测错误次数
    uint64_t pipeline_stalls;      // 流水线停顿次数
    
    // 调试支持
    uint64_t global_instruction_id;  // 全局指令序号
    
    // 构造函数 - 使用默认值和批量初始化
    CPUState() : 
        pc(0), halted(false), instruction_count(0), cycle_count(0),
        enabled_extensions(static_cast<uint32_t>(Extension::I) | 
                          static_cast<uint32_t>(Extension::M) | 
                          static_cast<uint32_t>(Extension::F) | 
                          static_cast<uint32_t>(Extension::C)),
        branch_mispredicts(0), pipeline_stalls(0), global_instruction_id(0) {
        
        // 批量初始化所有寄存器为0
        arch_registers.fill(0);
        arch_fp_registers.fill(0);
        physical_registers.fill(0);
        physical_fp_registers.fill(0);
        
        // 批量初始化执行单元
        initializeExecutionUnits();
    }

private:
    void initializeExecutionUnits() {
        auto initUnit = [](ExecutionUnit& unit) {
            unit = {false, 0, {}, 0, false, "", 0, false};
        };
        
        for (auto& unit : alu_units) initUnit(unit);
        for (auto& unit : branch_units) initUnit(unit);
        for (auto& unit : load_units) initUnit(unit);
        for (auto& unit : store_units) initUnit(unit);
    }
};

} // namespace riscv 