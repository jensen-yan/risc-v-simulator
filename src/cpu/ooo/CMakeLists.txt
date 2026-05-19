target_sources(risc-v-sim-lib PRIVATE
    ooo_cpu.cpp
    branch_predictor.cpp
    commit_control_flow_effects.cpp
    commit_memory_effects.cpp
    commit_register_effects.cpp
    commit_retire_effects.cpp
    commit_system_effects.cpp
    dynamic_inst.cpp
    execute_control_recovery.cpp
    execute_dcache_access.cpp
    execute_host_comm_access.cpp
    execute_load_value.cpp
    execute_memory_order.cpp
    execute_semantics.cpp
    ooo_recovery.cpp
    perf_counters.cpp
    register_rename.cpp
    reorder_buffer.cpp
    reservation_station.cpp
    store_buffer.cpp
)

add_subdirectory(cache)
add_subdirectory(stages)
