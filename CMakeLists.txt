cmake_minimum_required(VERSION 3.16)

project(risc-v-simulator 
    VERSION 1.0.0
    DESCRIPTION "A simple RISC-V CPU simulator"
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

include_directories(include)

# 自动读取 src 目录下所有 .cpp 文件，但排除 main.cpp
file(GLOB SOURCES "src/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# 自动读取 include 目录下所有 .h 文件
file(GLOB HEADERS "include/*.h")

add_executable(risc-v-sim
    src/main.cpp
    ${SOURCES}
)

target_include_directories(risc-v-sim PRIVATE include)

find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()
    
    file(GLOB TEST_SOURCES "tests/*.cpp")
    
    add_executable(risc-v-tests
        ${TEST_SOURCES}
        ${SOURCES}
    )
    
    target_include_directories(risc-v-tests PRIVATE include)
    target_link_libraries(risc-v-tests GTest::gtest_main)
    
    include(GoogleTest)
    gtest_discover_tests(risc-v-tests)
endif()