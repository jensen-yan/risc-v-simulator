#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${ROOT_DIR}/benchmarks/custom/lsu"
OUT_DIR="${ROOT_DIR}/benchmarks/build/lsu"
COMMON_DIR="${ROOT_DIR}/riscv-tests/benchmarks/common"
ENV_DIR="${ROOT_DIR}/riscv-tests/env"

RISCV_GCC="${RISCV_GCC:-riscv64-unknown-elf-gcc}"
RISCV_OBJDUMP="${RISCV_OBJDUMP:-riscv64-unknown-elf-objdump}"
RISCV_MARCH="${RISCV_MARCH:-rv64gc}"
RISCV_MABI="${RISCV_MABI:-lp64d}"

usage() {
  cat <<EOF
用法: $0 [选项]

选项:
  --src-dir <dir>         微基准源码目录（默认: ${SRC_DIR}）
  --out-dir <dir>         输出目录（默认: ${OUT_DIR}）
  --march <march>         目标架构（默认: ${RISCV_MARCH}）
  --mabi <mabi>           目标 ABI（默认: ${RISCV_MABI}）
  -h, --help              显示帮助
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --src-dir)
      SRC_DIR="$2"
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

for required in \
  "${COMMON_DIR}/crt.S" \
  "${COMMON_DIR}/syscalls.c" \
  "${COMMON_DIR}/test.ld" \
  "${COMMON_DIR}/util.h" \
  "${ENV_DIR}/encoding.h"; do
  if [[ ! -f "${required}" ]]; then
    echo "[ERROR] 缺少文件: ${required}" >&2
    exit 1
  fi
done

mkdir -p "${OUT_DIR}"

COMMON_CFLAGS=(
  -O2
  -static
  -std=gnu99
  -fno-common
  -fno-builtin-printf
  -fno-tree-loop-distribute-patterns
  -Wno-implicit-int
  -Wno-implicit-function-declaration
  -mcmodel=medany
  "-march=${RISCV_MARCH}"
  "-mabi=${RISCV_MABI}"
)

INCLUDE_FLAGS=(
  "-I${SRC_DIR}"
  "-I${COMMON_DIR}"
  "-I${ENV_DIR}"
)

shopt -s nullglob
sources=("${SRC_DIR}"/*.c)
if [[ ${#sources[@]} -eq 0 ]]; then
  echo "[ERROR] 未找到微基准源码: ${SRC_DIR}/*.c" >&2
  exit 1
fi

for src in "${sources[@]}"; do
  name="$(basename "${src}" .c)"
  output_elf="${OUT_DIR}/${name}.riscv"
  output_dump="${OUT_DIR}/${name}.riscv.dump"

  echo "[INFO] 构建 ${name}: ${output_elf}"
  "${RISCV_GCC}" \
    "${INCLUDE_FLAGS[@]}" \
    "${COMMON_CFLAGS[@]}" \
    -o "${output_elf}" \
    "${src}" \
    "${COMMON_DIR}/syscalls.c" \
    "${COMMON_DIR}/crt.S" \
    -nostdlib -nostartfiles -lm -lgcc \
    -T "${COMMON_DIR}/test.ld"

  "${RISCV_OBJDUMP}" --disassemble-all --disassemble-zeroes --section=.text --section=.data \
    "${output_elf}" > "${output_dump}"
done

echo "[DONE] LSU 微基准构建完成: ${OUT_DIR}"
