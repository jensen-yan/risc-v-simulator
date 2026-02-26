# 仓库指南

注意默认都使用中文回复我！
画图尽量用mermaid 画图
commit message 尽量详细点，用中文
写完代码后，最好自己编译运行测试下，保证代码写的正确性！
保持KISS 原则和DRY 原则！

## Bug 修复原则（新增）
- 修复 bug 时，优先做“通用一致性”方案，不做仅覆盖单一现象的定点修复。
- 允许临时止血补丁，但必须明确标注“临时方案”，并尽快补齐通用机制重构。
- 涉及缓存/内存/并发可见性问题时，优先从机制层抽象（事件通知、统一失效、统一仲裁）入手，再落到具体场景。

## 项目结构与模块
- `src/`: 核心模拟器源码（CPU、内存、译码器、系统、流水线阶段）。
- `include/`: 公共头文件，目录结构与 `src/` 对应。
- `tests/`: GoogleTest 单测（如 `test_memory.cpp`）。
- `riscv-tests/`: 上游测试套件（子模块），需单独构建。
- `programs/` + `runtime/`: 示例程序与最小化 libc 运行时。
- `build/`: CMake 构建输出（不提交）。
- `docs/`、`utils/`、scripts: 辅助文档与工具。
- `tasks/`: 任务说明、重构计划与执行记录（以后新增任务文档统一放这里）。

## 构建、测试与开发命令
- 配置并构建（Release）:
  `mkdir -p build && cd build && cmake .. && make -j`
- Debug 构建:
  `cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j`
- 运行模拟器:
  `./build/risc-v-sim --help`
  示例: `./build/risc-v-sim -e -m 2164260864 ./riscv-tests/isa/rv32ui-p-add`
- 单元测试（若 GTest 可用）:
  `ctest --test-dir build` 或 `./build/risc-v-tests`
- riscv-tests 执行器:
  `python3 run_tests.py -p "rv32ui-p-*" [--ooo] [-w 8]`
- 覆盖率:
  `./run_coverage.sh`（需要 `lcov`）。
- 子模块与工具链（首次配置）:
  `git submodule update --init --recursive && source ./setup_riscv_env.sh`

## 编码风格与命名
- 语言: C++17；4 空格缩进；不使用 tab。
- 文件: `src/` 与 `include/` 下使用 `snake_case.cpp/.h`。
- 类型: `PascalCase`；函数/方法: `lowerCamelCase`；
  常量/宏: `UPPER_SNAKE_CASE`；命名空间: 小写。
- 优先使用 `fmt::format`；include 分组为 std、第三方、项目。
- 头文件保持最小化；避免循环依赖；`src/` 与 `include/` 目录结构镜像。

## 测试规范
- 框架: `tests/` 下的 GoogleTest（文件 `test_*.cpp`）。
- 新增行为要写单测；覆盖边界与错误路径。
- 快速检查: `ctest --output-on-failure`。
- ISA 一致性: 用聚焦模式运行 `run_tests.py`（如 `-p "rv32um-p-*"`），在 PR 中附上命令与结果摘要。

## 提交与 PR 规范
- Commit: 优先 Conventional Commits（如 `feat:`、`fix:`、`refactor:`），描述清晰（EN/中文均可）。
- PR: 范围聚焦；关联 issue；说明变更、动机与测试。
- 附带证据: 失败/通过的测试日志或覆盖率变化；注明新 flag 或脚本。
- 清单: 构建通过；`ctest` 通过；指定的 `run_tests.py` 通过；文档已更新。

## 安全与配置提示
- 不提交二进制或 `build/` 输出。使用 `setup_riscv_env.sh` 导出工具链路径。
- 涉及 ELF/加载路径时，用 `-e` 且在顺序/乱序模式下验证。
- 若遇到网络问题（如 `Could not resolve host`、`git clone`/`git push` 超时），先执行 `source ~/.zshrc && proxyon`，确保已设置 `http_proxy`/`https_proxy` 后再重试相关命令（含 `git clone`、`git push`、`git submodule update`）。
