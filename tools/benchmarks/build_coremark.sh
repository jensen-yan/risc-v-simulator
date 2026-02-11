#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COREMARK_DIR="${ROOT_DIR}/benchmarks/external/coremark"
OUT_DIR="${ROOT_DIR}/benchmarks/build/coremark"
PORT_DIR="${ROOT_DIR}/tools/benchmarks/coremark_port"
COMMON_DIR="${ROOT_DIR}/riscv-tests/benchmarks/common"
ENV_DIR="${ROOT_DIR}/riscv-tests/env"

RISCV_GCC="${RISCV_GCC:-riscv64-unknown-elf-gcc}"
RISCV_OBJDUMP="${RISCV_OBJDUMP:-riscv64-unknown-elf-objdump}"
RISCV_MARCH="${RISCV_MARCH:-rv64gc}"
RISCV_MABI="${RISCV_MABI:-lp64d}"
ITERATIONS="${ITERATIONS:-2000}"

usage() {
  cat <<EOF
用法: $0 [选项]

选项:
  --coremark-dir <dir>    CoreMark 源码目录（默认: ${COREMARK_DIR}）
  --out-dir <dir>         输出目录（默认: ${OUT_DIR}）
  --march <march>         目标架构（默认: ${RISCV_MARCH}）
  --mabi <mabi>           目标 ABI（默认: ${RISCV_MABI}）
  --iterations <n>        CoreMark ITERATIONS（默认: ${ITERATIONS}）
  -h, --help              显示帮助

依赖:
  - 已拉取 CoreMark 源码（tools/benchmarks/fetch_external_benchmarks.sh）
  - riscv64-unknown-elf-gcc 工具链
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --coremark-dir)
      COREMARK_DIR="$2"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --march)
      RISCV_MARCH="$2"
      shift 2
      ;;
    --mabi)
      RISCV_MABI="$2"
      shift 2
      ;;
    --iterations)
      ITERATIONS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "未知参数: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ ! -d "${COREMARK_DIR}" ]]; then
  echo "[ERROR] CoreMark 源码目录不存在: ${COREMARK_DIR}" >&2
  echo "请先执行: tools/benchmarks/fetch_external_benchmarks.sh --coremark-only" >&2
  exit 1
fi

for required in \
  "${COREMARK_DIR}/core_main.c" \
  "${COREMARK_DIR}/core_list_join.c" \
  "${COREMARK_DIR}/core_matrix.c" \
  "${COREMARK_DIR}/core_state.c" \
  "${COREMARK_DIR}/core_util.c" \
  "${PORT_DIR}/core_portme.c" \
  "${PORT_DIR}/core_portme.h" \
  "${COMMON_DIR}/crt.S" \
  "${COMMON_DIR}/syscalls.c" \
  "${COMMON_DIR}/test.ld" \
  "${ENV_DIR}/encoding.h"; do
  if [[ ! -f "${required}" ]]; then
    echo "[ERROR] 缺少文件: ${required}" >&2
    exit 1
  fi
done

mkdir -p "${OUT_DIR}"

OUTPUT_ELF="${OUT_DIR}/coremark.riscv"
OUTPUT_DUMP="${OUT_DIR}/coremark.riscv.dump"

COMMON_CFLAGS=(
  -O2
  -static
  -std=gnu99
  -fno-common
  -fno-builtin-printf
  -fno-tree-loop-distribute-patterns
  -Wno-implicit-int
  -Wno-implicit-function-declaration
  -DPERFORMANCE_RUN=1
  -DVALIDATION_RUN=0
  -DPROFILE_RUN=0
  -DMEM_METHOD=MEM_STATIC
  "-DITERATIONS=${ITERATIONS}"
  -mcmodel=medany
  "-march=${RISCV_MARCH}"
  "-mabi=${RISCV_MABI}"
)

INCLUDE_FLAGS=(
  "-I${PORT_DIR}"
  "-I${COREMARK_DIR}"
  "-I${COMMON_DIR}"
  "-I${ENV_DIR}"
)

SRC_FILES=(
  "${COREMARK_DIR}/core_main.c"
  "${COREMARK_DIR}/core_list_join.c"
  "${COREMARK_DIR}/core_matrix.c"
  "${COREMARK_DIR}/core_state.c"
  "${COREMARK_DIR}/core_util.c"
  "${PORT_DIR}/core_portme.c"
  "${COMMON_DIR}/syscalls.c"
  "${COMMON_DIR}/crt.S"
)

echo "[INFO] 构建 CoreMark: ${OUTPUT_ELF}"
"${RISCV_GCC}" \
  "${INCLUDE_FLAGS[@]}" \
  "${COMMON_CFLAGS[@]}" \
  -o "${OUTPUT_ELF}" \
  "${SRC_FILES[@]}" \
  -nostdlib -nostartfiles -lm -lgcc \
  -T "${COMMON_DIR}/test.ld"

echo "[INFO] 生成反汇编: ${OUTPUT_DUMP}"
"${RISCV_OBJDUMP}" --disassemble-all --disassemble-zeroes --section=.text --section=.data \
  "${OUTPUT_ELF}" > "${OUTPUT_DUMP}"

echo "[DONE] CoreMark 构建完成"
echo "       ELF : ${OUTPUT_ELF}"
echo "       DUMP: ${OUTPUT_DUMP}"
