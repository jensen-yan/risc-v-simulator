#include "common/debug_types.h"

namespace riscv {

// 定义调试预设配置
const std::unordered_map<std::string, std::vector<std::string>> LogPresets::presets = {
    // 基础预设：适合初学者和基本调试
    {"basic", {
        "FETCH", "DECODE", "COMMIT"
    }},
    
    // 乱序执行预设：专门用于乱序CPU调试
    {"ooo", {
        "FETCH", "DECODE", "ISSUE", "EXECUTE", "WRITEBACK", "COMMIT", 
        "ROB", "RENAME", "RS"
    }},
    
    // 流水线预设：完整的流水线阶段
    {"pipeline", {
        "FETCH", "DECODE", "ISSUE", "EXECUTE", "WRITEBACK", "COMMIT"
    }},
    
    // 性能预设：性能分析相关
    {"performance", {
        "EXECUTE", "COMMIT", "ROB", "RS", "BRANCH", "STALL"
    }},
    
    // 详细预设：所有调试信息
    {"detailed", {
        "FETCH", "DECODE", "ISSUE", "EXECUTE", "WRITEBACK", "COMMIT",
        "ROB", "RENAME", "RS", "BRANCH", "STALL", "MEMORY", "SYSCALL"
    }},
    
    // 内存预设：内存访问相关
    {"memory", {
        "FETCH", "MEMORY", "EXECUTE", "COMMIT"
    }},
    
    // 分支预设：分支预测相关
    {"branch", {
        "FETCH", "DECODE", "EXECUTE", "COMMIT", "BRANCH"
    }},
    
    // 最小预设：最基本的调试信息
    {"minimal", {
        "FETCH", "COMMIT"
    }}
};

} // namespace riscv