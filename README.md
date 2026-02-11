# RISC-V CPU 模拟器

[![CI](https://github.com/jensen-yan/risc-v-simulator/actions/workflows/ci.yml/badge.svg)](https://github.com/jensen-yan/risc-v-simulator/actions/workflows/ci.yml)

一个模块化的 RISC-V CPU 模拟器，已从 RV32I 推进到 RV64I 基础指令集，并具备乱序（OOO）与顺序两种执行模式。

## 功能特性

- RV64I 基础指令集（含 RV32I 子集），支持部分扩展：M（乘除）、C（压缩）。
- 32 个通用寄存器（x0–x31），64 位寄存器/地址宽度。
- 内存可配置（默认 1MB）；示例中为 riscv-tests 配置 2GB。
- 顺序（In-Order）与乱序（Out-of-Order，默认）CPU 实现。
- 单步与连续运行、详尽的调试分类与周期过滤、状态转储。
- 基础系统调用/tohost 机制支持。

## 构建说明

```bash
mkdir build && cd build
cmake .. && make -j

# Debug 构建
cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j

# 覆盖率构建（也可使用脚本）
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON .. && make -j
```

## 使用方法

```bash
./risc-v-sim [选项] [程序文件]

常用示例：

# 运行 ELF，使用默认 OOO CPU 与 2GB 内存（适配 riscv-tests）
./risc-v-sim -e -m 2164260864 ./riscv-tests/isa/rv32ui-p-add

# 指定顺序执行 CPU
./risc-v-sim -e --in-order ./riscv-tests/isa/rv32ui-p-add

# 调试预设/分类与周期过滤
./risc-v-sim --debug --debug-preset=ooo -e program.elf
./risc-v-sim --debug-categories=fetch,decode,commit --debug-cycles=100-200 -e program.elf
```

## 项目结构

```
├── src/            # 源代码（core/、cpu/{inorder,ooo}、system/ 等）
├── include/        # 头文件（与 src/ 结构镜像）
├── tests/          # GoogleTest 单元测试
├── riscv-tests/    # 上游 riscv-tests（子模块）
├── runtime/        # 运行时库（含 minilibc）
├── programs/       # 示例/测试程序
├── docs/           # 架构/需求等稳定文档
└── tasks/          # 任务说明、重构计划与执行记录（新增任务文档统一放此目录）
```

## 开发原则（DRY / KISS）

为避免 InOrder/OOO 出现两套语义实现并长期分叉，后续开发默认遵循以下约束：

- 指令语义统一下沉到 `src/core/instruction_executor.cpp`（及对应头文件）。
- `src/cpu/inorder/` 与 `src/cpu/ooo/` 只处理流水线/调度/提交/异常传播等控制流逻辑。
- 禁止在 InOrder 与 OOO 中重复实现同一条指令的算术、比较、访存宽度、CSR 位语义。
- 新增 ISA 能力时，优先扩展 `InstructionExecutor`，然后让 InOrder/OOO 复用同一语义入口。

建议在提交前自检：

- 该指令的语义是否只在 `InstructionExecutor` 出现一次。
- InOrder 与 OOO 是否都调用了同一语义实现。
- 是否补充了对应单测与 `run_tests.py` 回归。

## 文档

- 架构设计（精简）：`docs/代码架构文档.md`
- 需求与范围（精简）：`docs/requirements.md`
- 项目计划（精简）：`docs/plans.md`
- 任务与重构记录：`tasks/`

## 支持的指令

支持 RV64I（含 RV32I 子集）与 M/C 扩展的常用子集；具体覆盖以测试集与源码实现为准。

## 构建 riscv-tests

```bash
git submodule update --init --recursive

# 工具链（Ubuntu 22.04 示例）
sudo apt install gcc-riscv64-unknown-elf picolibc-riscv64-unknown-elf
# 导出环境（如需）
source ./setup_riscv_env.sh

cd riscv-tests

# see README.md in riscv-tests
autoconf
./configure
make -j 

```

## 测试与覆盖率

依赖：`libgtest-dev`（或系统提供的 GTest）。

```bash
# 运行单元测试（构建后）
ctest --test-dir build --output-on-failure

# 并行运行 riscv-tests（示例：基础整数 + OOO）
python3 run_tests.py -p "rv32ui-p-*" --ooo -w 8

# 其他模式示例
python3 run_tests.py -p "rv32um-p-*"       # 乘除法
python3 run_tests.py -p "rv32uc-p-*"       # 压缩指令

# 覆盖率（生成 HTML 报告）
sudo apt install lcov
./run_coverage.sh
```

提示：运行 riscv-tests 时常用 `-m 2164260864`（2GB）确保地址范围充足；默认内存为 1MB，可按需调整。

## 性能基准（CoreMark + Embench-IoT + 现有子项）

已新增统一 benchmark 工作流，详情见 `benchmarks/README.md`。快速流程：

```bash
# 1) 拉取外部源码
./tools/benchmarks/fetch_external_benchmarks.sh

# 2) 构建 CoreMark（裸机）
./tools/benchmarks/build_coremark.sh

# 3) 构建 Embench-IoT（或导入已有产物）
./tools/benchmarks/build_embench_iot.sh
# ./tools/benchmarks/import_embench_binaries.sh --src <embench_build_dir>

# 4) 统一跑分（in-order + ooo）并导出 CSV/JSON
python3 ./tools/benchmarks/run_perf_suite.py \
  --manifest benchmarks/manifest/default.json \
  --cpu-mode both \
  --max-instructions 50000000 \
  --max-ooo-cycles 500000 \
  --output-dir benchmarks/results/latest
```

## RISCOF 架构测试（DUT vs Spike）

仓库已提供最小可用配置，目录：`tools/riscof/`。

```bash
# 1) 激活环境（如遇网络问题先 proxyon）
source ~/.zshrc
proxyon
source .venv-riscof/bin/activate

# 2) 生成测试列表
cd tools/riscof
riscof testlist \
  --config ./config.ini \
  --suite ../../riscv-arch-test/riscv-test-suite \
  --env ../../riscv-arch-test/riscv-test-suite/env \
  --work-dir ../../build/riscof-work

# 2.5) 生成 smoke testfile（add/and/sub）
python - <<'PY'
import yaml
from pathlib import Path
src = Path("../../build/riscof-work/test_list.yaml")
dst = Path("../../build/riscof-work/test_list_smoke.yaml")
data = yaml.safe_load(src.read_text())
want = ("rv64i_m/I/src/add-01.S", "rv64i_m/I/src/and-01.S", "rv64i_m/I/src/sub-01.S")
out = {k: v for k, v in data.items() if any(w in k for w in want)}
dst.write_text(yaml.safe_dump(out, sort_keys=False))
print(f"wrote {len(out)} tests -> {dst}")
PY

# 3) 运行对比（先 smoke）
riscof validateyaml --config ./config.ini --work-dir ../../build/riscof-work-smoke
riscof run \
  --config ./config.ini \
  --suite ../../riscv-arch-test/riscv-test-suite \
  --env ../../riscv-arch-test/riscv-test-suite/env \
  --work-dir ../../build/riscof-work-smoke \
  --testfile ../../build/riscof-work/test_list_smoke.yaml \
  --no-browser
```

详细说明见：`tools/riscof/README.md`
