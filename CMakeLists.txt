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

# 启用 CTest（让子目录测试能被 ctest 发现）
enable_testing()

# 调试/发布编译参数
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g")

# 代码覆盖率配置
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

option(RISCV_SIM_ENABLE_DRAMSIM3 "Enable optional DRAMSim3 memory timing backend" OFF)
set(
    DRAMSIM3_ROOT
    "${PROJECT_SOURCE_DIR}/third_party/DRAMsim3"
    CACHE PATH "Path to the DRAMSim3 source tree"
)
set(
    RISCV_SIM_DEFAULT_DRAMSIM3_CONFIG
    "${PROJECT_SOURCE_DIR}/configs/dramsim3/xiangshan_DDR4_8Gb_x8_3200_2ch.ini"
    CACHE FILEPATH "Default DRAMSim3 DDR config used when --dramsim3-ini is omitted"
)

if(RISCV_SIM_ENABLE_DRAMSIM3)
    if(NOT EXISTS "${DRAMSIM3_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR
            "DRAMSim3 submodule not found at ${DRAMSIM3_ROOT}. "
            "Run: git submodule update --init third_party/DRAMsim3"
        )
    endif()
    if(NOT EXISTS "${DRAMSIM3_ROOT}/src/dramsim3.h")
        message(FATAL_ERROR "DRAMSim3 header not found: ${DRAMSIM3_ROOT}/src/dramsim3.h")
    endif()

    add_subdirectory("${DRAMSIM3_ROOT}" "${CMAKE_BINARY_DIR}/third_party/dramsim3" EXCLUDE_FROM_ALL)
    set_target_properties(dramsim3 PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/third_party/dramsim3"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/third_party/dramsim3"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/third_party/dramsim3"
    )
    add_library(DRAMSim3::dramsim3 ALIAS dramsim3)
    find_library(GOMP_LIBRARY gomp)
endif()

# 可选的已安装 GTest 前缀；默认也会自动尝试 ~/.local 等用户目录
set(
    RISCV_SIM_GTEST_ROOT
    ""
    CACHE PATH "Prefix of an installed GTest package used when system GTest is unavailable"
)

set(_gtest_prefixes)
if(RISCV_SIM_GTEST_ROOT)
    list(APPEND _gtest_prefixes "${RISCV_SIM_GTEST_ROOT}")
endif()

if(DEFINED ENV{GTEST_ROOT} AND NOT "$ENV{GTEST_ROOT}" STREQUAL "")
    list(APPEND _gtest_prefixes "$ENV{GTEST_ROOT}")
endif()

if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
    list(APPEND _gtest_prefixes
        "$ENV{HOME}/.local"
        "$ENV{HOME}/opt/gtest"
        "$ENV{HOME}/opt/googletest"
    )
endif()

set(_gtest_config_hints)
foreach(prefix IN LISTS _gtest_prefixes)
    if(EXISTS "${prefix}")
        list(APPEND _gtest_config_hints
            "${prefix}"
            "${prefix}/lib/cmake/GTest"
            "${prefix}/lib64/cmake/GTest"
            "${prefix}/share/cmake/GTest"
        )
    endif()
endforeach()

find_package(GTest QUIET CONFIG HINTS ${_gtest_config_hints})
if(NOT GTest_FOUND)
    find_package(GTest QUIET)
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
if(RISCV_SIM_ENABLE_DRAMSIM3)
    set_target_properties(risc-v-sim PROPERTIES BUILD_RPATH "$<TARGET_FILE_DIR:dramsim3>")
endif()

# 测试目标放在 tests/CMakeLists.txt
add_subdirectory(tests)
