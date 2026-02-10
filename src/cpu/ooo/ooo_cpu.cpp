#include "cpu/ooo/ooo_cpu.h"
#include "cpu/ooo/stages/fetch_stage.h"
#include "cpu/ooo/stages/decode_stage.h"
#include "cpu/ooo/stages/issue_stage.h"
#include "cpu/ooo/stages/execute_stage.h"
#include "cpu/ooo/stages/writeback_stage.h"
#include "cpu/ooo/stages/commit_stage.h"
#include "core/csr_utils.h"
#include "core/instruction_executor.h"
#include "common/debug_types.h"
#include "system/difftest.h"
#include "system/syscall_handler.h"
#include <cstdint>
#include <iostream>
#include <iomanip>

namespace riscv {

namespace {

template <typename T>
void clearQueue(std::queue<T>& q) {
    while (!q.empty()) {
        q.pop();
    }
}

void resetSingleExecutionUnit(ExecutionUnit& unit) {
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
}

void resetAllExecutionUnits(CPUState& state) {
    for (auto& unit : state.alu_units) resetSingleExecutionUnit(unit);
    for (auto& unit : state.branch_units) resetSingleExecutionUnit(unit);
    for (auto& unit : state.load_units) resetSingleExecutionUnit(unit);
    for (auto& unit : state.store_units) resetSingleExecutionUnit(unit);
}

void recreateRuntimeComponents(CPUState& state, const std::shared_ptr<Memory>& memory) {
    state.register_rename = std::make_unique<RegisterRenameUnit>();
    state.reservation_station = std::make_unique<ReservationStation>();
    state.reorder_buffer = std::make_unique<ReorderBuffer>();
    state.store_buffer = std::make_unique<StoreBuffer>();
    state.syscall_handler = std::make_unique<SyscallHandler>(memory);
}

void resetCpuStateForReuse(CPUState& state, const std::shared_ptr<Memory>& memory) {
    state.pc = 0;
    state.halted = false;
    state.instruction_count = 0;
    state.cycle_count = 0;
    state.branch_mispredicts = 0;
    state.pipeline_stalls = 0;
    state.reservation_valid = false;
    state.reservation_addr = 0;
    state.global_instruction_id = 0;

    state.arch_registers.fill(0);
    state.arch_fp_registers.fill(0);
    state.csr_registers.fill(0);
    state.physical_registers.fill(0);
    state.physical_fp_registers.fill(0);

    recreateRuntimeComponents(state, memory);
    resetAllExecutionUnits(state);
    clearQueue(state.fetch_buffer);
    clearQueue(state.cdb_queue);
}

} // namespace

OutOfOrderCPU::OutOfOrderCPU(std::shared_ptr<Memory> memory)
    : memory_(memory), difftest_(nullptr), difftest_synced_once_(false) {
    // 初始化CPUState
    cpu_state_.memory = memory_;
    cpu_state_.cpu_interface = this;  // 设置CPU接口引用，让Stage可以调用CPU方法
    cpu_state_.enabled_extensions = static_cast<uint32_t>(Extension::I) | 
                                   static_cast<uint32_t>(Extension::M) | 
                                   static_cast<uint32_t>(Extension::A) |
                                   static_cast<uint32_t>(Extension::F) |
                                   static_cast<uint32_t>(Extension::D) |
                                   static_cast<uint32_t>(Extension::C);
    
    // 初始化可变运行态（寄存器、队列、ooo组件等）
    resetCpuStateForReuse(cpu_state_, memory_);
    syscall_handler_ = std::make_unique<SyscallHandler>(memory_);
    
    // DiffTest将由Simulator通过setDiffTest()方法设置
    
    // 创建流水线阶段
    fetch_stage_ = std::make_unique<FetchStage>();
    decode_stage_ = std::make_unique<DecodeStage>();
    issue_stage_ = std::make_unique<IssueStage>();
    execute_stage_ = std::make_unique<ExecuteStage>();
    writeback_stage_ = std::make_unique<WritebackStage>();
    commit_stage_ = std::make_unique<CommitStage>();
    
    LOGI(SYSTEM, "ooo cpu initialized (new pipeline), difftest will be configured by simulator");
}

OutOfOrderCPU::~OutOfOrderCPU() = default;



void OutOfOrderCPU::step() {
    if (cpu_state_.halted) {
        return;
    }
    
    try {
        // 流水线阶段执行（反向顺序以维护依赖关系）
        commit_stage_->execute(cpu_state_);    // 提交阶段
        writeback_stage_->execute(cpu_state_); // 写回阶段
        execute_stage_->execute(cpu_state_);   // 执行阶段
        issue_stage_->execute(cpu_state_);     // 发射阶段
        decode_stage_->execute(cpu_state_);    // 译码阶段
        fetch_stage_->execute(cpu_state_);     // 取指阶段
        
        // 增加周期计数
        cpu_state_.cycle_count++;
    } catch (const MemoryException& e) {
        handle_exception(e.what(), cpu_state_.pc);
    } catch (const SimulatorException& e) {
        handle_exception(e.what(), cpu_state_.pc);
    }
}

void OutOfOrderCPU::run() {
    while (!cpu_state_.halted && !memory_->shouldExit()) {
        step();
    }
}

void OutOfOrderCPU::reset() {
    resetCpuStateForReuse(cpu_state_, memory_);
    syscall_handler_ = std::make_unique<SyscallHandler>(memory_);
    
    // 重置DiffTest组件
    difftest_synced_once_ = false;
    if (difftest_) {
        difftest_->reset();
    }
    
    LOGI(SYSTEM, "ooo cpu reset completed");
}

uint64_t OutOfOrderCPU::get_physical_register_value(PhysRegNum reg) const {
    if (reg < RegisterRenameUnit::NUM_PHYSICAL_REGS) {
        return cpu_state_.physical_registers[reg];
    }
    return 0;
}

void OutOfOrderCPU::set_physical_register_value(PhysRegNum reg, uint64_t value) {
    if (reg < RegisterRenameUnit::NUM_PHYSICAL_REGS) {
        cpu_state_.physical_registers[reg] = value;
    }
}

// 接口兼容性方法
uint64_t OutOfOrderCPU::getRegister(RegNum reg) const {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    return cpu_state_.arch_registers[reg];
}

void OutOfOrderCPU::setRegister(RegNum reg, uint64_t value) {
    if (reg >= NUM_REGISTERS) {
        throw SimulatorException("无效的寄存器编号: " + std::to_string(reg));
    }
    
    // x0寄存器始终为0
    if (reg != 0) {
        cpu_state_.arch_registers[reg] = value;
    }
}

uint64_t OutOfOrderCPU::getFPRegister(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    return cpu_state_.arch_fp_registers[reg];
}

void OutOfOrderCPU::setFPRegister(RegNum reg, uint64_t value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    cpu_state_.arch_fp_registers[reg] = value;
}

float OutOfOrderCPU::getFPRegisterFloat(RegNum reg) const {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    union {
        uint32_t u;
        float f;
    } conv{static_cast<uint32_t>(cpu_state_.arch_fp_registers[reg])};
    return conv.f;
}

void OutOfOrderCPU::setFPRegisterFloat(RegNum reg, float value) {
    if (reg >= NUM_FP_REGISTERS) {
        throw SimulatorException("无效的浮点寄存器编号: " + std::to_string(reg));
    }
    union {
        uint32_t u;
        float f;
    } conv{};
    conv.f = value;
    cpu_state_.arch_fp_registers[reg] = 0xFFFFFFFF00000000ULL | static_cast<uint64_t>(conv.u);
}

uint64_t OutOfOrderCPU::getCSR(uint32_t addr) const {
    if (addr >= cpu_state_.csr_registers.size()) {
        throw SimulatorException("无效的CSR地址: " + std::to_string(addr));
    }
    return csr::read(cpu_state_.csr_registers, addr);
}

void OutOfOrderCPU::setCSR(uint32_t addr, uint64_t value) {
    if (addr >= cpu_state_.csr_registers.size()) {
        throw SimulatorException("无效的CSR地址: " + std::to_string(addr));
    }
    csr::write(cpu_state_.csr_registers, addr, value);
}

void OutOfOrderCPU::handle_exception(const std::string& exception_msg, uint64_t pc) {
    LOGE(SYSTEM, "exception: %s, pc=0x%" PRIx64, exception_msg.c_str(), pc);
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

bool OutOfOrderCPU::predict_branch(uint64_t pc) {
    // 简化的分支预测：总是预测不跳转
    return false;
}

void OutOfOrderCPU::update_branch_predictor(uint64_t pc, bool taken) {
    // 简化实现：不更新预测器
}

uint64_t OutOfOrderCPU::loadFromMemory(Address addr, Funct3 funct3) {
    return InstructionExecutor::loadFromMemory(memory_, addr, funct3);
}

void OutOfOrderCPU::storeToMemory(Address addr, uint64_t value, Funct3 funct3) {
    InstructionExecutor::storeToMemory(memory_, addr, value, funct3);
}

int32_t OutOfOrderCPU::signExtend(uint32_t value, int bits) const {
    return InstructionExecutor::signExtend(value, bits);
}

ICpuInterface::StatsList OutOfOrderCPU::getStats() const {
    return {
        {"instructions", cpu_state_.instruction_count, "Retired instruction count"},
        {"cycles", cpu_state_.cycle_count, "Elapsed cycles"},
        {"branch_mispredicts", cpu_state_.branch_mispredicts, "Branch mispredict count"},
        {"pipeline_stalls", cpu_state_.pipeline_stalls, "Pipeline stall count"},
    };
}

void OutOfOrderCPU::dumpRegisters() const {
    std::cout << "Architectural Registers:" << std::endl;
    for (int i = 0; i < NUM_REGISTERS; i += 4) {
        for (int j = 0; j < 4 && i + j < NUM_REGISTERS; ++j) {
            std::cout << "x" << std::setw(2) << (i + j) << ": 0x" 
                      << std::hex << std::setfill('0') << std::setw(16) 
                      << cpu_state_.arch_registers[i + j] << "  ";
        }
        std::cout << std::endl;
    }
    std::cout << std::dec;
}

void OutOfOrderCPU::dumpState() const {
    std::cout << "Out-of-Order CPU State:" << std::endl;
    std::cout << "PC: 0x" << std::hex << cpu_state_.pc << std::dec << std::endl;
    std::cout << "Instructions: " << cpu_state_.instruction_count << std::endl;
    std::cout << "Cycles: " << cpu_state_.cycle_count << std::endl;
    std::cout << "Halted: " << (cpu_state_.halted ? "yes" : "no") << std::endl;
    std::cout << "Branch Mispredicts: " << cpu_state_.branch_mispredicts << std::endl;
    std::cout << "Pipeline Stalls: " << cpu_state_.pipeline_stalls << std::endl;
    
    if (cpu_state_.cycle_count > 0) {
        double ipc = static_cast<double>(cpu_state_.instruction_count) / cpu_state_.cycle_count;
        std::cout << "IPC: " << std::fixed << std::setprecision(2) << ipc << std::endl;
    }
    
    dumpRegisters();
}

void OutOfOrderCPU::dumpPipelineState() const {
    std::cout << "\\n=== Out-of-Order Pipeline State ===" << std::endl;
    
    // 显示ROB状态
    cpu_state_.reorder_buffer->dump_reorder_buffer();
    
    // 显示保留站状态
    cpu_state_.reservation_station->dump_reservation_station();
    
    // 显示寄存器重命名状态
    cpu_state_.register_rename->dump_rename_table();
    
    // 显示执行单元状态
    cpu_state_.reservation_station->dump_execution_units();
    
    std::cout << "Fetch Buffer Size: " << cpu_state_.fetch_buffer.size() << std::endl;
    std::cout << "CDB Queue Size: " << cpu_state_.cdb_queue.size() << std::endl;
}



void OutOfOrderCPU::setDiffTest(DiffTest* difftest) {
    difftest_ = difftest;
    difftest_synced_once_ = false;
    LOGI(DIFFTEST, "difftest attached to ooo cpu");
}

void OutOfOrderCPU::enableDiffTest(bool enable) {
    if (difftest_) {
        difftest_->setEnabled(enable);
        if (enable) {
            difftest_synced_once_ = false;
        }
        LOGI(DIFFTEST, "difftest %s", enable ? "enabled" : "disabled");
    }
}

bool OutOfOrderCPU::isDiffTestEnabled() const {
    return difftest_ && difftest_->isEnabled();
}

void OutOfOrderCPU::performDiffTestWithCommittedPC(uint64_t committed_pc) {
    if (difftest_ && difftest_->isEnabled()) {
        // 第一次调用时，同步参考CPU的完整状态
        if (!difftest_synced_once_) {
            // 对于第一次同步，使用提交的PC来同步参考CPU
            difftest_->setReferencePC(committed_pc);
            difftest_->syncReferenceState(this);
            difftest_synced_once_ = true;
        }
        
        // 执行参考CPU一步并比较状态，使用提交指令的PC
        bool match = difftest_->stepAndCompareWithCommittedPC(this, committed_pc);
        if (!match) {
            LOGE(DIFFTEST, "difftest detected mismatch");
            exit(1);
        }
    }
}

void OutOfOrderCPU::getDiffTestStats(uint64_t& comparisons, uint64_t& mismatches) const {
    if (difftest_) {
        auto stats = difftest_->getStatistics();
        comparisons = stats.comparison_count;
        mismatches = stats.mismatch_count;
    } else {
        comparisons = 0;
        mismatches = 0;
    }
}

} // namespace riscv
