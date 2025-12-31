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
#include <cinttypes>
#include <optional>
#include <utility>

namespace riscv {

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error
};

inline const char* levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

/**
 * 调试信息结构体
 * 包含完整的调试信息，支持多种输出格式
 */
struct DebugInfo {
    std::string stage;
    std::string message;
    std::optional<uint64_t> cycle;
    std::optional<uint64_t> pc;
    LogLevel level = LogLevel::Debug;

    DebugInfo() = default;

    DebugInfo(std::string stageValue,
              std::string messageValue,
              LogLevel levelValue = LogLevel::Debug,
              std::optional<uint64_t> cycleValue = std::nullopt,
              std::optional<uint64_t> pcValue = std::nullopt)
        : stage(std::move(stageValue)),
          message(std::move(messageValue)),
          cycle(cycleValue),
          pc(pcValue),
          level(levelValue) {}
};

/**
 * 调试回调函数类型
 */
using DebugCallback = std::function<void(const DebugInfo&)>;

/**
 * 调试输出格式化器
 * 支持多种输出格式和模式
 */
class DebugFormatter {
public:
    enum class Mode {
        SIMPLE,     // 简洁模式: [LEVEL][STAGE] message
        VERBOSE,    // 详细模式: [LEVEL][STAGE] [cycle=] message
        WITH_PC     // 带PC模式: [LEVEL][STAGE] [cycle=] [pc=] message
    };

