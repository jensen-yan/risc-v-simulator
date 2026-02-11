#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXTERNAL_DIR="${ROOT_DIR}/benchmarks/external"

COREMARK_URL="https://github.com/eembc/coremark.git"
EMBENCH_URL="https://github.com/embench/embench-iot.git"

COREMARK_REF="main"
EMBENCH_REF="master"
FETCH_COREMARK=1
FETCH_EMBENCH=1
NO_UPDATE=0

usage() {
  cat <<EOF
用法: $0 [选项]

选项:
  --coremark-only          仅拉取 CoreMark
  --embench-only           仅拉取 Embench-IoT
  --coremark-ref <ref>     CoreMark 分支/标签/commit（默认: main）
  --embench-ref <ref>      Embench-IoT 分支/标签/commit（默认: master）
  --no-update              若目录已存在则跳过更新
  -h, --help               显示帮助

输出目录:
  ${EXTERNAL_DIR}
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --coremark-only)
      FETCH_COREMARK=1
      FETCH_EMBENCH=0
      shift
      ;;
    --embench-only)
      FETCH_COREMARK=0
      FETCH_EMBENCH=1
      shift
      ;;
    --coremark-ref)
      COREMARK_REF="$2"
      shift 2
      ;;
    --embench-ref)
      EMBENCH_REF="$2"
      shift 2
      ;;
    --no-update)
      NO_UPDATE=1
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

mkdir -p "${EXTERNAL_DIR}"

clone_or_update() {
  local repo_url="$1"
  local repo_ref="$2"
  local target_dir="$3"

  if [[ -d "${target_dir}/.git" ]]; then
    if [[ "${NO_UPDATE}" -eq 1 ]]; then
      echo "[SKIP] 已存在且 --no-update: ${target_dir}"
      return
    fi
    echo "[INFO] 更新仓库: ${target_dir}"
    git -C "${target_dir}" fetch --all --tags --prune
    git -C "${target_dir}" checkout "${repo_ref}"
    git -C "${target_dir}" pull --ff-only
  else
    echo "[INFO] 克隆仓库: ${repo_url} -> ${target_dir}"
    if ! git clone --depth 1 --branch "${repo_ref}" "${repo_url}" "${target_dir}"; then
      echo "[WARN] 分支/标签 ${repo_ref} 浅克隆失败，尝试完整克隆后切换"
      git clone "${repo_url}" "${target_dir}"
      if ! git -C "${target_dir}" checkout "${repo_ref}"; then
        echo "[ERROR] checkout 失败: ${repo_ref}" >&2
        echo "[INFO] 可用分支如下：" >&2
        git -C "${target_dir}" branch -a >&2 || true
        exit 1
      fi
    fi
  fi
}

if [[ "${FETCH_COREMARK}" -eq 1 ]]; then
  clone_or_update "${COREMARK_URL}" "${COREMARK_REF}" "${EXTERNAL_DIR}/coremark"
fi

if [[ "${FETCH_EMBENCH}" -eq 1 ]]; then
  clone_or_update "${EMBENCH_URL}" "${EMBENCH_REF}" "${EXTERNAL_DIR}/embench-iot"
fi

echo "[DONE] 外部 benchmark 源码已准备完成"
