# RISCOF 最小集成（本仓库）

该目录提供一个最小可用的 `RISCOF` 集成，用于把本模拟器作为 DUT，与 `spike_simple` 参考模型做签名比对。

## 前置条件

- 已构建模拟器：`./build/risc-v-sim`
- 已有工具链：`riscv64-unknown-elf-gcc`、`riscv64-unknown-elf-nm`
- 已安装 `RISCOF`（建议使用仓库根目录的 `.venv-riscof`）

## 一次性准备

在仓库根目录执行：

```bash
source ~/.zshrc
proxyon
source .venv-riscof/bin/activate
```

## 生成测试列表

```bash
cd tools/riscof
export PYTHONPATH=$PWD/plugins/riscv_sim:$PYTHONPATH
riscof testlist \
  --config ./config.ini \
  --suite ../../riscv-arch-test/riscv-test-suite \
  --env ../../riscv-arch-test/riscv-test-suite/env \
  --work-dir ../../build/riscof-work
```

生成文件：`../../build/riscof-work/test_list.yaml`

## 生成 smoke testfile（可选）

```bash
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
```

## 运行并与参考模型对比

全量测试会较慢，建议先用筛选后的 testlist。

```bash
cd tools/riscof
export PYTHONPATH=$PWD/plugins/riscv_sim:$PYTHONPATH
riscof validateyaml --config ./config.ini --work-dir ../../build/riscof-work
riscof run \
  --config ./config.ini \
  --suite ../../riscv-arch-test/riscv-test-suite \
  --env ../../riscv-arch-test/riscv-test-suite/env \
  --work-dir ../../build/riscof-work \
  --testfile ../../build/riscof-work/test_list_smoke.yaml \
  --no-browser
```

报告输出：`../../build/riscof-work/report.html`
