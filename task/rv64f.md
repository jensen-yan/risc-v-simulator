# RV64F/RV64D浮点扩展支持任务文档

## 项目概述

为RISC-V模拟器添加完整的RV64F（单精度浮点）和RV64D（双精度浮点）扩展支持，目标是通过所有相关测试用例：
- `rv64uf-p-*` (RV64F单精度浮点测试)
- `rv64ud-p-*` (RV64D双精度浮点测试)

## 当前进度状态 ✅

### 已完成 (✅ 第一阶段和第二阶段)

#### 第一阶段：基础设施升级 ✅
1. **✅ 扩展浮点寄存器文件**：将`fp_registers_`从32位扩展到64位数组
   - 更新了`include/cpu/inorder/cpu.h`：浮点寄存器数组改为`std::array<uint64_t, NUM_FP_REGISTERS>`
   - 更新了`include/cpu/ooo/cpu_state.h`：架构和物理浮点寄存器都改为64位
   - 实现了双精度浮点寄存器访问方法：`getFPRegisterDouble()`, `setFPRegisterDouble()`
   - 更新了所有CPU实现和工厂类，支持32位和64位访问

2. **✅ 扩展Funct7定义**：在`types.h`中添加了所有F/D扩展的Funct7常量
   - 添加了完整的RV64F单精度指令Funct7：`FADD_S`, `FSUB_S`, `FMUL_S`, `FDIV_S`, `FSQRT_S`, `FSGNJ_S`, `FMIN_FMAX_S`, `FCVT_INT_S`, `FMV_X_W`, `FCLASS_S`, `FCMP_S`, `FCVT_S_INT`, `FMV_W_X`
   - 添加了完整的RV64D双精度指令Funct7：`FADD_D`, `FSUB_D`, `FMUL_D`, `FDIV_D`, `FSQRT_D`, `FSGNJ_D`, `FMIN_FMAX_D`, `FCVT_INT_D`, `FMV_X_D`, `FCLASS_D`, `FCMP_D`, `FCVT_D_INT`, `FMV_D_X`, `FCVT_S_D`, `FCVT_D_S`
   - 添加了R4型指令类型：`InstructionType::R4_TYPE`
   - 添加了融合乘加指令操作码：`FMADD`, `FMSUB`, `FNMSUB`, `FNMADD`
   - 添加了浮点相关的Funct3常量：`FEQ`, `FLT`, `FLE`, `FSGNJ`, `FSGNJN`, `FSGNJX`, `FMIN`, `FMAX`, `FMV_CLASS`, `FCLASS`

3. **✅ 添加简化CSR支持**：实现基本的CSR读写接口（固定返回默认值）
   - 在`common/cpu_interface.h`中添加了`getCSR()`, `setCSR()`接口
   - 在`types.h`中添加了CSR地址常量：`FFLAGS`, `FRM`, `FCSR`, `MSTATUS`, `MISA`, `MIE`, `MTVEC`
   - 在所有CPU实现中提供了简化的CSR支持，返回合理的默认值

#### 第二阶段：解码器增强 ✅
1. **✅ 扩展解码器**：支持所有F/D扩展指令的解码
   - 更新了`determineType()`函数，添加了R4型指令支持
   - 在`decode()`函数中添加了R4_TYPE的立即数处理
   - 完善了`validateInstruction()`函数，添加了完整的F/D扩展验证：
     - 检查F扩展指令（包括融合乘加指令）
     - 检查D扩展指令及其对F扩展的依赖性
     - 检查浮点加载/存储指令的扩展要求

2. **✅ 指令格式支持**：添加了R4型指令（融合乘加指令）格式支持

### 当前支持的浮点指令 ✅
- **基本算术**：FADD.S, FSUB.S, FMUL.S, FDIV.S, FSQRT.S
- **比较指令**：FEQ.S, FLT.S, FLE.S（完整比较支持）
- **符号操作**：FSGNJ.S, FSGNJN.S, FSGNJX.S（已实现）
- **最小值/最大值**：FMIN.S, FMAX.S（已实现）
- **转换指令**：FCVT.W.S, FCVT.WU.S, FCVT.L.S, FCVT.LU.S, FCVT.S.W, FCVT.S.WU, FCVT.S.L, FCVT.S.LU（已实现）
- **移动指令**：FMV.X.W（已实现）
- **分类指令**：FCLASS.S（已实现）

