#pragma once

#include "types.h"
#include "memory.h"
#include "decoder.h"
#include "register_rename.h"
#include "reservation_station.h"
#include "reorder_buffer.h"
#include "ooo_types.h"
#include <array>
#include <memory>
#include <queue>

namespace riscv {

class SyscallHandler;

/**
 * 乱序执行RISC-V CPU核心类
 * 
 * 功能：
 * 1. 实现完整的乱序执行流水线
 * 2. 集成寄存器重命名、保留站、重排序缓冲等组件
 * 3. 支持精确异常处理
 * 4. 支持分支预测错误恢复
 * 5. 维护程序语义正确性
 */
class OutOfOrderCPU {
public:
    static constexpr size_t NUM_REGISTERS = 32;
    static constexpr size_t NUM_FP_REGISTERS = 32;
    static constexpr size_t PIPELINE_WIDTH = 4;  // 流水线宽度
    
    explicit OutOfOrderCPU(std::shared_ptr<Memory> memory);
    ~OutOfOrderCPU();
    
    // 禁用拷贝构造和赋值
    OutOfOrderCPU(const OutOfOrderCPU&) = delete;
    OutOfOrderCPU& operator=(const OutOfOrderCPU&) = delete;
    
    // 执行控制
    void step();                    // 单步执行（执行一个时钟周期）
    void run();                     // 连续执行直到结束
    void reset();                   // 重置CPU状态
    
    // 寄存器访问（架构寄存器值）
    uint32_t getRegister(RegNum reg) const;
    void setRegister(RegNum reg, uint32_t value);
    
    // 浮点寄存器访问
    uint32_t getFPRegister(RegNum reg) const;
    void setFPRegister(RegNum reg, uint32_t value);
    float getFPRegisterFloat(RegNum reg) const;
    void setFPRegisterFloat(RegNum reg, float value);
    
    // 程序计数器
    uint32_t getPC() const { return pc_; }
    void setPC(uint32_t pc) { pc_ = pc; }
    
    // 状态查询
    bool isHalted() const { return halted_; }
    uint64_t getInstructionCount() const { return instruction_count_; }
    uint64_t getCycleCount() const { return cycle_count_; }
    
    // 扩展支持
    void setEnabledExtensions(uint32_t extensions) { enabled_extensions_ = extensions; }
    uint32_t getEnabledExtensions() const { return enabled_extensions_; }
    
    // 性能统计
    void getPerformanceStats(uint64_t& instructions, uint64_t& cycles, 
                            uint64_t& branch_mispredicts, uint64_t& stalls) const;
    
    // 调试功能
    void dumpRegisters() const;
    void dumpState() const;
    void dumpPipelineState() const;
    
private:
    // 内存和基础组件
    std::shared_ptr<Memory> memory_;
    Decoder decoder_;
    std::unique_ptr<SyscallHandler> syscall_handler_;
    
    // 乱序执行组件
    std::unique_ptr<RegisterRenameUnit> register_rename_;
    std::unique_ptr<ReservationStation> reservation_station_;
    std::unique_ptr<ReorderBuffer> reorder_buffer_;
    
    // 物理寄存器文件
    std::array<uint32_t, RegisterRenameUnit::NUM_PHYSICAL_REGS> physical_registers_;
    std::array<uint32_t, RegisterRenameUnit::NUM_PHYSICAL_REGS> physical_fp_registers_;
    
    // 架构寄存器文件（用于提交阶段）
    std::array<uint32_t, NUM_REGISTERS> arch_registers_;
    std::array<uint32_t, NUM_FP_REGISTERS> arch_fp_registers_;
    
    // CPU状态
    uint32_t pc_;                   // 程序计数器
    bool halted_;                   // 停机标志
    uint64_t instruction_count_;    // 指令计数器
    uint64_t cycle_count_;          // 周期计数器
    uint32_t enabled_extensions_;   // 启用的扩展
    
    // 取指相关
    struct FetchedInstruction {
        uint32_t pc;
        Instruction instruction;
        bool is_compressed;
    };

    // 取指缓冲区
    std::queue<FetchedInstruction> fetch_buffer_;
    
    // 执行单元状态
    struct ExecutionUnit {
        bool busy;
        int remaining_cycles;
        ReservationStationEntry instruction;
        uint32_t result;
        bool has_exception;
        std::string exception_msg;
    };
    
    std::array<ExecutionUnit, 2> alu_units_;
    std::array<ExecutionUnit, 1> branch_units_;
    std::array<ExecutionUnit, 1> load_units_;
    std::array<ExecutionUnit, 1> store_units_;
    
    // 通用数据总线
    std::queue<CommonDataBusEntry> cdb_queue_;
    
    // 性能统计
    uint64_t branch_mispredicts_;
    uint64_t pipeline_stalls_;
    
    // 调试支持
    uint64_t global_instruction_id_;     // 全局指令序号
    bool debug_enabled_;                 // 是否启用调试输出
    
    // 调试辅助方法
    void print_cycle_header();          // 打印周期头部信息
    void print_stage_activity(const std::string& stage, const std::string& activity);
    std::string get_instruction_debug_info(uint64_t inst_id, uint32_t pc, const std::string& mnemonic);
    
    // 流水线阶段
    void fetch_stage();                    // 取指阶段
    void decode_stage();                   // 译码阶段
    void issue_stage();                    // 发射阶段
    void execute_stage();                  // 执行阶段
    void writeback_stage();                // 写回阶段
    void commit_stage();                   // 提交阶段
    
    // 指令执行
    void execute_instruction(ExecutionUnit& unit, const ReservationStationEntry& entry);
    uint32_t execute_alu_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2);
    bool execute_branch_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2);
    uint32_t execute_load_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2);
    void execute_store_operation(const DecodedInstruction& inst, uint32_t src1, uint32_t src2);
    
    // 异常处理
    void handle_exception(const std::string& exception_msg, uint32_t pc);
    void flush_pipeline();
    
    // 分支预测（简化实现）
    bool predict_branch(uint32_t pc);
    void update_branch_predictor(uint32_t pc, bool taken);
    
    // 系统调用处理
    void handleEcall();
    void handleEbreak();
    
    // 辅助方法
    uint32_t get_physical_register_value(PhysRegNum reg) const;
    void set_physical_register_value(PhysRegNum reg, uint32_t value);
    uint32_t get_physical_fp_register_value(PhysRegNum reg) const;
    void set_physical_fp_register_value(PhysRegNum reg, uint32_t value);
    
    // 执行单元管理
    void initialize_execution_units();
    ExecutionUnit* get_available_unit(ExecutionUnitType type);
    void update_execution_units();
    
    // 内存访问辅助方法
    uint32_t loadFromMemory(Address addr, Funct3 funct3);
    void storeToMemory(Address addr, uint32_t value, Funct3 funct3);
    
    // 立即数符号扩展
    int32_t signExtend(uint32_t value, int bits) const;
    
    // 初始化方法
    void initialize_components();
    void initialize_registers();
};

} // namespace riscv