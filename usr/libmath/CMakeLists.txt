# SPDX-License-Identifier: BSD-3-Clause

# libmath library
add_library(libmath STATIC)

file(GLOB_RECURSE LIBMATH_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.S"
)

target_sources(libmath PRIVATE ${LIBMATH_SOURCES})

target_include_directories(libmath PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_options(libmath PRIVATE
    -ffreestanding
    -nostdlib
    -fstack-protector-all
    -fPIC
)

set_target_properties(libmath PROPERTIES
    LINKER_LANGUAGE C
)