### 正在进行 (🚧 第三阶段)

#### 第三阶段：执行器实现（进行中） 🚧
1. **🚧 重构执行器**：正在扩展`instruction_executor.cpp`
   - ✅ 已实现大部分RV64F单精度指令执行逻辑
   - 🚧 需要添加融合乘加指令执行方法
   - ⏳ 待实现双精度指令执行逻辑
   - ⏳ 待实现FMV.W.X指令

### 待完成功能 ⏳
1. **⏳ 融合乘加指令执行**：FMADD.S, FMSUB.S, FNMSUB.S, FNMADD.S
2. **⏳ 双精度指令完整实现**：所有RV64D指令
3. **⏳ 浮点加载/存储指令**：FLW/FSW, FLD/FSD  
4. **⏳ CPU集成**：顺序和乱序CPU的浮点执行逻辑集成
5. **⏳ 测试验证**：运行rv64uf-p-*测试套件

### 简化实现策略
为了满足CoreMark、SPEC CPU 2006和Linux系统的需求，我们采用简化实现：
- **CSR寄存器**：提供基本读写接口，固定返回合理默认值
- **舍入模式**：固定使用IEEE 754默认舍入模式（Round to Nearest Even）
- **异常标志**：不实际设置异常标志，但产生正确的计算结果
- **NaN处理**：使用C++标准库的基本浮点行为

## 需要支持的指令集

### RV64F指令（单精度32位）
1. **算术运算**
   - FADD.S, FSUB.S, FMUL.S, FDIV.S - 四则运算
   - FSQRT.S - 平方根
   - FMIN.S, FMAX.S - 最小值/最大值

2. **融合乘加**
   - FMADD.S, FMSUB.S, FNMSUB.S, FNMADD.S

3. **比较指令**
   - FEQ.S, FLT.S, FLE.S - 相等/小于/小于等于比较

4. **符号操作**
   - FSGNJ.S, FSGNJN.S, FSGNJX.S - 符号注入

5. **分类指令**
   - FCLASS.S - 浮点数分类

6. **转换指令**
   - FCVT.W.S, FCVT.WU.S - 浮点转32位整数
   - FCVT.L.S, FCVT.LU.S - 浮点转64位整数（RV64特有）
   - FCVT.S.W, FCVT.S.WU - 32位整数转浮点
   - FCVT.S.L, FCVT.S.LU - 64位整数转浮点（RV64特有）

7. **移动指令**
   - FMV.X.W, FMV.W.X - 浮点寄存器与整数寄存器间移动

8. **加载/存储**
   - FLW, FSW - 32位浮点加载/存储

### RV64D指令（双精度64位）
1. **算术运算**
   - FADD.D, FSUB.D, FMUL.D, FDIV.D - 四则运算
   - FSQRT.D - 平方根
   - FMIN.D, FMAX.D - 最小值/最大值

2. **融合乘加**
   - FMADD.D, FMSUB.D, FNMSUB.D, FNMADD.D

3. **比较指令**
   - FEQ.D, FLT.D, FLE.D - 相等/小于/小于等于比较

4. **符号操作**
   - FSGNJ.D, FSGNJN.D, FSGNJX.D - 符号注入

5. **分类指令**
   - FCLASS.D - 浮点数分类

6. **转换指令**
   - FCVT.W.D, FCVT.WU.D - 双精度转32位整数
   - FCVT.L.D, FCVT.LU.D - 双精度转64位整数
   - FCVT.D.W, FCVT.D.WU - 32位整数转双精度
   - FCVT.D.L, FCVT.D.LU - 64位整数转双精度
   - FCVT.S.D, FCVT.D.S - 单精度与双精度互转

