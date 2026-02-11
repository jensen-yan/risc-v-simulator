#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR=""
OUT_DIR="${ROOT_DIR}/benchmarks/build/embench-iot"

usage() {
  cat <<EOF
用法: $0 --src <embench_build_dir> [--out <dir>]

说明:
  将 embench-iot 构建产物中的 .elf/.riscv 统一复制到本仓库，
  供 tools/benchmarks/run_perf_suite.py 通过 manifest 统一执行。

选项:
  --src <dir>    embench 产物目录（必填）
  --out <dir>    导入输出目录（默认: ${OUT_DIR}）
  -h, --help     显示帮助
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --src)
      SRC_DIR="$2"
      shift 2
      ;;
    --out)
      OUT_DIR="$2"
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

if [[ -z "${SRC_DIR}" ]]; then
  echo "[ERROR] 缺少 --src 参数" >&2
  usage
  exit 1
fi

if [[ ! -d "${SRC_DIR}" ]]; then
  echo "[ERROR] 源目录不存在: ${SRC_DIR}" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

count=0
while IFS= read -r -d '' file; do
  rel="${file#${SRC_DIR}/}"
  safe_name="${rel//\//__}"
  cp "${file}" "${OUT_DIR}/${safe_name}"
  count=$((count + 1))
done < <(find "${SRC_DIR}" -type f \( -name "*.elf" -o -name "*.riscv" \) -print0)

echo "[DONE] 导入完成: ${count} 个文件"
echo "       输出目录: ${OUT_DIR}"
