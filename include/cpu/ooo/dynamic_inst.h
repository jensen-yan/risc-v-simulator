#pragma once

#include "common/types.h"
#include "cpu/ooo/ooo_types.h"
#include <memory>
#include <string>
#include <optional>

namespace riscv {

// 前向声明
class ReorderBuffer;
class ReservationStation;

/**
 * 动态指令类 - 乱序执行中指令的统一载体
 * 
 * 该类整合了原本分散在 ReorderBufferEntry、ReservationStationEntry 
 * 和 ExecutionUnit 中的重复数据，实现了 Single Source of Truth 设计原则。
 * 
 * 功能特性：
 * 1. 统一存储指令的所有静态和动态信息
 * 2. 支持指令生命周期的完整状态跟踪
 * 3. 提供清晰的状态转换接口
 * 4. 支持可扩展的调试和分析信息
 * 5. 内存高效的共享指针设计
 */
class DynamicInst {
public:
    // 指令状态枚举
    enum class Status {
        ALLOCATED,      // 已分配到ROB，等待发射
        ISSUED,         // 已发射到保留站，等待调度  
        EXECUTING,      // 正在执行单元中执行
        COMPLETED,      // 执行完成，等待写回
        RETIRED         // 已退休提交
    };

    // 执行相关的扩展信息（可选）
    struct ExecutionInfo {
        ExecutionUnitType required_unit_type;  // 需要的执行单元类型
        int execution_cycles;                  // 执行所需周期数
        int remaining_cycles;                  // 剩余执行周期
        bool has_memory_dependency;            // 是否有内存依赖
        
        ExecutionInfo() : required_unit_type(ExecutionUnitType::ALU), 
                         execution_cycles(1), remaining_cycles(0),
                         has_memory_dependency(false) {}
    };

    // 分支预测相关信息（可选）
    struct BranchInfo {
        bool is_branch;                        // 是否为分支指令
        bool predicted_taken;                  // 预测是否跳转
        uint64_t predicted_target;             // 预测跳转目标
        bool actual_taken;                     // 实际是否跳转
        uint64_t actual_target;                // 实际跳转目标
        bool prediction_correct;               // 预测是否正确
        
        BranchInfo() : is_branch(false), predicted_taken(false), 
                      predicted_target(0), actual_taken(false),
                      actual_target(0), prediction_correct(true) {}
    };

    // 内存访问相关信息（可选）
    struct MemoryInfo {
        bool is_memory_op;                     // 是否为内存操作
        bool is_load;                          // 是否为加载指令
        bool is_store;                         // 是否为存储指令
        uint64_t memory_address;               // 内存地址
        uint64_t memory_value;                 // 内存值
        uint8_t memory_size;                   // 访问大小（字节）
        bool address_ready;                    // 地址是否准备好
        bool store_forwarded;                  // 是否通过Store-to-Load转发
        
        MemoryInfo() : is_memory_op(false), is_load(false), is_store(false),
                      memory_address(0), memory_value(0), memory_size(0),
                      address_ready(false), store_forwarded(false) {}
    };

    struct FpExecuteInfo {
        uint64_t value = 0;
        bool write_int_reg = false;
        bool write_fp_reg = false;
        uint8_t fflags = 0;
    };

    struct AtomicExecuteInfo {
        bool acquire_reservation = false;
        bool release_reservation = false;
        bool do_store = false;
        uint64_t address = 0;
        uint64_t store_value = 0;
        Funct3 width = Funct3::LW;
    };

private:
    // ========== 核心指令信息 ==========
    DecodedInstruction decoded_info_;          // 解码后的指令信息（只存储一份）
    uint64_t instruction_id_;                  // 全局唯一指令序号
    uint64_t pc_;                             // 程序计数器
    Status status_;                           // 指令当前状态

    // ========== 寄存器重命名信息 ==========
    RegNum logical_dest_;                     // 逻辑目标寄存器
    PhysRegNum physical_dest_;                // 物理目标寄存器
    RegNum logical_src1_;                     // 逻辑源寄存器1
    RegNum logical_src2_;                     // 逻辑源寄存器2
    PhysRegNum physical_src1_;                // 物理源寄存器1
    PhysRegNum physical_src2_;                // 物理源寄存器2

    // ========== 操作数状态 ==========
    bool src1_ready_;                         // 源操作数1是否准备好
    bool src2_ready_;                         // 源操作数2是否准备好  
    uint64_t src1_value_;                     // 源操作数1的值
    uint64_t src2_value_;                     // 源操作数2的值

    // ========== 执行结果 ==========
    uint64_t result_;                         // 执行结果
    bool result_ready_;                       // 结果是否准备好
    bool has_exception_;                      // 是否有异常
    std::string exception_msg_;               // 异常信息
    bool has_trap_;                           // 是否触发可恢复陷入
    uint64_t trap_cause_;                     // 陷入原因（mcause）
    uint64_t trap_tval_;                      // 陷入附加值（mtval）
    bool has_fp_execute_info_;                // 是否携带浮点执行附加结果
    FpExecuteInfo fp_execute_info_;           // 浮点执行附加结果（fflags/目的寄存器类型）
    bool has_atomic_execute_info_;            // 是否携带原子执行附加结果
    AtomicExecuteInfo atomic_execute_info_;   // 原子执行附加结果（提交时更新内存/预留）

