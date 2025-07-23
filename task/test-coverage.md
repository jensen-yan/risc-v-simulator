# 测试覆盖率提升计划

## 当前覆盖率状态

**整体覆盖率（2025-01-23）：**
- **行覆盖率：48.4%** (1549/3201行)
- **函数覆盖率：66.0%** (256/388函数)
- **分支覆盖率：暂无数据**

## 详细模块覆盖率分析

### 🔥 优先级1 - 紧急需要测试的模块

| 模块 | 文件路径 | 行覆盖率 | 函数覆盖率 | 状态 |
|------|----------|----------|------------|------|
| difftest | src/system/difftest.cpp | **0%** (0/69) | **0%** (0/10) | ❌ 完全未测试 |
| instruction_executor | src/core/instruction_executor.cpp | **27.5%** (86/313) | **50%** (14/28) | ❌ 严重不足 |
| syscall_handler | src/system/syscall_handler.cpp | **13.4%** (13/97) | **22.2%** (2/9) | ❌ 严重不足 |

### 🚨 优先级2 - 重要但部分覆盖的模块

| 模块 | 文件路径 | 行覆盖率 | 函数覆盖率 | 状态 |
|------|----------|----------|------------|------|
| execute_stage | src/cpu/ooo/stages/execute_stage.cpp | **28.9%** (82/284) | **38.9%** (7/18) | ⚠️ 需要加强 |
| commit_stage | src/cpu/ooo/stages/commit_stage.cpp | **43.2%** (57/132) | **33.3%** (3/9) | ⚠️ 需要加强 |
| decoder | src/core/decoder.cpp | **24.0%** (98/409) | **80%** (16/20) | ⚠️ 行覆盖不足 |
| fetch_stage | src/cpu/ooo/stages/fetch_stage.cpp | **41.4%** (24/58) | **50%** (2/4) | ⚠️ 需要完善 |
| issue_stage | src/cpu/ooo/stages/issue_stage.cpp | **58.1%** (25/43) | **50%** (2/4) | ⚠️ 需要完善 |

### ⚠️ 优先级3 - 需要完善的模块

| 模块 | 文件路径 | 行覆盖率 | 函数覆盖率 | 状态 |
|------|----------|----------|------------|------|
| ooo_cpu | src/cpu/ooo/ooo_cpu.cpp | **55.0%** (115/209) | **50%** (16/32) | ✅ 基础覆盖 |
| inorder_cpu | src/cpu/inorder/cpu.cpp | **40.6%** (104/256) | **46.7%** (14/30) | ✅ 基础覆盖 |
| dynamic_inst | src/cpu/ooo/dynamic_inst.cpp | **44.3%** (86/194) | **64.7%** (11/17) | ✅ 基础覆盖 |
| memory | src/core/memory.cpp | **53.8%** (56/104) | **78.6%** (11/14) | ✅ 良好覆盖 |

### ✅ 已有良好覆盖的模块

| 模块 | 文件路径 | 行覆盖率 | 函数覆盖率 | 状态 |
|------|----------|----------|------------|------|
| reservation_station | src/cpu/ooo/reservation_station.cpp | **90.0%** (208/231) | **90.9%** (20/22) | ✅ 优秀覆盖 |
| reorder_buffer | src/cpu/ooo/reorder_buffer.cpp | **73.2%** (167/228) | **81.3%** (26/32) | ✅ 良好覆盖 |
| register_rename | src/cpu/ooo/register_rename.cpp | **80.0%** (132/165) | **89.5%** (17/19) | ✅ 良好覆盖 |
| store_buffer | src/cpu/ooo/store_buffer.cpp | **72.5%** (66/91) | **88.9%** (8/9) | ✅ 良好覆盖 |

## 6周测试实施计划

### 阶段1: 紧急优先级模块 (第1-2周)

#### Week 1: 创建基础测试框架
- [ ] **创建 test_difftest.cpp** 
  - 目标：0% → 80%+ 覆盖率
  - 测试内容：构造函数、状态同步、比较逻辑、错误检测
- [ ] **扩展 test_instruction_executor.cpp**
  - 目标：27.5% → 60%+ 覆盖率  
  - 测试内容：基础指令类型、边界值测试

#### Week 2: 系统调用和指令执行
- [ ] **创建 test_syscall_handler.cpp**
  - 目标：13.4% → 70%+ 覆盖率
  - 测试内容：系统调用表、文件操作、异常处理
