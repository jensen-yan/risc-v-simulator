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
#include <fmt/printf.h>
#include <cinttypes>
#include <optional>
#include <utility>
#include <cctype>

namespace riscv {

enum class LogLevel {
    TRACE = 0,
    INFO,
    WARN,
    ERROR,
    FATAL
};

/**
 * 调试信息结构体
 * 包含完整的调试信息，支持多种输出格式
 */
struct DebugInfo {
    std::string category;
    std::string message;
    LogLevel level = LogLevel::TRACE;
    std::optional<uint64_t> cycle;
    std::optional<uint64_t> pc;

    DebugInfo() = default;

    DebugInfo(std::string categoryValue,
              std::string messageValue,
              LogLevel levelValue = LogLevel::TRACE,
              std::optional<uint64_t> cycleValue = std::nullopt,
              std::optional<uint64_t> pcValue = std::nullopt)
        : category(std::move(categoryValue)),
          message(std::move(messageValue)),
          level(levelValue),
          cycle(cycleValue),
          pc(pcValue) {}
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
    // Trace默认格式: [CAT][c=] message
    // Event格式: [LEVEL][CAT][c=] message
    static std::string format(const DebugInfo& info) {
        const std::string cat_part = fmt::format("[{}]", info.category);
        const std::string cycle_part = info.cycle ? fmt::format("[c={}]", *info.cycle) : "[c=-]";
        const bool with_level = info.level != LogLevel::TRACE;
        if (with_level) {
            return fmt::format("[{}]{}{} {}", toString(info.level), cat_part, cycle_part, info.message);
        }
        return fmt::format("{}{} {}", cat_part, cycle_part, info.message);
    }

private:
    static const char* toString(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "TRACE";
        }
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

    // 启用/禁用调试分类
    void enableCategory(const std::string& category) {
        enabled_categories_.insert(toUpper(category));
    }

    void disableCategory(const std::string& category) {
        enabled_categories_.erase(toUpper(category));
    }

    void clearCategories() {
        enabled_categories_.clear();
    }

    // 批量设置调试分类
    void setCategories(const std::vector<std::string>& categories) {
        enabled_categories_.clear();
        for (const auto& category : categories) {
            enabled_categories_.insert(toUpper(category));
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
                enabled_categories_.insert(toUpper(category));
            }
            current = current.substr(pos + 1);
        }
        current.erase(0, current.find_first_not_of(" \t"));
        current.erase(current.find_last_not_of(" \t") + 1);
        if (!current.empty()) {
            enabled_categories_.insert(toUpper(current));
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

    // 快速判断是否需要记录该分类日志（用于避免无效格式化开销）
    bool shouldLog(const std::string& category,
                   std::optional<uint64_t> cycle = std::nullopt) const {
        std::optional<uint64_t> resolved_cycle = cycle;
        if (!resolved_cycle && has_global_cycle_) {
            resolved_cycle = global_cycle_;
        }
        return shouldOutput(toUpper(category), resolved_cycle);
    }

    // Trace日志（默认）
    void logTrace(const std::string& category,
                  const std::string& message,
                  std::optional<uint64_t> cycle = std::nullopt,
                  std::optional<uint64_t> pc = std::nullopt) {
        logImpl(LogLevel::TRACE, category, message, cycle, pc);
    }

    // Event日志（建议用于重要事件）
    void logEvent(LogLevel level,
                  const std::string& category,
                  const std::string& message,
                  std::optional<uint64_t> cycle = std::nullopt,
                  std::optional<uint64_t> pc = std::nullopt) {
        logImpl(level, category, message, cycle, pc);
    }

    // 兼容旧接口：等价于Trace日志
    void log(const std::string& category,
             const std::string& message,
             std::optional<uint64_t> cycle = std::nullopt,
             std::optional<uint64_t> pc = std::nullopt) {
        logTrace(category, message, cycle, pc);
    }

private:
    void logImpl(LogLevel level,
                 const std::string& category,
                 const std::string& message,
                 std::optional<uint64_t> cycle,
                 std::optional<uint64_t> pc) {
        std::optional<uint64_t> resolved_cycle = cycle;
        if (!resolved_cycle && has_global_cycle_) {
            resolved_cycle = global_cycle_;
        }

        std::optional<uint64_t> resolved_pc = pc;
        if (!resolved_pc && has_global_pc_) {
            resolved_pc = global_pc_;
        }

        const std::string normalized_category = toUpper(category);
        if (!shouldOutput(normalized_category, resolved_cycle)) {
            return;
        }

        DebugInfo info(normalized_category, message, level, resolved_cycle, resolved_pc);

        if (output_to_console_) {
            if (debug_callback_) {
                debug_callback_(info);
            } else {
                std::clog << DebugFormatter::format(info) << std::endl;
            }
        } else if (debug_callback_) {
            debug_callback_(info);
        }

        if (output_to_file_ && log_file_.is_open()) {
            log_file_ << DebugFormatter::format(info) << std::endl;
            log_file_.flush();
        }
    }

public:

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
        return info;
    }

private:
    DebugManager() = default;
    ~DebugManager() = default;
    DebugManager(const DebugManager&) = delete;
    DebugManager& operator=(const DebugManager&) = delete;

    static std::string toUpper(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        for (unsigned char ch : input) {
            result.push_back(static_cast<char>(std::toupper(ch)));
        }
        return result;
    }

    bool shouldOutput(const std::string& category,
                      const std::optional<uint64_t>& cycle) const {

        if (!enabled_categories_.empty() &&
            enabled_categories_.find(category) == enabled_categories_.end()) {
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
    std::ofstream log_file_;
    bool output_to_file_ = false;
    bool output_to_console_ = false;
    uint64_t global_cycle_ = 0;
    bool has_global_cycle_ = false;
    uint64_t global_pc_ = 0;
    bool has_global_pc_ = false;
};

} // namespace riscv

#define LOG_DEBUG(stage, ...) do { \
    auto& _dm = ::riscv::DebugManager::getInstance(); \
    if (_dm.shouldLog(#stage)) { \
        const auto message = fmt::sprintf(__VA_ARGS__); \
        _dm.log(#stage, message); \
    } \
} while (0)

#define LOGT(stage, ...) do { \
    auto& _dm = ::riscv::DebugManager::getInstance(); \
    if (_dm.shouldLog(#stage)) { \
        const auto message = fmt::sprintf(__VA_ARGS__); \
        _dm.logTrace(#stage, message); \
    } \
} while (0)

#define LOGI(stage, ...) do { \
    auto& _dm = ::riscv::DebugManager::getInstance(); \
    if (_dm.shouldLog(#stage)) { \
        const auto message = fmt::sprintf(__VA_ARGS__); \
        _dm.logEvent(::riscv::LogLevel::INFO, #stage, message); \
    } \
} while (0)

#define LOGW(stage, ...) do { \
    auto& _dm = ::riscv::DebugManager::getInstance(); \
    if (_dm.shouldLog(#stage)) { \
        const auto message = fmt::sprintf(__VA_ARGS__); \
        _dm.logEvent(::riscv::LogLevel::WARN, #stage, message); \
    } \
} while (0)

#define LOGE(stage, ...) do { \
    auto& _dm = ::riscv::DebugManager::getInstance(); \
    if (_dm.shouldLog(#stage)) { \
        const auto message = fmt::sprintf(__VA_ARGS__); \
        _dm.logEvent(::riscv::LogLevel::ERROR, #stage, message); \
    } \
} while (0)

// 兼容旧接口
#define dprintf(stage, ...) LOGT(stage, __VA_ARGS__)
