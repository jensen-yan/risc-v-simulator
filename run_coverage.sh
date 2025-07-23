#!/bin/bash

# 代码覆盖率测试脚本

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}开始代码覆盖率测试...${NC}"

# 检查是否安装了必要工具
command -v gcov >/dev/null 2>&1 || { echo -e "${RED}错误: gcov 未安装${NC}" >&2; exit 1; }
command -v lcov >/dev/null 2>&1 || { echo -e "${YELLOW}警告: lcov 未安装，将使用基础gcov输出${NC}"; }

# 清理之前的构建
echo -e "${YELLOW}清理之前的构建...${NC}"
rm -rf build_coverage
mkdir build_coverage
cd build_coverage

# 使用Coverage标志构建
echo -e "${YELLOW}配置项目(启用覆盖率)...${NC}"
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..

echo -e "${YELLOW}编译项目...${NC}"
make -j$(nproc)

# 运行测试
echo -e "${YELLOW}运行测试...${NC}"
if [ -f "./risc-v-tests" ]; then
    ./risc-v-tests
else
    echo -e "${RED}错误: 找不到测试可执行文件${NC}"
    exit 1
fi

# 生成覆盖率报告
echo -e "${YELLOW}生成覆盖率报告...${NC}"

if command -v lcov >/dev/null 2>&1; then
    # 使用lcov生成详细报告
    echo -e "${GREEN}使用lcov生成HTML报告...${NC}"
    
    # 创建覆盖率数据
    lcov --capture --directory . --output-file coverage.info --ignore-errors gcov,mismatch
    
    # 过滤掉系统文件和测试文件
    lcov --remove coverage.info '/usr/*' '*/tests/*' '*/build*/*' --output-file coverage_filtered.info --ignore-errors empty
    
    # 生成HTML报告
    mkdir -p coverage_html
    genhtml coverage_filtered.info --output-directory coverage_html
    
    echo -e "${GREEN}HTML覆盖率报告已生成在: $(pwd)/coverage_html/index.html${NC}"
    echo -e "${GREEN}可以用浏览器打开查看详细报告${NC}"
    
    # 显示覆盖率汇总
    echo -e "${YELLOW}覆盖率汇总:${NC}"
    lcov --summary coverage_filtered.info
    
else
    # 使用基础gcov
    echo -e "${GREEN}使用gcov生成基础报告...${NC}"
    
    # 为每个源文件生成gcov报告
    find . -name "*.gcno" -exec gcov {} \;
    
    echo -e "${GREEN}gcov文件已生成，查看.gcov文件了解覆盖率详情${NC}"
    
    # 显示一些基本统计
    echo -e "${YELLOW}覆盖率统计:${NC}"
    echo "总.gcov文件数: $(find . -name "*.gcov" | wc -l)"
fi

echo -e "${GREEN}覆盖率测试完成！${NC}"