7. **移动指令**
   - FMV.X.D, FMV.D.X - 双精度寄存器与整数寄存器间移动

8. **加载/存储**
   - FLD, FSD - 64位浮点加载/存储

## 实现计划（更新版）

### 第一阶段：基础设施升级 ✅ COMPLETED
> 已完成所有基础设施升级，包括64位浮点寄存器、Funct7常量定义、简化CSR支持

### 第二阶段：解码器增强 ✅ COMPLETED  
> 已完成解码器扩展，支持F/D指令解码和R4型指令格式

### 第三阶段：执行器实现 ✅ COMPLETED
**当前状态**：所有RV64F和RV64D指令都已实现

**已完成**：
- ✅ 基本算术运算：FADD.S, FSUB.S, FMUL.S, FDIV.S, FSQRT.S
- ✅ 比较指令：FEQ.S, FLT.S, FLE.S  
- ✅ 符号操作：FSGNJ.S, FSGNJN.S, FSGNJX.S
- ✅ 最小值/最大值：FMIN.S, FMAX.S
- ✅ 转换指令：FCVT.W.S, FCVT.WU.S, FCVT.L.S, FCVT.LU.S, FCVT.S.W, FCVT.S.WU, FCVT.S.L, FCVT.S.LU
- ✅ 移动指令：FMV.X.W, FMV.W.X
- ✅ 分类指令：FCLASS.S
- ✅ 融合乘加指令：FMADD.S, FMSUB.S, FNMSUB.S, FNMADD.S
- ✅ 双精度算术运算：FADD.D, FSUB.D, FMUL.D, FDIV.D, FSQRT.D
- ✅ 双精度比较指令：FEQ.D, FLT.D, FLE.D
- ✅ 双精度符号操作：FSGNJ.D, FSGNJN.D, FSGNJX.D
- ✅ 双精度最小值/最大值：FMIN.D, FMAX.D
- ✅ 双精度转换指令：所有FCVT.*.D和FCVT.D.*指令
- ✅ 双精度移动指令：FMV.X.D, FMV.D.X
- ✅ 双精度分类指令：FCLASS.D
- ✅ 双精度融合乘加：FMADD.D, FMSUB.D, FNMSUB.D, FNMADD.D
- ✅ 精度转换：FCVT.S.D, FCVT.D.S

### 第四阶段：内存系统支持 ✅ COMPLETED
**当前状态**：所有浮点加载/存储指令都已完成实现

**已完成**：
1. **✅ 扩展InstructionExecutor类**：
   - 添加了浮点内存操作方法：`loadFloatFromMemory()`, `loadDoubleFromMemory()`
   - 添加了浮点存储方法：`storeFloatToMemory()`, `storeDoubleToMemory()`
   - 实现了内存对齐检查（FLW/FSW: 4字节对齐，FLD/FSD: 8字节对齐）

2. **✅ 更新现有内存操作**：
   - 在`loadFromMemory()`中集成FLW/FLD支持（合并相同funct3值）
   - 在`storeToMemory()`中集成FSW/FSD支持（合并相同funct3值）
   - 验证Memory类已支持所需的32位和64位操作方法

3. **✅ 处理指令编码重复**：
   - 正确处理LW/FLW和LD/FLD共享相同funct3值的情况
   - 正确处理SW/FSW和SD/FSD共享相同funct3值的情况
   - 统一使用底层内存操作，CPU层负责解释数据类型

### 第五阶段：顺序CPU集成 ✅ COMPLETED
**当前状态**：顺序CPU已完全集成所有浮点指令支持

**已完成**：
1. **✅ 添加R4_TYPE指令类型支持**：
   - 在step()方法中添加了R4_TYPE指令的执行分支
   - 实现了executeR4Type()方法来处理融合乘加指令

2. **✅ 重写浮点指令执行方法**：
   - 完全重构了executeFPExtension()方法，支持单精度和双精度自动识别
   - 新增executeFPExtensionDouble()方法专门处理双精度浮点指令
   - 正确处理不同指令类型的结果存储（整数寄存器 vs 浮点寄存器）

