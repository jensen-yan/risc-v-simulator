#pragma once

#include "common/types.h"
#include "core/memory.h"
#include "core/decoder.h"
#include "cpu/ooo/register_rename.h"
#include "cpu/ooo/reservation_station.h"
#include "cpu/ooo/reorder_buffer.h"
#include "cpu/ooo/store_buffer.h"
#include "cpu/ooo/perf_counters.h"
#include "cpu/ooo/ooo_types.h"
#include "cpu/ooo/dynamic_inst.h"
#include "cpu/ooo/branch_predictor.h"
#include "cpu/ooo/cache/blocking_cache.h"
#include "system/syscall_handler.h"
#include "common/cpu_interface.h"
#include <array>
#include <memory>
#include <queue>

namespace riscv {

/**
 * 取指后的指令结构
 */
struct FetchedInstruction {
    uint64_t pc;
    Instruction instruction;
    bool is_compressed;
    uint64_t predicted_next_pc;
};

/**
 * 执行单元状态
 */
struct ExecutionUnit {
    struct DCacheAccessState {
        bool request_sent;
        bool waiting;

        DCacheAccessState() : request_sent(false), waiting(false) {}

        void reset() {
            request_sent = false;
            waiting = false;
        }
    };

    bool busy;
    int remaining_cycles;
    DynamicInstPtr instruction;    // 使用DynamicInst指针代替原来的副本
    uint64_t result;
    bool has_exception;
    std::string exception_msg;
    // 跳转指令相关字段
    uint64_t jump_target;
    bool is_jump;
    // Load指令相关字段
    uint64_t load_address;
    uint8_t load_size;
    DCacheAccessState dcache;
};

struct ICacheFetchState {
    ICacheFetchState() : wait_cycles_(0), request_pending_(false), request_pc_(0), pending_instruction_valid_(false), pending_instruction_(0) {}

    void reset() {
        wait_cycles_ = 0;
        request_pending_ = false;
        request_pc_ = 0;
        pending_instruction_valid_ = false;
        pending_instruction_ = 0;
    }

    bool hasPendingFor(uint64_t pc) const {
        return request_pending_ && pending_instruction_valid_ && request_pc_ == pc;
    }

    bool consumeIfMatch(uint64_t pc, Instruction& instruction_out) {
        if (!hasPendingFor(pc)) {
            return false;
        }
        instruction_out = pending_instruction_;
        reset();
        return true;
    }

    void startMissWait(uint64_t pc, Instruction instruction_in, int latency_cycles) {
        wait_cycles_ = (latency_cycles > 1) ? (latency_cycles - 1) : 0;
        request_pending_ = true;
        request_pc_ = pc;
        pending_instruction_valid_ = true;
        pending_instruction_ = instruction_in;
    }

    bool hasMissWait() const {
        return wait_cycles_ > 0;
    }

    // 消耗一个等待周期；返回 true 表示当前周期结束后仍需继续等待。
    bool advanceMissWaitCycle() {
        if (wait_cycles_ <= 0) {
            return false;
        }
        --wait_cycles_;
        return wait_cycles_ > 0;
    }

    int remainingWaitCycles() const {
        return wait_cycles_;
    }

private:
    int wait_cycles_;
    bool request_pending_;
    uint64_t request_pc_;
    bool pending_instruction_valid_;
    Instruction pending_instruction_;
};

inline void resetExecutionUnitState(ExecutionUnit& unit) {
    unit.busy = false;
    unit.remaining_cycles = 0;
    unit.instruction = nullptr;
    unit.result = 0;
    unit.has_exception = false;
    unit.exception_msg.clear();
    unit.jump_target = 0;
    unit.is_jump = false;
    unit.load_address = 0;
    unit.load_size = 0;
    unit.dcache.reset();
}

template<typename UnitContainer>
inline void resetExecutionUnitContainer(UnitContainer& units) {
    for (auto& unit : units) {
        resetExecutionUnitState(unit);
    }
}

/**
 * CPU共享状态结构
 * 包含所有流水线阶段需要访问的共享数据
 */
struct CPUState {
    // 基本CPU状态
    uint64_t pc;                    // 程序计数器（取指PC）
    bool halted;                    // 停机标志
    uint64_t instruction_count;     // 指令计数器
    uint64_t cycle_count;          // 周期计数器
    uint32_t enabled_extensions;   // 启用的扩展
    
    // 寄存器文件
    static constexpr size_t NUM_REGISTERS = 32;
    static constexpr size_t NUM_FP_REGISTERS = 32;
    
