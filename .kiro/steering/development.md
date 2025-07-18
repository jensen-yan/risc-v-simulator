# RISC-V 模拟器开发指南

## 开发环境设置

### 依赖项
- C++20 兼容编译器（GCC 10+ 或 Clang 11+）
- CMake 3.16+
- GoogleTest（可选，用于单元测试）
- Python 3.6+（用于运行测试脚本）

### 构建步骤
```bash
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake ..

# 编译
make -j

# 生成调试版本
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j
```

## 代码规范

### 命名约定
- **类名**：使用 PascalCase（如 `MemoryManager`）
- **函数名**：使用 camelCase（如 `loadProgram`）
- **变量名**：使用 snake_case（如 `instruction_count_`）
- **常量**：使用全大写 SNAKE_CASE（如 `MAX_MEMORY_SIZE`）
- **枚举**：枚举名使用 PascalCase，枚举值使用全大写（如 `enum class OpType { ADD, SUB }`）

### 代码格式
- 使用 4 空格缩进（不使用制表符）
- 行宽限制为 120 字符
- 大括号使用 K&R 风格（左括号不换行）
- 类成员变量以下划线结尾（如 `count_`）

### 注释规范
- 使用中文注释解释复杂逻辑
- 每个类和公共方法都应有文档注释
- 使用 `//` 进行单行注释
- 使用 `/* ... */` 进行多行注释

## 测试指南

### 单元测试
- 使用 GoogleTest 框架
- 每个指令都应有对应的单元测试
- 测试文件放在 `tests/` 目录下
- 测试命名格式：`Test{类名}_{功能}`

### 集成测试
- 使用 `run_tests.py` 脚本运行 riscv-tests 测试套件
- 支持不同的测试模式（如 rv32ui-p-*、rv32mi-p-*）
- 可以指定超时时间和输出格式

```bash
# 运行基本整数指令测试
python3 run_tests.py -p "rv32ui-p-*"

# 运行压缩指令测试
python3 run_tests.py -p "rv32uc-p-*"

# 运行乘除法指令测试
python3 run_tests.py -p "rv32um-p-*"
```

## 调试技巧

### 调试模式
模拟器支持多种调试模式：
- `-d, --debug`：启用基本调试输出
- `--debug-preset=<preset>`：使用预设调试配置
- `--debug-categories=<cats>`：指定调试分类
- `--debug-cycles=<start>-<end>`：指定调试周期范围

### 可用的调试预设
- `basic`：基础流水线（fetch, decode, commit）
- `ooo`：乱序执行（fetch, decode, issue, execute, writeback, commit, rob, rename, rs）
- `pipeline`：完整流水线（fetch, decode, issue, execute, writeback, commit）
- `performance`：性能分析（execute, commit, rob, rs, branch, stall）
- `detailed`：所有调试信息
- `memory`：内存访问（fetch, memory, execute, commit）
- `branch`：分支预测（fetch, decode, execute, commit, branch）
- `minimal`：最小调试（fetch, commit）

### 单步执行
使用 `-s, --step` 选项启用单步执行模式，每次执行一条指令后暂停，等待用户输入。

## 扩展开发

### 添加新指令
1. 在 `common/types.h` 中更新相关枚举（如 `Opcode`、`Funct3`、`Funct7`）
2. 在 `Decoder` 类中添加指令解码逻辑
3. 在 `CPU` 类中实现指令执行逻辑
4. 添加单元测试验证功能

### 添加新扩展
1. 在 `Extension` 枚举中添加新标志
2. 实现扩展指令的解码和执行逻辑
3. 更新 `validateInstruction` 检查逻辑
4. 添加扩展指令的测试用例

### 优化性能
- 使用内联函数优化高频调用
- 考虑使用查找表代替复杂条件分支
- 优化内存访问模式，提高缓存命中率
- 使用编译器优化选项（如 `-O3`）

## 常见问题解决

### 内存错误
- 检查内存边界检查逻辑
- 验证地址对齐要求
- 确保内存大小配置正确

### 指令执行错误
- 检查指令解码逻辑
- 验证立即数符号扩展
- 确认寄存器编号范围

### 系统调用问题
- 检查 ABI 参数传递约定
- 验证系统调用号映射
- 确保内存读写操作正确

## 性能分析

### 性能指标
- 指令执行速度（IPS，每秒指令数）
- 内存使用量
- 缓存命中率（如适用）
- 分支预测准确率（如适用）

### 分析工具
- 使用 `--debug-preset=performance` 获取性能统计
- 考虑使用外部工具如 perf、Valgrind 进行分析
- 对比不同 CPU 模型（顺序执行 vs 乱序执行）的性能