3. **✅ 实现融合乘加指令支持**：
   - 新增executeFusedMultiplyAdd()方法
   - 支持单精度和双精度融合乘加指令的自动识别和执行
   - 处理FMADD、FMSUB、FNMSUB、FNMADD四种融合乘加操作

4. **✅ 集成浮点内存操作**：
   - 扩展executeLoadOperations()方法支持FLW/FLD浮点加载指令
   - 扩展executeSType()方法支持FSW/FSD浮点存储指令
   - 新增executeFPLoadOperations()和executeFPStoreOperations()专用方法

5. **✅ 更新CPU扩展配置**：
   - 在CPU构造函数中启用F和D扩展
   - 确保所有浮点指令都能被正确解码和执行

6. **✅ 方法声明和实现完整性**：
   - 在头文件中添加了所有新方法的声明
   - 所有方法都有完整的实现并通过编译验证

### 第六阶段：乱序CPU集成 ⏳ PENDING
1. **OOO CPU修改**：
   - 更新动态指令结构
   - 修改执行单元
   - 确保乱序执行的正确性

### 第七阶段：测试和验证 ⏳ PENDING
1. **单元测试**：
   - 针对每类指令编写测试
   - 验证IEEE 754兼容性
2. **集成测试**：
   - 运行`rv64uf-p-*`测试套件
   - 运行`rv64ud-p-*`测试套件
   - 确保所有测试通过

## 下一步行动计划 🎯

### 立即需要完成（第三阶段剩余部分）：
1. **添加融合乘加指令执行方法**：
   - 在`include/core/instruction_executor.h`中添加`executeFusedMultiplyAdd()`方法声明
   - 在`src/core/instruction_executor.cpp`中实现单精度和双精度融合乘加
   
2. **实现FMV.W.X指令**：
   - 整数寄存器到浮点寄存器的位模式移动

3. **完整实现双精度指令执行**：
   - 添加`executeFPExtensionDouble()`方法
   - 实现所有RV64D指令的执行逻辑

### 中期目标（第四、五阶段）：
1. **实现浮点加载/存储指令**
2. **集成到顺序CPU中**
3. **添加乱序CPU支持**

### 最终目标（第七阶段）：
1. **运行测试验证**：`python3 run_tests.py --ooo -p "rv64uf-p-*" -w 8`

## 文件修改状态

### 已修改文件 ✅
1. **✅ `include/common/types.h`** - 完成F/D扩展常量和类型定义
   - 添加了完整的Funct7枚举（40+个浮点指令常量）
   - 添加了R4_TYPE指令类型  
   - 添加了融合乘加操作码：FMADD, FMSUB, FNMSUB, FNMADD
   - 添加了浮点相关Funct3常量
   - 添加了CSR地址常量命名空间

2. **✅ `include/common/cpu_interface.h`** - 添加浮点和CSR接口
   - 添加了双精度浮点寄存器访问方法
   - 添加了CSR寄存器访问接口

3. **✅ `include/cpu/inorder/cpu.h`** - 扩展浮点寄存器和CSR
   - 将浮点寄存器数组改为64位
   - 添加了双精度和CSR访问方法声明

4. **✅ `src/cpu/inorder/cpu.cpp`** - 实现浮点指令执行逻辑  
   - 实现了64位浮点寄存器访问
   - 实现了简化CSR支持

5. **✅ `include/cpu/ooo/cpu_state.h`** - 更新乱序CPU状态
   - 将架构和物理浮点寄存器都改为64位

6. **✅ `include/cpu/ooo/ooo_cpu.h`** - 添加乱序CPU浮点和CSR接口

7. **✅ `src/cpu/ooo/ooo_cpu.cpp`** - 更新乱序执行逻辑
   - 实现了64位浮点寄存器访问和简化CSR支持

8. **✅ `src/cpu/cpu_factory.cpp`** - 更新工厂类适配器
   - 添加了双精度和CSR方法转发

9. **✅ `src/core/decoder.cpp`** - 扩展浮点指令解码
   - 添加了R4型指令支持
   - 完善了F/D扩展验证逻辑

