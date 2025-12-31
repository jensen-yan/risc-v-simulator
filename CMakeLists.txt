cmake_minimum_required(VERSION 3.16)

project(risc-v-simulator 
    VERSION 1.0.0
    DESCRIPTION "A modular RISC-V CPU simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 调试/发布编译参数
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g")

# 代码覆盖率配置
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# 查找fmt库
find_package(fmt QUIET)
if(NOT fmt_FOUND)
    # 如果系统没有fmt，尝试使用FetchContent下载
    include(FetchContent)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG        10.1.1  # 使用稳定版本
    )
    FetchContent_MakeAvailable(fmt)
endif()

# 源码按目录拆分管理
add_subdirectory(src)

# 主可执行目标
add_executable(risc-v-sim
    src/main.cpp
)

target_include_directories(risc-v-sim PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(risc-v-sim risc-v-sim-lib)

# 测试目标放在 tests/CMakeLists.txt
add_subdirectory(tests)
