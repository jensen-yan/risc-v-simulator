find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing()

    # 收集测试源文件（新增文件会触发重新生成构建）
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

    add_executable(risc-v-tests
        ${TEST_SOURCES}
    )

    target_include_directories(risc-v-tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(risc-v-tests risc-v-sim-lib GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(risc-v-tests)
endif()
