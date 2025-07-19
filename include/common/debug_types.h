#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <fstream>
#include <fmt/format.h>

namespace riscv {

/**
 * 全局调试上下文管理器
 * 用于存储当前的周期数等上下文信息
 */
class DebugContext {
public:
    static DebugContext& getInstance() {
        static DebugContext instance;
        return instance;
    }
    
    // 设置当前周期数（由 ooo_cpu 调用）
    void setCycle(uint64_t cycle) {
        current_cycle_ = cycle;
    }
    
    // 获取当前周期数
    uint64_t getCurrentCycle() const {
        return current_cycle_;
    }
    
private:
    uint64_t current_cycle_ = 0;
    
    DebugContext() = default;
    ~DebugContext() = default;
    DebugContext(const DebugContext&) = delete;
    DebugContext& operator=(const DebugContext&) = delete;
};

/**
 * 调试信息结构体
 * 包含完整的调试信息，支持多种输出格式
 */
struct DebugInfo {
    std::string stage;        // 阶段名称 (FETCH, DECODE, ISSUE, EXECUTE, WRITEBACK, COMMIT, ROB, RS, RENAME)
    std::string message;      // 调试消息
    uint64_t cycle = 0;       // 当前周期
    uint32_t pc = 0;          // 程序计数器（可选）
    
    // 简单构造函数
    DebugInfo() = default;
    
    DebugInfo(const std::string& stage, const std::string& message)
        : stage(stage), message(message) {}
    
    DebugInfo(const std::string& stage, const std::string& message, 
              uint64_t cycle, uint32_t pc = 0)
        : stage(stage), message(message), cycle(cycle), pc(pc) {}
};

/**
 * 调试回调函数类型， 定义函数别名，返回void，参数为DebugInfo
 */
using DebugCallback = std::function<void(const DebugInfo&)>;

/**
 * 调试输出格式化器
 * 支持多种输出格式和模式
 */
class DebugFormatter {
public:
    enum class Mode {
        SIMPLE,     // 简洁模式: [STAGE] message
        VERBOSE,    // 详细模式: [STAGE] Cycle X: message
        WITH_PC     // 带PC模式: [STAGE] Cycle X (PC=0xYYYY): message
    };
    
    // 根据模式格式化输出
    static std::string format(const DebugInfo& info, Mode mode = Mode::VERBOSE) {
        switch (mode) {
            case Mode::SIMPLE:
                return "[" + info.stage + "] " + info.message;
            case Mode::VERBOSE:
                return "[" + info.stage + "] Cycle " + std::to_string(info.cycle) + ": " + info.message;
            case Mode::WITH_PC:
                if (info.pc != 0) {
                    return "[" + info.stage + "] Cycle " + std::to_string(info.cycle) + 
                           " (PC=0x" + std::to_string(info.pc) + "): " + info.message;
                }
                return format(info, Mode::VERBOSE);
        }
        return "";
    }
    
    // 兼容性函数
    static std::string formatSimple(const DebugInfo& info) {
        return format(info, Mode::SIMPLE);
    }
    
    static std::string formatWithPC(const DebugInfo& info) {
        return format(info, Mode::WITH_PC);
    }
};

/**
 * 调试预设配置类
 * 提供常用的调试分类组合
 */
class LogPresets {
public:
    // 预设配置映射
    static const std::unordered_map<std::string, std::vector<std::string>> presets;
    
    // 获取预设的调试分类
    static std::vector<std::string> getCategories(const std::string& preset) {
        auto it = presets.find(preset);
        if (it != presets.end()) {
            return it->second;
        }
        return {};
    }
    
    // 获取所有可用的预设名称
    static std::vector<std::string> getAvailablePresets() {
        std::vector<std::string> result;
        for (const auto& pair : presets) {
            result.push_back(pair.first);
        }
        return result;
    }
};

/**
 * 增强的调试管理器
 * 提供类似GEM5的dprintf功能，支持分类控制和周期范围
 */
class DebugManager {
public:
    static DebugManager& getInstance() {
        static DebugManager instance;
        return instance;
    }
    
    // 设置调试输出回调
    void setCallback(DebugCallback callback) {
        debug_callback_ = callback;
    }
    
    // 文件输出功能
    void setLogFile(const std::string& filename) {
        if (log_file_.is_open()) {
            log_file_.close();
        }
        // 强制使用文本模式，设置UTF-8编码
        log_file_.open(filename, std::ios::out | std::ios::trunc);
        if (log_file_.is_open()) {
            // 确保使用UTF-8编码
            log_file_.imbue(std::locale(""));
        } else {
            std::cerr << "Warning: Cannot open log file: " << filename << std::endl;
        }
    }
    
    void setOutputToFile(bool enable) {
        output_to_file_ = enable;
    }
    
    void setOutputToConsole(bool enable) {
        output_to_console_ = enable;
    }
    
    void closeLogFile() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
    // 启用/禁用调试分类
    void enableCategory(const std::string& category) {
        enabled_categories_.insert(category);
    }
    
