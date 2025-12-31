#!/bin/bash

# 代码覆盖率测试脚本

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_message() {
    local level="$1"; shift
    local message="$*"
    local prefix="[$level][COVERAGE]"
    local color="$NC"

    case "$level" in
        INFO) color="$GREEN" ;;
        WARN) color="$YELLOW" ;;
        ERROR) color="$RED" ;;
        DEBUG) color="$NC" ;;
        *) color="$NC" ;;
    esac

    if [[ -n "$NO_COLOR" ]]; then
        printf '%s %s\n' "$prefix" "$message"
    else
        printf '%b%s%b %s\n' "${color}" "$prefix" "${NC}" "$message"
    fi
}

detect_cores() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif [[ "$OSTYPE" == darwin* ]]; then
        sysctl -n hw.ncpu 2>/dev/null || echo 1
    else
        getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1
    fi
}

log_message INFO "开始代码覆盖率测试..."

# 检查工具依赖
if ! command -v gcov >/dev/null 2>&1; then
    log_message ERROR "gcov 未安装"
    exit 1
fi

if ! command -v lcov >/dev/null 2>&1; then
    log_message WARN "lcov 未安装，将使用基础 gcov 输出"
fi

# 清理旧构建
log_message INFO "清理之前的构建..."
rm -rf build_coverage
mkdir build_coverage
cd build_coverage || exit 1

# 配置并编译
log_message INFO "配置项目(启用覆盖率)..."
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..

log_message INFO "编译项目..."
make -j"$(detect_cores)"

# 运行测试
log_message INFO "运行测试..."
if [[ -f "./risc-v-tests" ]]; then
    ./risc-v-tests
else
    log_message ERROR "找不到测试可执行文件"
    exit 1
fi

# 生成覆盖率报告
log_message INFO "生成覆盖率报告..."

if command -v lcov >/dev/null 2>&1; then
    log_message INFO "使用 lcov 生成 HTML 报告..."

    lcov --capture --directory . --output-file coverage.info --ignore-errors gcov,mismatch
    lcov --remove coverage.info '/usr/*' '*/tests/*' '*/build*/*' --output-file coverage_filtered.info --ignore-errors empty

    mkdir -p coverage_html
    genhtml coverage_filtered.info --output-directory coverage_html

    log_message INFO "HTML 覆盖率报告已生成在: $(pwd)/coverage_html/index.html"
    log_message INFO "可以用浏览器打开查看详细报告"

    log_message INFO "覆盖率汇总:"
    lcov --summary coverage_filtered.info
else
    log_message INFO "使用 gcov 生成基础报告..."

    find . -name "*.gcno" -exec gcov {} \;

    log_message INFO "gcov 文件已生成，查看 .gcov 文件了解覆盖率详情"
    log_message INFO "覆盖率统计:"
    log_message INFO "总 .gcov 文件数: $(find . -name \"*.gcov\" | wc -l)"
fi

log_message INFO "覆盖率测试完成！"
