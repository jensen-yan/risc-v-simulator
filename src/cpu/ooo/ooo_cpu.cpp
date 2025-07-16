#include "cpu/ooo/ooo_cpu.h"
#include "cpu/ooo/stages/fetch_stage.h"
#include "cpu/ooo/stages/decode_stage.h"
#include "cpu/ooo/stages/issue_stage.h"
#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/stages/writeback_stage.h"
#include "cpu/ooo/stages/commit_stage.h"
#include "system/syscall_handler.h"
#include "core/instruction_executor.h"
#include "common/debug_types.h"
#include <cstdint>
#include <iostream>
#include <iomanip>

namespace riscv {

OutOfOrderCPU::OutOfOrderCPU(std::shared_ptr<Memory> memory) : memory_(memory) {
    // 初始化CPUState
    cpu_state_.memory = memory_;
    cpu_state_.cpu_interface = this;  // 设置CPU接口引用，让Stage可以调用CPU方法
    cpu_state_.enabled_extensions = static_cast<uint32_t>(Extension::I) | 
                                   static_cast<uint32_t>(Extension::M) | 
                                   static_cast<uint32_t>(Extension::F) | 
                                   static_cast<uint32_t>(Extension::C);
    
    // 初始化组件
    cpu_state_.register_rename = std::make_unique<RegisterRenameUnit>();
    cpu_state_.reservation_station = std::make_unique<ReservationStation>();
    cpu_state_.reorder_buffer = std::make_unique<ReorderBuffer>();
    cpu_state_.syscall_handler = std::make_unique<SyscallHandler>(memory_);
    syscall_handler_ = std::make_unique<SyscallHandler>(memory_);
    
    // 创建流水线阶段
    fetch_stage_ = std::make_unique<FetchStage>();
    decode_stage_ = std::make_unique<DecodeStage>();
    issue_stage_ = std::make_unique<IssueStage>();
    execute_stage_ = std::make_unique<ExecuteStage>();
    writeback_stage_ = std::make_unique<WritebackStage>();
    commit_stage_ = std::make_unique<CommitStage>();
    
    std::cout << "乱序执行CPU初始化完成（新流水线设计）" << std::endl;
}

OutOfOrderCPU::~OutOfOrderCPU() = default;



void OutOfOrderCPU::step() {
    if (cpu_state_.halted) {
        return;
    }
    
    try {
        // 更新全局调试上下文
        DebugContext::getInstance().setCycle(cpu_state_.cycle_count);
        
        // 流水线阶段执行（反向顺序以维护依赖关系）
        commit_stage_->execute(cpu_state_);    // 提交阶段
        writeback_stage_->execute(cpu_state_); // 写回阶段
        execute_stage_->execute(cpu_state_);   // 执行阶段
        issue_stage_->execute(cpu_state_);     // 发射阶段
        decode_stage_->execute(cpu_state_);    // 译码阶段
        fetch_stage_->execute(cpu_state_);     // 取指阶段
        
        // 增加周期计数
        cpu_state_.cycle_count++;
        
        // 简单的停机条件检查
        if (cpu_state_.cycle_count > 10000) {
            std::cout << "警告: 执行周期数超过10000，自动停止" << std::endl;
            cpu_state_.halted = true;
        }
        
    } catch (const MemoryException& e) {
        handle_exception(e.what(), cpu_state_.pc);
    } catch (const SimulatorException& e) {
        handle_exception(e.what(), cpu_state_.pc);
    }
}

void OutOfOrderCPU::run() {
    while (!cpu_state_.halted) {
        step();
    }
}

void OutOfOrderCPU::reset() {
    // 重置CPU状态
    cpu_state_.pc = 0;
    cpu_state_.halted = false;
    cpu_state_.instruction_count = 0;
    cpu_state_.cycle_count = 0;
    cpu_state_.branch_mispredicts = 0;
    cpu_state_.pipeline_stalls = 0;
    
    // 重置寄存器
    cpu_state_.arch_registers.fill(0);
    cpu_state_.arch_fp_registers.fill(0);
    cpu_state_.physical_registers.fill(0);
    cpu_state_.physical_fp_registers.fill(0);
    
    // 重置乱序执行组件
    cpu_state_.register_rename = std::make_unique<RegisterRenameUnit>();
    cpu_state_.reservation_station = std::make_unique<ReservationStation>();
    cpu_state_.reorder_buffer = std::make_unique<ReorderBuffer>();
    
    // 重置CPUState中的组件
    cpu_state_.register_rename = std::make_unique<RegisterRenameUnit>();
    cpu_state_.reservation_station = std::make_unique<ReservationStation>();
    cpu_state_.reorder_buffer = std::make_unique<ReorderBuffer>();
    
    // 清空缓冲区
    while (!cpu_state_.fetch_buffer.empty()) {
        cpu_state_.fetch_buffer.pop();
    }
    while (!cpu_state_.cdb_queue.empty()) {
        cpu_state_.cdb_queue.pop();
    }
    
    // 清空CPUState中的缓冲区
    while (!cpu_state_.fetch_buffer.empty()) {
        cpu_state_.fetch_buffer.pop();
    }
    while (!cpu_state_.cdb_queue.empty()) {
        cpu_state_.cdb_queue.pop();
    }
    
    std::cout << "乱序执行CPU重置完成" << std::endl;
}

uint32_t OutOfOrderCPU::get_physical_register_value(PhysRegNum reg) const {
    if (reg < RegisterRenameUnit::NUM_PHYSICAL_REGS) {
        return cpu_state_.physical_registers[reg];
    }
    return 0;
}

void OutOfOrderCPU::set_physical_register_value(PhysRegNum reg, uint32_t value) {
    if (reg < RegisterRenameUnit::NUM_PHYSICAL_REGS) {
        cpu_state_.physical_registers[reg] = value;
    }
}

// 接口兼容性方法
uint32_t OutOfOrderCPU::getRegister(RegNum reg) const {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    return cpu_state_.arch_registers[reg];
}

void OutOfOrderCPU::setRegister(RegNum reg, uint32_t value) {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    
    // x0寄存器始终为0
    if (reg != 0) {
        cpu_state_.arch_registers[reg] = value;
    }
}

uint32_t OutOfOrderCPU::getFPRegister(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    return cpu_state_.arch_fp_registers[reg];
}

void OutOfOrderCPU::setFPRegister(RegNum reg, uint32_t value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    cpu_state_.arch_fp_registers[reg] = value;
}

float OutOfOrderCPU::getFPRegisterFloat(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    return *reinterpret_cast<const float*>(&cpu_state_.arch_fp_registers[reg]);
}

void OutOfOrderCPU::setFPRegisterFloat(RegNum reg, float value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    cpu_state_.arch_fp_registers[reg] = *reinterpret_cast<const uint32_t*>(&value);
}

void OutOfOrderCPU::handle_exception(const std::string& exception_msg, uint32_t pc) {
    std::cerr << "异常: " << exception_msg << ", PC=0x" << std::hex << pc << std::dec << std::endl;
    flush_pipeline();
    cpu_state_.halted = true;
}

void OutOfOrderCPU::flush_pipeline() {
    // 清空取指缓冲区
    while (!cpu_state_.fetch_buffer.empty()) {
        cpu_state_.fetch_buffer.pop();
    }
    
    // 刷新保留站
    cpu_state_.reservation_station->flush_pipeline();
    
    // 刷新ROB
    cpu_state_.reorder_buffer->flush_pipeline();
    
    // 重新初始化寄存器重命名
    cpu_state_.register_rename = std::make_unique<RegisterRenameUnit>();
    
    // 清空CDB队列
    while (!cpu_state_.cdb_queue.empty()) {
        cpu_state_.cdb_queue.pop();
    }
}

bool OutOfOrderCPU::predict_branch(uint32_t pc) {
    // 简化的分支预测：总是预测不跳转
    return false;
}

void OutOfOrderCPU::update_branch_predictor(uint32_t pc, bool taken) {
    // 简化实现：不更新预测器
}

void OutOfOrderCPU::handleEcall() {
    // 处理系统调用
    bool shouldHalt = syscall_handler_->handleSyscall(this);
    if (shouldHalt) {
        cpu_state_.halted = true;
    }
}

void OutOfOrderCPU::handleEbreak() {
    std::cout << "遇到断点指令，停止执行" << std::endl;
    cpu_state_.halted = true;
}

uint32_t OutOfOrderCPU::loadFromMemory(Address addr, Funct3 funct3) {
    return InstructionExecutor::loadFromMemory(memory_, addr, funct3);
}

void OutOfOrderCPU::storeToMemory(Address addr, uint32_t value, Funct3 funct3) {
    InstructionExecutor::storeToMemory(memory_, addr, value, funct3);
}

int32_t OutOfOrderCPU::signExtend(uint32_t value, int bits) const {
    return InstructionExecutor::signExtend(value, bits);
}

void OutOfOrderCPU::getPerformanceStats(uint64_t& instructions, uint64_t& cycles, 
                                       uint64_t& branch_mispredicts, uint64_t& stalls) const {
    instructions = cpu_state_.instruction_count;
    cycles = cpu_state_.cycle_count;
    branch_mispredicts = cpu_state_.branch_mispredicts;
    stalls = cpu_state_.pipeline_stalls;
}

void OutOfOrderCPU::dumpRegisters() const {
    std::cout << "架构寄存器状态:" << std::endl;
    for (int i = 0; i < NUM_REGISTERS; i += 4) {
        for (int j = 0; j < 4 && i + j < NUM_REGISTERS; ++j) {
            std::cout << "x" << std::setw(2) << (i + j) << ": 0x" 
                      << std::hex << std::setfill('0') << std::setw(8) 
                      << cpu_state_.arch_registers[i + j] << "  ";
        }
        std::cout << std::endl;
    }
    std::cout << std::dec;
}

void OutOfOrderCPU::dumpState() const {
    std::cout << "乱序执行CPU状态:" << std::endl;
    std::cout << "PC: 0x" << std::hex << cpu_state_.pc << std::dec << std::endl;
    std::cout << "指令计数: " << cpu_state_.instruction_count << std::endl;
    std::cout << "周期计数: " << cpu_state_.cycle_count << std::endl;
    std::cout << "停机状态: " << (cpu_state_.halted ? "是" : "否") << std::endl;
    std::cout << "分支预测错误: " << cpu_state_.branch_mispredicts << std::endl;
    std::cout << "流水线停顿: " << cpu_state_.pipeline_stalls << std::endl;
    
    if (cpu_state_.cycle_count > 0) {
        double ipc = static_cast<double>(cpu_state_.instruction_count) / cpu_state_.cycle_count;
        std::cout << "IPC: " << std::fixed << std::setprecision(2) << ipc << std::endl;
    }
    
    dumpRegisters();
}

void OutOfOrderCPU::dumpPipelineState() const {
    std::cout << "\\n=== 乱序执行流水线状态 ===" << std::endl;
    
    // 显示ROB状态
    cpu_state_.reorder_buffer->dump_reorder_buffer();
    
    // 显示保留站状态
    cpu_state_.reservation_station->dump_reservation_station();
    
    // 显示寄存器重命名状态
    cpu_state_.register_rename->dump_rename_table();
    
    // 显示执行单元状态
    cpu_state_.reservation_station->dump_execution_units();
    
    std::cout << "取指缓冲区大小: " << cpu_state_.fetch_buffer.size() << std::endl;
    std::cout << "CDB队列大小: " << cpu_state_.cdb_queue.size() << std::endl;
}

void OutOfOrderCPU::print_stage_activity(const std::string& stage, const std::string& activity) {
    auto& debugManager = DebugManager::getInstance();
    debugManager.printf(stage, activity, cpu_state_.cycle_count, cpu_state_.pc);
}

} // namespace riscv