    std::array<uint64_t, NUM_REGISTERS> arch_registers;     // 架构寄存器
    std::array<uint64_t, NUM_FP_REGISTERS> arch_fp_registers; // 架构浮点寄存器
    std::array<uint64_t, 4096> csr_registers;               // CSR寄存器文件（12位地址空间）
    std::array<uint64_t, RegisterRenameUnit::NUM_PHYSICAL_REGS> physical_registers;    // 物理寄存器
    std::array<uint64_t, RegisterRenameUnit::NUM_PHYSICAL_REGS> physical_fp_registers; // 物理浮点寄存器
    
    // 流水线缓冲区
    std::queue<FetchedInstruction> fetch_buffer;  // 取指缓冲区
    std::queue<CommonDataBusEntry> cdb_queue;    // 通用数据总线队列
    
    // 核心组件（共享引用）
    std::shared_ptr<Memory> memory;
    Decoder decoder;
    std::unique_ptr<SyscallHandler> syscall_handler;
    
    // CPU接口引用，用于Stage中调用CPU方法
    ICpuInterface* cpu_interface;
    
    // 乱序执行组件
    std::unique_ptr<RegisterRenameUnit> register_rename;
    std::unique_ptr<ReservationStation> reservation_station;
    std::unique_ptr<ReorderBuffer> reorder_buffer;
    std::unique_ptr<StoreBuffer> store_buffer;  // Store Buffer用于Store-to-Load Forwarding

    // 分支预测器（Fetch使用；Commit更新；flush时保留状态）
    std::unique_ptr<BranchPredictor> branch_predictor;

    // L1 cache（时序+功能模型）
    std::unique_ptr<BlockingCache> l1i_cache;
    std::unique_ptr<BlockingCache> l1d_cache;
    ICacheFetchState icache;
    
    // 执行单元
    std::array<ExecutionUnit, 2> alu_units;      // 2个ALU单元
    std::array<ExecutionUnit, 1> branch_units;   // 1个分支单元
    std::array<ExecutionUnit, 1> load_units;     // 1个加载单元
    std::array<ExecutionUnit, 1> store_units;    // 1个存储单元
    
    // 性能统计
    PerfCounterBank perf_counters; // 结构化性能计数器
    uint64_t branch_mispredicts;   // 条件分支预测错误次数（仅B-type）
    uint64_t pipeline_stalls;      // 流水线停顿次数

    // A扩展 LR/SC 预留状态
    bool reservation_valid;        // LR 预留是否有效
    uint64_t reservation_addr;     // LR 预留地址
    
    // 调试支持
    uint64_t global_instruction_id;  // 全局指令序号
    
    // 构造函数 - 使用默认值和批量初始化
    CPUState() : 
        pc(0), halted(false), instruction_count(0), cycle_count(0),
        enabled_extensions(static_cast<uint32_t>(Extension::I) | 
                          static_cast<uint32_t>(Extension::M) | 
                          static_cast<uint32_t>(Extension::A) |
                          static_cast<uint32_t>(Extension::F) |
                          static_cast<uint32_t>(Extension::D) |
                          static_cast<uint32_t>(Extension::C)),
        cpu_interface(nullptr),
        branch_mispredicts(0), pipeline_stalls(0),
        reservation_valid(false), reservation_addr(0),
        global_instruction_id(0) {
        
        // 批量初始化所有寄存器为0
        arch_registers.fill(0);
        arch_fp_registers.fill(0);
        csr_registers.fill(0);
        physical_registers.fill(0);
        physical_fp_registers.fill(0);
        
        // 批量初始化执行单元
        resetExecutionUnits();
    }

    // 兼容旧统计字段，同时维护结构化计数器。
    void recordPipelineStall(PerfCounterId reason) {
        ++pipeline_stalls;
        perf_counters.increment(PerfCounterId::PIPELINE_STALLS);
        if (reason != PerfCounterId::PIPELINE_STALLS) {
            perf_counters.increment(reason);
        }
    }

    void recordBranchMispredict() {
        ++branch_mispredicts;
        perf_counters.increment(PerfCounterId::BRANCH_MISPREDICTS);
    }

    void resetExecutionUnits() {
        resetExecutionUnitContainer(alu_units);
        resetExecutionUnitContainer(branch_units);
        resetExecutionUnitContainer(load_units);
        resetExecutionUnitContainer(store_units);
    }
};

} // namespace riscv 
