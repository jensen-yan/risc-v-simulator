target_sources(risc-v-sim-lib PRIVATE
    ooo_cpu.cpp
    dynamic_inst.cpp
    execute_semantics.cpp
    perf_counters.cpp
    register_rename.cpp
    reorder_buffer.cpp
    reservation_station.cpp
    store_buffer.cpp
)

add_subdirectory(stages)