    void disableCategory(const std::string& category) {
        enabled_categories_.erase(category);
    }
    
    void clearCategories() {
        enabled_categories_.clear();
    }
    
    // 批量设置调试分类
    void setCategories(const std::vector<std::string>& categories) {
        enabled_categories_.clear();
        for (const auto& category : categories) {
            enabled_categories_.insert(category);
        }
    }
    
    // 设置调试分类（字符串格式，用逗号分隔）
    void setCategories(const std::string& categories) {
        enabled_categories_.clear();
        if (categories.empty()) {
            return;
        }
        
        std::string current = categories;
        size_t pos = 0;
        while ((pos = current.find(',')) != std::string::npos) {
            std::string category = current.substr(0, pos);
            // 去除空格
            category.erase(0, category.find_first_not_of(" \t"));
            category.erase(category.find_last_not_of(" \t") + 1);
            if (!category.empty()) {
                enabled_categories_.insert(category);
            }
            current = current.substr(pos + 1);
        }
        // 处理最后一个分类
        current.erase(0, current.find_first_not_of(" \t"));
        current.erase(current.find_last_not_of(" \t") + 1);
        if (!current.empty()) {
            enabled_categories_.insert(current);
        }
    }
    
    // 使用预设配置
    void setPreset(const std::string& preset) {
        auto categories = LogPresets::getCategories(preset);
        setCategories(categories);
    }
    
    // 设置周期范围
    void setCycleRange(uint64_t start, uint64_t end = UINT64_MAX) {
        debug_start_cycle_ = start;
        debug_end_cycle_ = end;
    }
    
    // 设置输出格式
    void setOutputMode(DebugFormatter::Mode mode) {
        output_mode_ = mode;
    }
    
    // 获取当前输出格式
    DebugFormatter::Mode getOutputMode() const {
        return output_mode_;
    }
    
    // 检查是否应该输出调试信息
    bool shouldOutput(const std::string& stage, uint64_t cycle) const {
        // 检查分类过滤
        if (!enabled_categories_.empty() && 
            enabled_categories_.find(stage) == enabled_categories_.end()) {
            return false;
        }
        
        // 检查周期范围
        if (cycle < debug_start_cycle_ || cycle > debug_end_cycle_) {
            return false;
        }
        
        return debug_callback_ != nullptr;
    }
    
    // 输出调试信息
    void printf(const std::string& stage, const std::string& message, 
                uint64_t cycle = 0, uint32_t pc = 0) {
        if (shouldOutput(stage, cycle)) {
            DebugInfo info(stage, message, cycle, pc);
            std::string formatted = DebugFormatter::format(info, output_mode_);
            
            // 输出到控制台
            if (output_to_console_ && debug_callback_) {
                debug_callback_(info);
            }
            
            // 输出到文件
            if (output_to_file_ && log_file_.is_open()) {
                log_file_ << formatted << std::endl;
                log_file_.flush();
            }
        }
    }
    
    // 获取当前配置信息
    std::string getConfigInfo() const {
        std::string info = "Debug Configuration:\n";
        info += "  Categories: ";
        if (enabled_categories_.empty()) {
            info += "ALL";
        } else {
            bool first = true;
            for (const auto& category : enabled_categories_) {
                if (!first) info += ", ";
                info += category;
                first = false;
            }
        }
        info += "\n  Cycle Range: " + std::to_string(debug_start_cycle_) + "-";
        if (debug_end_cycle_ == UINT64_MAX) {
            info += "END";
        } else {
            info += std::to_string(debug_end_cycle_);
        }
        info += "\n  Output Mode: ";
        switch (output_mode_) {
            case DebugFormatter::Mode::SIMPLE:
                info += "SIMPLE";
                break;
            case DebugFormatter::Mode::VERBOSE:
                info += "VERBOSE";
                break;
            case DebugFormatter::Mode::WITH_PC:
                info += "WITH_PC";
                break;
        }
        return info;
    }
    
private:
    DebugCallback debug_callback_;
    std::unordered_set<std::string> enabled_categories_;
    uint64_t debug_start_cycle_ = 0;
    uint64_t debug_end_cycle_ = UINT64_MAX;
    DebugFormatter::Mode output_mode_ = DebugFormatter::Mode::VERBOSE;
    std::ofstream log_file_;
    bool output_to_file_ = false;
    bool output_to_console_ = true;
};

} // namespace riscv

// 类似GEM5的dprintf宏，自动使用全局上下文的cycle
#define dprintf(stage, ...) do { \
    char buffer[1024]; \
    snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
    uint64_t current_cycle = riscv::DebugContext::getInstance().getCurrentCycle(); \
    riscv::DebugManager::getInstance().printf(#stage, std::string(buffer), current_cycle); \
} while(0)

// 支持自定义周期和PC的dprintf宏（用于特殊场景）
#define dprintf_cycle(stage, cycle, pc, ...) do { \
    char buffer[1024]; \
    snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
    riscv::DebugManager::getInstance().printf(#stage, std::string(buffer), cycle, pc); \
} while(0)