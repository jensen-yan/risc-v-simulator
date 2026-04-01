#pragma once

#include "cpu/ooo/dynamic_inst.h"
#include <string>
#include <vector>

namespace riscv {

/**
 * 流水线可视化追踪器
 *
 * 在 commit 阶段记录已提交指令的各阶段时间戳，
 * 模拟结束后生成 HTML 流水线时序图。
 */
class PipelineTracer {
public:
    struct FlushSummary {
        bool triggered;
        const char* reason;
        uint64_t flushed_rob_entries;
        uint64_t redirect_pc;
        bool has_redirect_pc;
        size_t fetch_buffer_dropped;

        FlushSummary()
            : triggered(false),
              reason(nullptr),
              flushed_rob_entries(0),
              redirect_pc(0),
              has_redirect_pc(false),
              fetch_buffer_dropped(0) {}
    };

    // 记录窗口配置
    struct Config {
        uint64_t start_cycle;
        uint64_t end_cycle;
        size_t max_instructions;

        Config() : start_cycle(0), end_cycle(UINT64_MAX), max_instructions(2000) {}
    };

    explicit PipelineTracer(const Config& config = Config());

    // 在 commit 阶段调用，记录已提交的指令
    void recordCommittedInstruction(const DynamicInstPtr& inst,
                                    const FlushSummary& flush_summary = FlushSummary{});

    // 生成 HTML 文件
    bool generateHTML(const std::string& output_path) const;

    // 生成文本格式（方便终端/日志分析）
    bool generateText(const std::string& output_path) const;

    size_t getRecordedCount() const { return records_.size(); }

private:
    // 每条指令的快照记录
    struct InstructionRecord {
        uint64_t instruction_id;
        uint64_t pc;
        std::string disassembly;  // 指令助记符

        uint64_t fetch_cycle;
        uint64_t decode_cycle;
        uint64_t issue_cycle;
        uint64_t execute_cycle;
        uint64_t complete_cycle;
        uint64_t retire_cycle;

        bool is_branch;
        bool branch_mispredicted;
        bool is_load;
        bool is_store;
        bool has_exception;
        bool caused_flush;
        std::string flush_reason;
        uint64_t flushed_rob_entries;
        uint64_t redirect_pc;
        bool has_redirect_pc;
        size_t fetch_buffer_dropped;
    };

    // 从 DecodedInstruction 生成助记符
    static std::string disassemble(const DecodedInstruction& decoded, uint64_t pc);

    // 生成寄存器名称
    static const char* regName(RegNum reg);

    Config config_;
    std::vector<InstructionRecord> records_;
};

} // namespace riscv
