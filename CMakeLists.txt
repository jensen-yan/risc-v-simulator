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

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 设置包含目录
include_directories(include)

# 定义源文件组
set(COMMON_SOURCES
    src/common/types.cpp
    src/common/debug_types.cpp
)

set(CORE_SOURCES
    src/core/memory.cpp
    src/core/alu.cpp
    src/core/decoder.cpp
    src/core/instruction_executor.cpp
)

set(CPU_INORDER_SOURCES
    src/cpu/inorder/cpu.cpp
)

set(CPU_OOO_SOURCES
    src/cpu/ooo/ooo_cpu.cpp
    src/cpu/ooo/register_rename.cpp
    src/cpu/ooo/reorder_buffer.cpp
    src/cpu/ooo/reservation_station.cpp
    src/cpu/ooo/stages/fetch_stage.cpp
    src/cpu/ooo/stages/decode_stage.cpp
    src/cpu/ooo/stages/issue_stage.cpp
    src/cpu/ooo/stages/execute_stage.cpp
    src/cpu/ooo/stages/writeback_stage.cpp
    src/cpu/ooo/stages/commit_stage.cpp
)

set(CPU_SOURCES
    src/cpu/cpu_factory.cpp
    ${CPU_INORDER_SOURCES}
    ${CPU_OOO_SOURCES}
)

set(SYSTEM_SOURCES
    src/system/simulator.cpp
    src/system/elf_loader.cpp
    src/system/syscall_handler.cpp
)

# 所有库源文件
set(LIB_SOURCES
    ${COMMON_SOURCES}
    ${CORE_SOURCES}
    ${CPU_SOURCES}
    ${SYSTEM_SOURCES}
)

# 主可执行文件
add_executable(risc-v-sim
    src/main.cpp
    ${LIB_SOURCES}
)

target_include_directories(risc-v-sim PRIVATE include)

find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    
    file(GLOB TEST_SOURCES "tests/*.cpp")
    
    add_executable(risc-v-tests
        ${TEST_SOURCES}
        ${LIB_SOURCES}
    )
    
    target_include_directories(risc-v-tests PRIVATE include)
    target_link_libraries(risc-v-tests GTest::gtest_main)
    
    include(GoogleTest)
    gtest_discover_tests(risc-v-tests)
endif()