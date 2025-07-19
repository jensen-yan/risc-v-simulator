#pragma once

#include "common/cpu_interface.h"
#include "core/memory.h"
#include <memory>
#include <string>

namespace riscv {

/**
 * DiffTest机制实现
 * 
 * 用于验证乱序CPU的正确性：
 * 1. 维护一个参考的顺序CPU
 * 2. 当乱序CPU提交指令时，参考CPU也执行一条指令并比较状态
 * 3. 发现不一致时立即报告错误
 */
class DiffTest {
public:
    /**
     * 构造函数
     * @param memory 共享内存实例
     */
    explicit DiffTest(std::shared_ptr<Memory> memory);
    ~DiffTest();
    
    // 禁用拷贝构造和赋值
    DiffTest(const DiffTest&) = delete;
    DiffTest& operator=(const DiffTest&) = delete;
    
    /**
     * 启用/禁用difftest
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    /**
     * 设置参考CPU的PC
     * @param pc 程序计数器值
     */
    void setReferencePC(uint64_t pc);
    
    /**
     * 同步参考CPU的状态到与乱序CPU一致
     * @param ooo_cpu 乱序CPU接口
     */
    void syncReferenceState(ICpuInterface* ooo_cpu);
    
    /**
     * 执行参考CPU一条指令并比较状态，使用指定的提交PC
     * @param ooo_cpu 乱序CPU接口
     * @param committed_pc 提交指令的PC
     * @return true如果状态一致，false如果发现不一致
     */
    bool stepAndCompareWithCommittedPC(ICpuInterface* ooo_cpu, uint32_t committed_pc);
    
    /**
     * 重置difftest状态
     */
    void reset();
    
    /**
     * 获取统计信息
     */
    struct Statistics {
        uint64_t comparison_count;
        uint64_t mismatch_count;
    };
    Statistics getStatistics() const;
    
    /**
     * 配置选项
     */
    void setStopOnMismatch(bool stop) { stop_on_mismatch_ = stop; }
    bool getStopOnMismatch() const { return stop_on_mismatch_; }

private:
    /**
     * 比较寄存器状态
     */
    bool compareRegisters(ICpuInterface* ooo_cpu);
    bool comparePC(ICpuInterface* ooo_cpu);
    bool compareFPRegisters(ICpuInterface* ooo_cpu);
    
    /**
     * 转储状态信息用于调试
     */
    void dumpState(ICpuInterface* ref_cpu, ICpuInterface* ooo_cpu);
    
    // 成员变量
    std::unique_ptr<ICpuInterface> reference_cpu_;  // 参考CPU
    bool enabled_;                                  // 是否启用
    bool stop_on_mismatch_;                        // 发现不一致时是否停止
    uint64_t comparison_count_;                    // 比较次数
    uint64_t mismatch_count_;                      // 不一致次数
};

} // namespace riscv 