    // ========== ROB 关联信息 ==========
    ROBEntry rob_entry_;                      // 关联的ROB表项编号
    RSEntry rs_entry_;                        // 关联的保留站表项编号

    // ========== 跳转控制相关 ==========
    bool is_jump_;                            // 是否需要跳转
    uint64_t jump_target_;                    // 跳转目标地址

    // ========== 扩展信息（可选，支持未来功能） ==========
    std::optional<ExecutionInfo> exec_info_;  // 执行相关扩展信息
    std::optional<BranchInfo> branch_info_;   // 分支预测相关信息
    std::optional<MemoryInfo> memory_info_;   // 内存访问相关信息

    // ========== 调试和统计信息 ==========
    uint64_t fetch_cycle_;                    // 取指周期
    uint64_t decode_cycle_;                   // 译码周期
    uint64_t issue_cycle_;                    // 发射周期
    uint64_t execute_cycle_;                  // 执行开始周期
    uint64_t complete_cycle_;                 // 完成周期
    uint64_t retire_cycle_;                   // 退休周期

public:
    // ========== 构造函数和析构函数 ==========
    DynamicInst();
    explicit DynamicInst(const DecodedInstruction& decoded_info, uint64_t pc, uint64_t instruction_id);
    ~DynamicInst() = default;

    // 禁用拷贝构造和赋值（使用shared_ptr管理）
    DynamicInst(const DynamicInst&) = delete;
    DynamicInst& operator=(const DynamicInst&) = delete;

    // 允许移动构造和赋值
    DynamicInst(DynamicInst&&) = default;
    DynamicInst& operator=(DynamicInst&&) = default;

    // ========== 基础信息访问接口 ==========
    const DecodedInstruction& get_decoded_info() const { return decoded_info_; }
    uint64_t get_instruction_id() const { return instruction_id_; }
    uint64_t get_pc() const { return pc_; }
    Status get_status() const { return status_; }
    void set_status(Status status) { status_ = status; }

    // ========== 寄存器相关接口 ==========
    RegNum get_logical_dest() const { return logical_dest_; }
    PhysRegNum get_physical_dest() const { return physical_dest_; }
    void set_physical_dest(PhysRegNum reg) { physical_dest_ = reg; }
    
    RegNum get_logical_src1() const { return logical_src1_; }
    RegNum get_logical_src2() const { return logical_src2_; }
    PhysRegNum get_physical_src1() const { return physical_src1_; }
    PhysRegNum get_physical_src2() const { return physical_src2_; }
    void set_physical_src1(PhysRegNum reg) { physical_src1_ = reg; }
    void set_physical_src2(PhysRegNum reg) { physical_src2_ = reg; }

    // ========== 操作数状态接口 ==========
    bool is_src1_ready() const { return src1_ready_; }
    bool is_src2_ready() const { return src2_ready_; }
    bool is_ready_to_execute() const { return src1_ready_ && src2_ready_; }
    
    uint64_t get_src1_value() const { return src1_value_; }
    uint64_t get_src2_value() const { return src2_value_; }
    
    void set_src1_ready(bool ready, uint64_t value = 0) { 
        src1_ready_ = ready; 
        if (ready) src1_value_ = value; 
    }
    void set_src2_ready(bool ready, uint64_t value = 0) { 
        src2_ready_ = ready; 
        if (ready) src2_value_ = value; 
    }

    // ========== 执行结果接口 ==========
    uint64_t get_result() const { return result_; }
    bool is_result_ready() const { return result_ready_; }
    void set_result(uint64_t result) { 
        result_ = result; 
        result_ready_ = true; 
    }
    void set_fp_execute_info(const FpExecuteInfo& info) {
        has_fp_execute_info_ = true;
        fp_execute_info_ = info;
    }
    bool has_fp_execute_info() const { return has_fp_execute_info_; }
    const FpExecuteInfo& get_fp_execute_info() const { return fp_execute_info_; }
    void clear_fp_execute_info() {
        has_fp_execute_info_ = false;
        fp_execute_info_ = FpExecuteInfo{};
    }
    void set_atomic_execute_info(const AtomicExecuteInfo& info) {
        has_atomic_execute_info_ = true;
        atomic_execute_info_ = info;
    }
    bool has_atomic_execute_info() const { return has_atomic_execute_info_; }
    const AtomicExecuteInfo& get_atomic_execute_info() const { return atomic_execute_info_; }
    void clear_atomic_execute_info() {
        has_atomic_execute_info_ = false;
        atomic_execute_info_ = AtomicExecuteInfo{};
    }