10. **✅ `src/core/instruction_executor.cpp`** - 完成所有浮点指令和内存操作实现
    - ✅ 实现了所有RV64F单精度指令
    - ✅ 实现了所有RV64D双精度指令
    - ✅ 实现了融合乘加指令执行
    - ✅ 实现了FMV.W.X指令
    - ✅ 解决了Funct7枚举冲突问题（FMV_X_W/FCLASS_S, FMV_X_D/FCLASS_D）
    - ✅ 添加了浮点内存操作方法：`loadFloatFromMemory()`, `loadDoubleFromMemory()`, `storeFloatToMemory()`, `storeDoubleToMemory()`
    - ✅ 更新了`loadFromMemory()`和`storeToMemory()`以支持FLW/FLD/FSW/FSD指令

11. **✅ `include/core/instruction_executor.h`** - 完成浮点执行和内存方法声明
    - ✅ 添加了双精度浮点指令执行方法：`executeFPExtensionDouble()`
    - ✅ 添加了融合乘加方法：`executeFusedMultiplyAddSingle()`, `executeFusedMultiplyAddDouble()`
    - ✅ 添加了双精度浮点辅助方法：`uint64ToDouble()`, `doubleToUint64()`
    - ✅ 添加了浮点内存操作方法声明：`loadFloatFromMemory()`, `loadDoubleFromMemory()`, `storeFloatToMemory()`, `storeDoubleToMemory()`

12. **✅ `tests/test_syscall_handler.cpp`** - 更新测试占位符
    - 添加了双精度和CSR测试占位符实现

13. **✅ `tests/test_instruction_executor.cpp`** - 修复测试用例
    - 更新了测试用例以使用新的Funct7枚举

14. **✅ `include/cpu/inorder/cpu.h`** - 扩展顺序CPU方法声明（第五阶段）
    - ✅ 添加了executeR4Type()方法声明
    - ✅ 添加了executeFPExtensionDouble()方法声明
    - ✅ 添加了executeFusedMultiplyAdd()方法声明
    - ✅ 添加了executeFPLoadOperations()和executeFPStoreOperations()方法声明

15. **✅ `src/cpu/inorder/cpu.cpp`** - 完成顺序CPU浮点指令集成（第五阶段）
    - ✅ 在step()方法中添加了R4_TYPE指令执行支持
    - ✅ 重构了executeFPExtension()方法，支持单精度和双精度自动识别
    - ✅ 新增executeFPExtensionDouble()方法处理双精度浮点指令
    - ✅ 新增executeR4Type()和executeFusedMultiplyAdd()方法处理融合乘加指令
    - ✅ 扩展executeLoadOperations()和executeSType()方法支持浮点内存操作
    - ✅ 新增executeFPLoadOperations()和executeFPStoreOperations()专用方法
    - ✅ 在构造函数中启用F和D扩展

### 待修改文件 ⏳
1. **⏳ 内存系统文件（待确定）** - 支持浮点加载/存储
   - 可能需要修改内存访问相关代码

### 编译状态 ✅
- **✅ 当前代码可以成功编译**：所有修改都已通过编译验证
- **⚠️ 存在少量编译警告**：关于移位操作的警告，不影响功能

## 预期挑战（简化版）

1. **基本IEEE 754兼容性**：依赖C++标准库确保基本兼容性
2. **寄存器文件扩展**：确保32位和64位访问的正确性
3. **指令解码完整性**：正确识别所有F/D扩展指令
4. **内存对齐**：浮点加载/存储的对齐要求
5. **测试覆盖**：确保所有指令类型都被正确实现

## 估计工作量（简化版）

- 总计：约8-12个工作日（相比原计划减少40%）
- 第一阶段：1-2天
- 第二阶段：1天
- 第三阶段：3-4天（大幅简化）
- 第四阶段：1天
- 第五阶段：1-2天
- 第六阶段：1-2天
- 第七阶段：1-2天

通过简化实现，我们可以快速支持浮点扩展，满足CoreMark和SPEC CPU 2006的需求。