# Repository Guidelines

注意默认都使用中文回复我！
画图尽量用mermaid 画图
commit message 尽量详细点，用中文

## Project Structure & Modules
- `src/`: Core simulator sources (CPU, memory, decoder, system, stages).
- `include/`: Public headers mirroring `src/` layout.
- `tests/`: GoogleTest unit tests (e.g., `test_memory.cpp`).
- `riscv-tests/`: Upstream test suite (submodule). Build separately.
- `programs/` + `runtime/`: Example programs and minimal libc runtime.
- `build/`: CMake build output (not committed).
- `docs/`, `utils/`, scripts: Helper docs and tooling.

## Build, Test, and Development Commands
- Configure & build (Release):
  `mkdir -p build && cd build && cmake .. && make -j`
- Debug build:
  `cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j`
- Run simulator:
  `./build/risc-v-sim --help`
  Example: `./build/risc-v-sim -e -m 2164260864 ./riscv-tests/isa/rv32ui-p-add`
- Unit tests (if GTest available):
  `ctest --test-dir build` or `./build/risc-v-tests`
- riscv-tests harness:
  `python3 run_tests.py -p "rv32ui-p-*" [--ooo] [-w 8]`
- Coverage:
  `./run_coverage.sh` (requires `lcov`).
- Submodules & toolchain (first-time setup):
  `git submodule update --init --recursive && source ./setup_riscv_env.sh`

## Coding Style & Naming
- Language: C++17; 4-space indentation; no tabs.
- Files: `snake_case.cpp/.h` under `src/` and `include/`.
- Types: `PascalCase`; functions/methods: `lowerCamelCase`;
  constants/macros: `UPPER_SNAKE_CASE`; namespaces: lowercase.
- Prefer `fmt::format` for formatting; group includes: std, third-party, project.
- Keep headers minimal; avoid cyclic deps; mirror folder structure between `src/` and `include/`.

## Testing Guidelines
- Framework: GoogleTest in `tests/` (files `test_*.cpp`).
- Write unit tests for new behavior; cover edge cases and error paths.
- Quick check: `ctest --output-on-failure`.
- ISA conformance: run `run_tests.py` with a focused pattern (e.g., `-p "rv32um-p-*"`). Include command and summary in your PR.

## Commit & Pull Request Guidelines
- Commits: Prefer Conventional Commits (e.g., `feat:`, `fix:`, `refactor:`). Keep messages clear (EN/中文均可)。
- PRs: scope narrowly; link issues; describe changes, rationale, and testing.
- Attach evidence: failing/passing test logs or coverage deltas; note any new flags or scripts.
- Checklist: builds cleanly; `ctest` passes; targeted `run_tests.py` passes; docs updated.

## Security & Configuration Tips
- Do not commit binaries or `build/` output. Use `setup_riscv_env.sh` to export toolchain paths.
- When touching ELF/loading paths, validate with `-e` and both in-order/OOO modes.
