#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EMBENCH_DIR="${ROOT_DIR}/benchmarks/external/embench-iot"
RAW_BUILD_DIR="${ROOT_DIR}/benchmarks/build/embench-raw"
OUT_DIR="${ROOT_DIR}/benchmarks/build/embench-iot"
LOG_DIR="${ROOT_DIR}/benchmarks/results/embench-buildlog"

ARCH="riscv32"
CHIP="generic"
BOARD="ri5cyverilator"
CC="${CC:-riscv64-unknown-elf-gcc}"
LD="${LD:-riscv64-unknown-elf-gcc}"
TIMEOUT="120"
CLEAN=0

usage() {
  cat <<EOF
用法: $0 [选项]

选项:
  --embench-dir <dir>   Embench-IoT 源码目录（默认: ${EMBENCH_DIR}）
  --raw-build-dir <dir> Embench 原始构建目录（默认: ${RAW_BUILD_DIR}）
  --out-dir <dir>       归档后 ELF 输出目录（默认: ${OUT_DIR}）
  --log-dir <dir>       构建日志目录（默认: ${LOG_DIR}）
  --arch <name>         Embench 架构（默认: ${ARCH}）
  --chip <name>         Embench 芯片配置（默认: ${CHIP}）
  --board <name>        Embench 板级配置（默认: ${BOARD}）
  --cc <path>           C 编译器（默认: ${CC}）
  --ld <path>           链接器（默认: ${LD}）
  --timeout <sec>       编译/链接超时（默认: ${TIMEOUT}）
  --clean               清理后重建
  -h, --help            显示帮助

说明:
  1) 先运行 Embench 官方 build_all.py
  2) 将成功生成的 benchmark 可执行文件复制为 *.elf 到 out-dir
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --embench-dir)
      EMBENCH_DIR="$2"
      shift 2
      ;;
    --raw-build-dir)
      RAW_BUILD_DIR="$2"
      shift 2
      ;;
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --log-dir)
      LOG_DIR="$2"
      shift 2
      ;;
    --arch)
      ARCH="$2"
      shift 2
      ;;
    --chip)
      CHIP="$2"
      shift 2
      ;;
    --board)
      BOARD="$2"
      shift 2
      ;;
    --cc)
      CC="$2"
      shift 2
      ;;
    --ld)
      LD="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    --clean)
      CLEAN=1
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

if [[ ! -d "${EMBENCH_DIR}" ]]; then
  echo "[ERROR] Embench 源码目录不存在: ${EMBENCH_DIR}" >&2
  echo "请先执行: tools/benchmarks/fetch_external_benchmarks.sh --embench-only" >&2
  exit 1
fi

mkdir -p "${RAW_BUILD_DIR}" "${OUT_DIR}" "${LOG_DIR}"

BUILD_CMD=(
  python3
  "${EMBENCH_DIR}/build_all.py"
  --arch "${ARCH}"
  --chip "${CHIP}"
  --board "${BOARD}"
  --cc "${CC}"
  --ld "${LD}"
  --cc-output-pattern "-c -o {0}"
  --builddir "${RAW_BUILD_DIR}"
  --logdir "${LOG_DIR}"
  --timeout "${TIMEOUT}"
)

if [[ "${CLEAN}" -eq 1 ]]; then
  BUILD_CMD+=(--clean)
fi

echo "[INFO] 执行 Embench 构建"
"${BUILD_CMD[@]}"

echo "[INFO] 整理成功产物到 ${OUT_DIR}"
count=0
while IFS= read -r -d '' file; do
  name="$(basename "${file}")"
  cp "${file}" "${OUT_DIR}/${name}.elf"
  count=$((count + 1))
done < <(find "${RAW_BUILD_DIR}/src" -mindepth 2 -maxdepth 2 -type f -perm -111 -print0)

if [[ "${count}" -eq 0 ]]; then
  echo "[ERROR] 未发现可执行 benchmark 产物" >&2
  exit 1
fi

latest_log="$(ls -1t "${LOG_DIR}"/build-*.log 2>/dev/null | head -n 1 || true)"
if [[ -n "${latest_log}" ]]; then
  warn_count="$(rg -c "Warning: Link of benchmark" "${latest_log}" || true)"
  if [[ "${warn_count}" != "0" ]]; then
    echo "[WARN] 有 ${warn_count} 个 benchmark 链接失败，详见日志: ${latest_log}"
  fi
fi

echo "[DONE] Embench 构建完成，可用 benchmark 数量: ${count}"
echo "       输出目录: ${OUT_DIR}"
