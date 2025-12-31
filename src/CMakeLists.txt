add_library(risc-v-sim-lib STATIC)

add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(cpu)
add_subdirectory(system)

# 目标级别 include（PUBLIC 会向依赖方传递）
target_include_directories(risc-v-sim-lib PUBLIC ${PROJECT_SOURCE_DIR}/include)

# 链接 fmt 到库（PUBLIC 向依赖方传递）
target_link_libraries(risc-v-sim-lib PUBLIC fmt::fmt)

# 设置库输出名称（静态库名与可执行保持一致）
set_target_properties(risc-v-sim-lib PROPERTIES OUTPUT_NAME risc-v-sim)