    // 根据模式格式化输出
    static std::string format(const DebugInfo& info, Mode mode = Mode::VERBOSE) {
        const std::string prefix = fmt::format("[{}][{}]", levelToString(info.level), info.stage);

        switch (mode) {
            case Mode::SIMPLE:
                return fmt::format("{} {}", prefix, info.message);
            case Mode::VERBOSE:
                if (info.cycle) {
                    return fmt::format("{} [cycle={}]: {}", prefix, *info.cycle, info.message);
                }
                return fmt::format("{} {}", prefix, info.message);
            case Mode::WITH_PC:
                if (info.cycle && info.pc) {
                    return fmt::format("{} [cycle={}][pc=0x{:016x}] {}", prefix, *info.cycle, *info.pc, info.message);
                }
                if (info.cycle) {
                    return fmt::format("{} [cycle={}]: {}", prefix, *info.cycle, info.message);
                }
                if (info.pc) {
                    return fmt::format("{} [pc=0x{:016x}] {}", prefix, *info.pc, info.message);
                }
                return fmt::format("{} {}", prefix, info.message);
        }
        return fmt::format("{} {}", prefix, info.message);
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
 * 提供类似GEM5的日志功能，支持分类、等级控制和周期范围
 */
class DebugManager {
public:
    static DebugManager& getInstance() {
        static DebugManager instance;
        return instance;
    }

    // 设置调试输出回调
    void setCallback(DebugCallback callback) {
        debug_callback_ = std::move(callback);
    }

    // 文件输出功能
    void setLogFile(const std::string& filename) {
        if (log_file_.is_open()) {
            log_file_.close();
        }
        log_file_.open(filename, std::ios::out | std::ios::trunc);
        if (!log_file_.is_open()) {
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

    // 日志等级控制
    void setLogLevel(LogLevel level) {
        min_level_ = level;
    }

    LogLevel getLogLevel() const {
        return min_level_;
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
            category.erase(0, category.find_first_not_of(" \t"));
            category.erase(category.find_last_not_of(" \t") + 1);
            if (!category.empty()) {
                enabled_categories_.insert(category);
            }
            current = current.substr(pos + 1);
        }
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

    // 设置全局上下文
    void setGlobalContext(uint64_t cycle, uint64_t pc) {
        global_cycle_ = cycle;
        global_pc_ = pc;
        has_global_cycle_ = true;
        has_global_pc_ = true;
    }

    void clearGlobalContext() {
        has_global_cycle_ = false;
        has_global_pc_ = false;
    }

    uint64_t getCurrentCycle() const {
        return has_global_cycle_ ? global_cycle_ : 0;
    }

    uint64_t getCurrentPC() const {
        return has_global_pc_ ? global_pc_ : 0;
    }

    // 输出调试信息
    void log(const std::string& stage,
             LogLevel level,
             const std::string& message,
             std::optional<uint64_t> cycle = std::nullopt,
             std::optional<uint64_t> pc = std::nullopt) {
        std::optional<uint64_t> resolved_cycle = cycle;
        if (!resolved_cycle && has_global_cycle_) {
            resolved_cycle = global_cycle_;
        }

        std::optional<uint64_t> resolved_pc = pc;
        if (!resolved_pc && has_global_pc_) {
            resolved_pc = global_pc_;
        }

        if (!shouldOutput(stage, resolved_cycle, level)) {
            return;
        }

        DebugInfo info(stage, message, level, resolved_cycle, resolved_pc);

        if (output_to_console_) {
            if (debug_callback_) {
                debug_callback_(info);
            } else {
                std::cout << DebugFormatter::format(info, output_mode_) << std::endl;
            }
        } else if (debug_callback_) {
            debug_callback_(info);
        }

        if (output_to_file_ && log_file_.is_open()) {
            log_file_ << DebugFormatter::format(info, output_mode_) << std::endl;
            log_file_.flush();
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
        info += "\n  Min Level: ";
        info += levelToString(min_level_);
        return info;
    }

private:
    DebugManager() = default;
    ~DebugManager() = default;
    DebugManager(const DebugManager&) = delete;
    DebugManager& operator=(const DebugManager&) = delete;

    bool shouldOutput(const std::string& stage,
                      const std::optional<uint64_t>& cycle,
                      LogLevel level) const {
        if (static_cast<int>(level) < static_cast<int>(min_level_)) {
            return false;
        }

        if (stage != "SYSTEM" &&
            !enabled_categories_.empty() &&
            enabled_categories_.find(stage) == enabled_categories_.end()) {
            return false;
        }

        if (cycle) {
            if (*cycle < debug_start_cycle_ || *cycle > debug_end_cycle_) {
                return false;
            }
        } else {
            if (debug_start_cycle_ != 0 || debug_end_cycle_ != UINT64_MAX) {
                return false;
            }
        }

        if (!output_to_console_ && !output_to_file_ && !debug_callback_) {
            return false;
        }

        return true;
    }

    DebugCallback debug_callback_;
    std::unordered_set<std::string> enabled_categories_;
    uint64_t debug_start_cycle_ = 0;
    uint64_t debug_end_cycle_ = UINT64_MAX;
    DebugFormatter::Mode output_mode_ = DebugFormatter::Mode::VERBOSE;
    std::ofstream log_file_;
    bool output_to_file_ = false;
    bool output_to_console_ = true;
    LogLevel min_level_ = LogLevel::Debug;
    uint64_t global_cycle_ = 0;
    bool has_global_cycle_ = false;
    uint64_t global_pc_ = 0;
    bool has_global_pc_ = false;
};

} // namespace riscv

#define LOG_WITH_LEVEL(level, stage, ...) do { \
    char buffer[1024]; \
    std::snprintf(buffer, sizeof(buffer), __VA_ARGS__); \
    ::riscv::DebugManager::getInstance().log(#stage, level, std::string(buffer)); \
} while (0)

#define LOG_DEBUG(stage, ...) LOG_WITH_LEVEL(::riscv::LogLevel::Debug, stage, __VA_ARGS__)
#define LOG_INFO(stage, ...)  LOG_WITH_LEVEL(::riscv::LogLevel::Info, stage, __VA_ARGS__)
#define LOG_WARN(stage, ...)  LOG_WITH_LEVEL(::riscv::LogLevel::Warn, stage, __VA_ARGS__)
#define LOG_ERROR(stage, ...) LOG_WITH_LEVEL(::riscv::LogLevel::Error, stage, __VA_ARGS__)

// 兼容旧接口
#define dprintf(stage, ...) LOG_DEBUG(stage, __VA_ARGS__)
