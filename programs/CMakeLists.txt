cmake_minimum_required(VERSION 3.16)

project(risc-v-programs LANGUAGES C)

# 查找 RISC-V 工具链
find_program(RISCV_GCC riscv64-unknown-elf-gcc)
find_program(RISCV_OBJCOPY riscv64-unknown-elf-objcopy)

if(NOT RISCV_GCC OR NOT RISCV_OBJCOPY)
    message(FATAL_ERROR "未找到 RISC-V 工具链 (riscv64-unknown-elf-gcc)")
endif()

# 编译选项
set(RISCV_CFLAGS -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -static -Wl,-Ttext=0x1000)

# 获取所有 C 源文件
file(GLOB PROGRAM_SOURCES "*.c")
# 排除有问题的文件
list(REMOVE_ITEM PROGRAM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_arithmetic.c")

# 为每个程序创建目标
foreach(PROGRAM_SOURCE ${PROGRAM_SOURCES})
    get_filename_component(PROGRAM_NAME ${PROGRAM_SOURCE} NAME_WE)
    
    # 创建单独的目标
    add_custom_target(${PROGRAM_NAME}_elf
        COMMAND ${RISCV_GCC} ${RISCV_CFLAGS} -o ${PROGRAM_NAME}.elf ${PROGRAM_SOURCE}
        DEPENDS ${PROGRAM_SOURCE}
        COMMENT "编译 ${PROGRAM_NAME}.elf"
    )
    
    add_custom_target(${PROGRAM_NAME}_bin
        COMMAND ${RISCV_OBJCOPY} -O binary ${PROGRAM_NAME}.elf ${PROGRAM_NAME}.bin
        DEPENDS ${PROGRAM_NAME}_elf
        COMMENT "生成 ${PROGRAM_NAME}.bin"
    )
    
    add_custom_target(${PROGRAM_NAME}_asm
        COMMAND ${RISCV_GCC} ${RISCV_CFLAGS} -S -o ${PROGRAM_NAME}.s ${PROGRAM_SOURCE}
        DEPENDS ${PROGRAM_SOURCE}
        COMMENT "生成 ${PROGRAM_NAME}.s"
    )
    
    list(APPEND ALL_TARGETS ${PROGRAM_NAME}_bin ${PROGRAM_NAME}_asm)
endforeach()

# 编译所有程序
add_custom_target(all_programs ALL)
add_dependencies(all_programs ${ALL_TARGETS})