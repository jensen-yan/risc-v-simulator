target_sources(risc-v-sim-lib PRIVATE
    cpu_factory.cpp
)

add_subdirectory(inorder)
add_subdirectory(ooo)
