# RISC-V 乱序CPU调试修复进度记录

## 🎯 当前任务
修复乱序CPU在RISC-V测试中的寄存器值不一致问题

## 📊 问题描述
在运行 `rv32ui-p-add` 测试时，发现寄存器x4(tp)的值在乱序CPU与参考CPU之间不一致：
- **参考CPU**: x4 = 0x2
- **乱序CPU**: x4 = 0x1

## 🔍 问题定位
### 关键指令
```assembly
80000380: 00120213  addi tp,tp,1  # tp = tp + 1
```

### 根因分析
**自依赖寄存器处理问题**：
- `addi tp,tp,1` 指令中源寄存器和目标寄存器相同(x4)
- 寄存器重命名逻辑在循环中未能正确处理这种自依赖关系
- 导致循环中第二次执行该指令时，使用了错误的初始值

## ✅ 已完成的修复

### 1. 寄存器重命名修复
**文件**: `src/cpu/ooo/register_rename.cpp`
**修复内容**:
```cpp
// 关键修复：如果源寄存器和目标寄存器相同（如addi tp,tp,1），
// 需要确保源寄存器仍然指向旧的物理寄存器
if (instruction.rs1 == instruction.rd && instruction.rs1 < NUM_LOGICAL_REGS) {
    result.src1_reg = old_physical_reg;
    result.src1_ready = physical_registers[old_physical_reg].ready;
    result.src1_value = physical_registers[old_physical_reg].value;
    dprintf(RENAME, "修复自依赖: x%d 源使用 p%d (旧值), 目标使用 p%d (新值)", 
            (int)instruction.rd, (int)old_physical_reg, (int)result.dest_reg);
}
```

### 2. 诊断警告修复
**文件**: `src/cpu/ooo/stages/commit_stage.cpp`
**修复内容**: 移除了未使用的头文件引用

**文件**: `src/cpu/ooo/stages/fetch_stage.cpp`
**修复内容**: 移除了未使用的头文件引用

## 🔧 当前状态
- ✅ **修复自依赖问题**: 寄存器重命名逻辑已正确修复
- ✅ **构建成功**: 项目编译无错误
- ⚠️ **测试仍存在问题**: 寄存器x4值仍不一致

## 🎯 调试发现
从详细日志分析发现：
- ✅ "修复自依赖" 消息已出现，表明修复代码生效
- ✅ 物理寄存器正确分配和释放
- ❌ **循环第二次迭代时仍出现值不一致**
- ❌ **分支跳转可能影响寄存器更新时机**

## 🔍 深层次问题
**循环依赖链问题**：
1. **分支指令影响**: `bne tp,t0,80000370` 的跳转可能干扰寄存器状态
2. **流水线刷新**: 分支跳转后的流水线刷新可能错误影响寄存器重命名
3. **提交顺序**: 循环中指令的提交顺序可能影响最终结果

## 🎯 下一步行动
1. **分析分支指令影响**: 检查循环中的分支跳转是否影响寄存器更新
2. **验证依赖链完整性**: 确保循环中的寄存器依赖链在重命名后仍保持正确
3. **DiffTest机制分析**: 检查差异测试的比较时机是否正确

## 📈 关键发现
问题可能不仅在于寄存器重命名，还可能涉及：
- **分支指令的提交顺序**
- **循环回跳时的寄存器状态恢复**
- **DiffTest状态同步时机**

## 🚀 继续方向
需要进一步分析：
- 分支指令 `bne tp,t0,80000370` 的执行对寄存器状态的影响
- 循环回跳时的流水线刷新行为
- DiffTest状态比较的精确时机

## 📋 验证日志
```
[RENAME] Cycle 232: 修复自依赖: x4 源使用 p4 (旧值), 目标使用 p36 (新值)
[DIFFTEST] Cycle 237: 寄存器x4不一致: 参考CPU=0x2, 乱序CPU=0x1
```