- [ ] **完善指令执行器测试**
  - 目标：60% → 75%+ 覆盖率
  - 测试内容：复杂指令、异常情况、RV64I特有指令

### 阶段2: 核心流水线模块 (第3-4周)

#### Week 3: 执行和提交阶段
- [ ] **扩展 test_execute_stage.cpp**
  - 目标：28.9% → 65%+ 覆盖率
  - 测试内容：多发射执行、数据前向传递、异常处理
- [ ] **扩展 test_commit_stage.cpp** 
  - 目标：43.2% → 70%+ 覆盖率
  - 测试内容：顺序提交、异常回滚、状态更新

#### Week 4: 解码和流水线协调
- [ ] **扩展 test_decoder.cpp**
  - 目标：24.0% → 60%+ 覆盖率
  - 测试内容：指令格式解码、立即数扩展、控制信号
- [ ] **完善 stage 测试**
  - 完善 fetch_stage 和 issue_stage 测试
  - 测试流水线协调逻辑

### 阶段3: CPU核心逻辑完善 (第5-6周)

#### Week 5: CPU整体测试
- [ ] **完善 test_ooo_cpu.cpp**
  - 目标：55.0% → 75%+ 覆盖率
  - 测试内容：复杂指令序列、依赖处理、异常处理
- [ ] **创建 test_inorder_cpu.cpp**
  - 目标：40.6% → 70%+ 覆盖率
  - 测试内容：基础流水线、分支处理、对比测试

#### Week 6: 集成测试和优化
- [ ] **集成测试**
  - 完整测试套件运行
  - 性能回归测试
  - 覆盖率验证
- [ ] **文档和维护**
  - 更新测试文档
  - 建立持续集成
  - 覆盖率监控

## 测试工具和方法指南

### 代码覆盖率监控

**生成覆盖率报告：**
```bash
# 使用提供的脚本
./run_coverage.sh

# 手动生成
mkdir build_coverage && cd build_coverage
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..
make -j
./risc-v-tests
lcov --capture --directory . --output-file coverage.info --ignore-errors gcov,mismatch
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/build*/*' --output-file coverage_filtered.info --ignore-errors empty
genhtml coverage_filtered.info --output-directory coverage_html
```

**查看覆盖率报告：**
- HTML报告：`build_coverage/coverage_html/index.html`
- 在WSL中：`explorer.exe coverage_html/index.html`
- HTTP服务：`python3 -m http.server 8000`

### 测试编写规范

**测试文件命名：**
- 格式：`test_[module_name].cpp`
- 示例：`test_difftest.cpp`, `test_instruction_executor.cpp`

**测试类命名：**
```cpp
class DiffTestTest : public ::testing::Test {
protected:
    void SetUp() override { /* 初始化 */ }
    void TearDown() override { /* 清理 */ }
};
```

**测试用例命名：**
```cpp
TEST_F(DiffTestTest, ConstructorWithValidInputs) { /* 测试内容 */ }
TEST_F(DiffTestTest, CompareRegistersWithMismatch) { /* 测试内容 */ }
```

### 测试辅助工具

**指令构造辅助函数：**
```cpp
// 在测试基类中提供
uint32_t createRTypeInstruction(uint8_t funct7, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode);
uint32_t createITypeInstruction(uint16_t imm, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode);
// 等等...
```

**内存和寄存器验证：**
```cpp
void verifyRegisterValue(uint8_t reg, uint64_t expected_value);
void verifyMemoryValue(uint64_t address, uint64_t expected_value);
void compareSystemState(ICpuInterface* cpu1, ICpuInterface* cpu2);
```

## 预期结果

### 覆盖率目标
- **整体行覆盖率：** 48.4% → 70%+
- **整体函数覆盖率：** 66.0% → 80%+
- **关键模块覆盖率：** 所有优先级1模块达到70%+

### 质量提升
- ✅ 核心功能完全测试覆盖
- ✅ 边界条件和异常情况测试
- ✅ 回归测试防护
- ✅ 持续集成保障

### 维护性改进
- ✅ 标准化测试框架
- ✅ 自动化覆盖率监控
- ✅ 完善的测试文档
- ✅ 可扩展的测试架构

## 进度跟踪

**当前状态：** 规划阶段完成 ✅  
**下一步：** 开始第1周测试实施

**更新日志：**
- 2025-01-23: 初始覆盖率分析和计划制定
- 待更新: 各阶段实施进度

---

> 💡 **提示**: 可以使用 `./run_coverage.sh` 快速生成最新的覆盖率报告，建议每周运行一次来跟踪进度。