#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SIMULATOR="${ROOT_DIR}/build/risc-v-sim"
OUTPUT_DIR="${ROOT_DIR}/benchmarks/results/memory-learning"
MANIFEST_REL="benchmarks/manifest/lsu_foundation.json"
CPU_MODE="ooo"
MAX_INSTRUCTIONS=0
MAX_OOO_CYCLES=600000
MEMORY_SIZE=2164260864
TIMEOUT=60
FILTER="*"
BUILD_LSU=1
PHASE="lsu-foundation"

usage() {
  cat <<EOF
用法: $0 [选项]

选项:
  --phase <name>             运行阶段：lsu-foundation | stream | full（默认: ${PHASE}）
  --cpu-mode <mode>          CPU 模式：in-order | ooo | both（默认: ${CPU_MODE}）
  --max-instructions <N>     In-order 指令上限（默认: ${MAX_INSTRUCTIONS}）
  --max-ooo-cycles <N>       OOO 周期上限（默认: ${MAX_OOO_CYCLES}）
  --memory-size <bytes>      模拟器内存大小（默认: ${MEMORY_SIZE}）
  --timeout <sec>            单个 benchmark 超时秒数（默认: ${TIMEOUT}）
  --output-dir <dir>         结果输出目录（默认: ${OUTPUT_DIR}）
  --filter <glob>            benchmark 名称过滤（默认: ${FILTER}）
  --no-build-lsu             跳过 LSU 微基准构建
  -h, --help                 显示帮助
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --phase)
      PHASE="$2"
      shift 2
      ;;
    --cpu-mode)
      CPU_MODE="$2"
      shift 2
      ;;
    --max-instructions)
      MAX_INSTRUCTIONS="$2"
      shift 2
      ;;
    --max-ooo-cycles)
      MAX_OOO_CYCLES="$2"
      shift 2
      ;;
    --memory-size)
      MEMORY_SIZE="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --filter)
      FILTER="$2"
      shift 2
      ;;
    --no-build-lsu)
      BUILD_LSU=0
      shift
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

if [[ ! -x "${SIMULATOR}" ]]; then
  echo "[ERROR] 模拟器不存在或不可执行: ${SIMULATOR}" >&2
  exit 1
fi

case "${PHASE}" in
  lsu-foundation)
    MANIFEST_REL="benchmarks/manifest/lsu_foundation.json"
    ;;
  stream)
    MANIFEST_REL="benchmarks/manifest/lsu_foundation.json"
    FILTER="lsu-microbench::stream_*"
    ;;
  full)
    MANIFEST_REL="benchmarks/manifest/memory_learning.json"
    ;;
  *)
    echo "[ERROR] 不支持的 phase: ${PHASE}" >&2
    usage
    exit 1
    ;;
esac

if [[ ${BUILD_LSU} -eq 1 ]]; then
  "${ROOT_DIR}/tools/benchmarks/build_lsu_microbench.sh"
fi

mkdir -p "${OUTPUT_DIR}"

python3 "${ROOT_DIR}/tools/benchmarks/run_perf_suite.py" \
  --manifest "${MANIFEST_REL}" \
  --simulator "build/risc-v-sim" \
  --cpu-mode "${CPU_MODE}" \
  --memory-size "${MEMORY_SIZE}" \
  --timeout "${TIMEOUT}" \
  --max-instructions "${MAX_INSTRUCTIONS}" \
  --max-ooo-cycles "${MAX_OOO_CYCLES}" \
  --output-dir "${OUTPUT_DIR}" \
  --filter "${FILTER}"
