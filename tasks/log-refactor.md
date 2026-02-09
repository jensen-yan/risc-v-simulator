# 调试与日志系统重构方案

本文档旨在分析当前 RISC-V 模拟器项目中的调试与日志系统，并提出一个具体的重构方案，以解决现有问题并提升系统的可维护性和易用性。

## 1. 现状分析

经过对代码库的详细阅读和编译分析，当前调试系统主要存在以下几个特点和问题：

### 1.1. 编译告警

在 `make` 过程中，出现了大量编译告警，主要包括：
- **文件末尾的反斜杠** (`warning: backslash-newline at end of file`): 存在于 `include/common/debug_types.h` 的宏定义中。
- **未使用的变量/参数** (`warning: unused parameter/variable`): 表明存在冗余或未完成的代码。
- **变量赋值后未使用** (`warning: variable ‘...’ set but not used`): 可能是逻辑冗余。
- **成员变量遮蔽** (`warning: shadow-member-variable`): 降低代码可读性。
- **枚举值未在 switch 中处理** (`warning: enumeration value ‘...’ not handled in switch`): 可能导致未定义行为。

这些告警虽然不影响编译，但反映了代码质量和健壮性有待提升。

### 1.2. 调试系统机制

项目中���存两套功能重叠的调试输出机制：

1.  **`dprintf` 宏系列**:
    *   **机制**: 这是项目的主要调试工具，通过 `DebugManager` 单例实现，支持按类别、周期和预设进行过滤。
    *   **问题**:
        *   **上下文强耦合**: 宏的调用方必须手动传入 `cycle` 和 `pc`。
        *   **变通方案**: 为了解决上下文问题，代码引入了 `DebugContext`，这是一种脆弱的、基于线程局部存储的解决方案，将调试逻辑与 `OutOfOrderCPU` 的实现紧密绑定。

2.  **`print_stage_activity` 方法**:
    *   **机制**: `OutOfOrderCPU` 的一个成员函数，直接调用 `DebugManager`。
    *   **问题**:
        *   **API 冗余**: 与 `dprintf` 功能重复，造成了 API 的混乱和不统一。
        *   **耦合性高**: 被硬编码在各个流水线阶段的类中，加深了与 `OutOfOrderCPU` 的耦合。

### 1.3. 核心问题总结

- **上下文依赖**: `cycle` 和 `pc` 不是全局概念，导致调试系统难以在模拟器的其他部分（如顺序执行CPU）中复用。
- **API 不统一**: `dprintf` 和 `print_stage_activity` 的并存增加了认知负担和维护成本。
- **灵活性不足**: 当前系统强制要求提供 `pc`，无法满足只输出 `cycle` 等定制化需求。

## 2. 重构方案 (方案A)

我们选���对现有调试系统进行重构，而不是引入新的外部依赖。此方案风险小、改动可控，且能完美解决现有问题。

### 第一步：清理与统一

1.  **修复编译告警**: 在进行重构前，首先修复所有编译告警，确保代码的健康状态。
2.  **统一 API**: 在整个项目中，将所有 `print_stage_activity` 的调用替换为 `dprintf`，彻底废弃 `print_stage_activity` 方法。

### 第二步：建立全局上下文，解耦调试系统

这是重构的核心，旨在将调试系统与任何特定的CPU实现解耦。

1.  **提升 `cycle_count` 的地位**:
    *   将 `cycle_count_` 成员变量从 `cpu/ooo/cpu_state.h` 移至 `system/simulator.h` 的 `Simulator` 类中。`Simulator` 作为顶层控制器，是维护全局周期的最合适位置。

2.  **由 `Simulator` 更新 `DebugManager`**:
    *   在 `Simulator::step()` 的主循环中，每次递增 `cycle_count_` 后，调用 `DebugManager` 的新方法来更新全局上下文。
    *   **示例**:
        ```cpp
        // In system/simulator.cpp
        void Simulator::step() {
            cycle_count_++; // 递增全局周期
            // 使用新周期和当前PC更新DebugManager
            DebugManager::getInstance().setGlobalContext(cycle_count_, cpu_->getPC());
            cpu_->step();
        }
        ```

3.  **改造 `DebugManager`**:
    *   在 `DebugManager` 中添加私有成员 `global_cycle_` 和 `global_pc_`。
    *   添加公共方法 `setGlobalContext(uint64_t cycle, uint32_t pc)` 来更新这两个值。
    *   添加 `getCurrentCycle()` 和 `getCurrentPC()` 方法供内部使用。

### 第三步：简化并增强日志宏

1.  **创建新的日志宏**:
    *   在 `debug_types.h` 中，将现有的 `dprintf` 宏重构为一个新的、更简洁的宏，例如 `LOG_DEBUG`。
    *   这个新宏将不再需要手动传入 `cycle` 和 `pc`，而是从 `DebugManager` 的全局上下文中自动获取。
    *   **示例**:
        ```cpp
        // In include/common/debug_types.h
        #define LOG_DEBUG(stage, format, ...)                                 \
            do {                                                              \
                auto& manager = riscv::DebugManager::getInstance();           \
                if (manager.isCategoryEnabled(stage)) {                       \
                    uint64_t cycle = manager.getCurrentCycle();               \
                    if (manager.isCycleInRange(cycle)) {                      \
                        uint32_t pc = manager.getCurrentPC();                 \
                        /* 调用真正的打印函数，自动���入上下文 */                  \
                        manager.printf(stage, format, cycle, pc, ##__VA_ARGS__); \
                    }                                                         \
                }                                                             \
            } while (0)
        ```

2.  **调整输出格式**:
    *   修改 `DebugFormatter::format` 方法，使其更加灵活。可以增加一个新的输出模式，在该模式下不显示 `pc` 信息，以满足用户需求。

## 3. 备选方案：引入 `spdlog`

- **优点**: 功能强大、高性能、类型安全。
- **缺点**: 引入新依赖，增加项目配置复杂性，有一定学习成本，需要重写过滤逻辑。
- **结论**: 对于本项目，`spdlog` 提供的额外功能可能过于复杂，而其引入的依赖和配置成本较高。因此，内部重构是更优选择。

## 4. 结论

推荐采用**方案A**进行重构。此方案通过**解耦上下文**、**统一API**和**简化宏调用**，能够以最小的代价解决当前调试系统的所有核心问题，显著提高代码质量和可维护性，同时保持项目的简洁与独立。