    // ========== 异常处理接口 ==========
    bool has_exception() const { return has_exception_; }
    const std::string& get_exception_message() const { return exception_msg_; }
    void set_exception(const std::string& msg) { 
        has_exception_ = true; 
        exception_msg_ = msg;
        clear_trap();
    }
    void clear_exception() { 
        has_exception_ = false; 
        exception_msg_.clear(); 
    }

    bool has_trap() const { return has_trap_; }
    uint64_t get_trap_cause() const { return trap_cause_; }
    uint64_t get_trap_tval() const { return trap_tval_; }
    void set_trap(uint64_t cause, uint64_t tval) {
        clear_exception();
        has_trap_ = true;
        trap_cause_ = cause;
        trap_tval_ = tval;
    }
    void clear_trap() {
        has_trap_ = false;
        trap_cause_ = 0;
        trap_tval_ = 0;
    }

    // ========== ROB/RS 关联接口 ==========
    ROBEntry get_rob_entry() const { return rob_entry_; }
    void set_rob_entry(ROBEntry entry) { rob_entry_ = entry; }
    
    RSEntry get_rs_entry() const { return rs_entry_; }
    void set_rs_entry(RSEntry entry) { rs_entry_ = entry; }

    // ========== 跳转控制接口 ==========
    bool is_jump() const { return is_jump_; }
    uint64_t get_jump_target() const { return jump_target_; }
    void set_jump_info(bool is_jump, uint64_t target = 0);

    // ========== 扩展信息接口 ==========
    ExecutionInfo& get_execution_info() { 
        if (!exec_info_) exec_info_ = ExecutionInfo();
        return *exec_info_; 
    }
    const ExecutionInfo& get_execution_info() const { 
        static ExecutionInfo default_info;
        return exec_info_ ? *exec_info_ : default_info; 
    }

    BranchInfo& get_branch_info() { 
        if (!branch_info_) branch_info_ = BranchInfo();
        return *branch_info_; 
    }
    const BranchInfo& get_branch_info() const { 
        static BranchInfo default_info;
        return branch_info_ ? *branch_info_ : default_info; 
    }

    MemoryInfo& get_memory_info() { 
        if (!memory_info_) memory_info_ = MemoryInfo();
        return *memory_info_; 
    }
    const MemoryInfo& get_memory_info() const { 
        static MemoryInfo default_info;
        return memory_info_ ? *memory_info_ : default_info; 
    }

    // ========== 周期跟踪接口 ==========
    void set_fetch_cycle(uint64_t cycle) { fetch_cycle_ = cycle; }
    void set_decode_cycle(uint64_t cycle) { decode_cycle_ = cycle; }
    void set_issue_cycle(uint64_t cycle) { issue_cycle_ = cycle; }
    void set_execute_cycle(uint64_t cycle) { execute_cycle_ = cycle; }
    void set_complete_cycle(uint64_t cycle) { complete_cycle_ = cycle; }
    void set_retire_cycle(uint64_t cycle) { retire_cycle_ = cycle; }

    uint64_t get_fetch_cycle() const { return fetch_cycle_; }
    uint64_t get_decode_cycle() const { return decode_cycle_; }
    uint64_t get_issue_cycle() const { return issue_cycle_; }
    uint64_t get_execute_cycle() const { return execute_cycle_; }
    uint64_t get_complete_cycle() const { return complete_cycle_; }
    uint64_t get_retire_cycle() const { return retire_cycle_; }

    // ========== 状态查询接口 ==========
    bool is_allocated() const { return status_ == Status::ALLOCATED; }
    bool is_issued() const { return status_ == Status::ISSUED; }
    bool is_executing() const { return status_ == Status::EXECUTING; }
    bool is_completed() const { return status_ == Status::COMPLETED; }
    bool is_retired() const { return status_ == Status::RETIRED; }

    // ========== 工具函数 ==========
    // 检查指令是否为特定类型
    bool is_load_instruction() const;
    bool is_store_instruction() const;
    bool is_branch_instruction() const;
    bool is_jump_instruction() const;
    bool is_alu_instruction() const;

    // 获取指令需要的执行单元类型
    ExecutionUnitType get_required_execution_unit() const;

    // ========== 调试和序列化接口 ==========
    std::string to_string() const;
    void dump_state() const;
    
    // 状态转换辅助函数
    static const char* status_to_string(Status status);
    
    // 重置指令状态（用于流水线刷新）
    void reset_to_allocated();

private:
    // 初始化函数
    void initialize_from_decoded_instruction();
    void extract_register_info();
    void setup_execution_requirements();
};

// 类型别名
using DynamicInstPtr = std::shared_ptr<DynamicInst>;
using ConstDynamicInstPtr = std::shared_ptr<const DynamicInst>;

// 工厂函数
DynamicInstPtr create_dynamic_inst(const DecodedInstruction& decoded_info, 
                                  uint64_t pc, uint64_t instruction_id);

} // namespace riscv
