# SPDX-License-Identifier: BSD-3-Clause

# Shell executable
add_executable(sh)

file(GLOB_RECURSE SH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.S"
)

target_sources(sh PRIVATE ${SH_SOURCES})

target_include_directories(sh PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(sh PRIVATE
    -ffreestanding
    -nostdlib
    -fstack-protector-all
    -fPIC
)

target_link_libraries(sh PRIVATE
    gcc
)

target_link_directories(sh PRIVATE
    "${TOOLCHAIN_LIBGCC}"
)

set_target_properties(sh PROPERTIES
    LINKER_LANGUAGE C
)
