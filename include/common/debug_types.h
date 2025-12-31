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

/**
 * 调试信息结构体
 * 包含完整的调试信息，支持多种输出格式
 */
struct DebugInfo {
    std::string stage;
    std::string message;
    std::optional<uint64_t> cycle;
    std::optional<uint64_t> pc;

    DebugInfo() = default;

    DebugInfo(std::string stageValue,
              std::string messageValue,
              std::optional<uint64_t> cycleValue = std::nullopt,
              std::optional<uint64_t> pcValue = std::nullopt)
        : stage(std::move(stageValue)),
          message(std::move(messageValue)),
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
    // 输出固定verbose格式: [STAGE] [c=] message
    static std::string format(const DebugInfo& info) {
        const std::string prefix = fmt::format("[{}]", info.stage);
        const std::string cycle_part = info.cycle ? fmt::format(" [c={}]", *info.cycle) : "";

        return fmt::format("{}{} {}", prefix, cycle_part, info.message);
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

    // 输出调试信息
    void log(const std::string& stage,
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

        if (!shouldOutput(stage, resolved_cycle)) {
            return;
        }

        DebugInfo info(stage, message, resolved_cycle, resolved_pc);

        if (output_to_console_) {
            if (debug_callback_) {
                debug_callback_(info);
            } else {
                std::cout << DebugFormatter::format(info) << std::endl;
            }
        } else if (debug_callback_) {
            debug_callback_(info);
        }

        if (output_to_file_ && log_file_.is_open()) {
            log_file_ << DebugFormatter::format(info) << std::endl;
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

    bool shouldOutput(const std::string& stage,
                      const std::optional<uint64_t>& cycle) const {

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
    std::ofstream log_file_;
    bool output_to_file_ = false;
    bool output_to_console_ = true;
    uint64_t global_cycle_ = 0;
    bool has_global_cycle_ = false;
    uint64_t global_pc_ = 0;
    bool has_global_pc_ = false;
};

} // namespace riscv

#define LOG_DEBUG(stage, ...) do { \
    const auto message = fmt::sprintf(__VA_ARGS__); \
    ::riscv::DebugManager::getInstance().log(#stage, message); \
} while (0)

// 兼容旧接口
#define dprintf(stage, ...) LOG_DEBUG(stage, __VA_ARGS__)
