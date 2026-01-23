# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.15)

# Define build variables
set(CONFIG "debug" CACHE STRING "Build configuration")
set_property(CACHE CONFIG PROPERTY STRINGS "debug" "release")

set(ARCH "i686" CACHE STRING "Target architecture")
set_property(CACHE ARCH PROPERTY STRINGS "i686" "x64")

set(IMAGE_FS "fat32" CACHE STRING "Type of image")
set_property(CACHE IMAGE_FS PROPERTY STRINGS "fat12" "fat16" "fat32" "ext2")

set(BUILD_TYPE_OPTION "full" CACHE STRING "What to build")
set_property(CACHE BUILD_TYPE_OPTION PROPERTY STRINGS "full" "kernel" "usr" "image")

set(IMAGE_SIZE "250m" CACHE STRING "The size of the image (use suffixes k/m/g)")
set(TOOLCHAIN_PATH "toolchain/" CACHE PATH "Path to toolchain directory")
set(OUTPUT_FILE "image" CACHE STRING "The name of final image")
set(OUTPUT_FORMAT "img" CACHE STRING "The extension of the disk image")
set(KERNEL_NAME "valkyrix" CACHE STRING "The name of the executable")

# Dependencies versions
set(BINUTILS_VERSION "2.37")
set(GCC_VERSION "15.2.0")
set(MUSL_VERSION "1.2.5")

# ============================================================================
# Target environment (cross-compilation) - MUST be set BEFORE project()
# ============================================================================

set(PLATFORM_PREFIX "")
set(TARGET "unknown")

if(ARCH STREQUAL "i686")
    set(PLATFORM_PREFIX "i686-linux-musl-")
    set(TARGET "i686-linux-musl")
elseif(ARCH STREQUAL "x64")
    set(PLATFORM_PREFIX "x86_64-linux-musl-")
    set(TARGET "x86_64-linux-musl")
endif()

# Resolve toolchain path
get_filename_component(TOOLCHAIN_DIR "${TOOLCHAIN_PATH}" ABSOLUTE)
set(TOOLCHAIN_BIN "${TOOLCHAIN_DIR}/bin")

# Extract platform prefix without trailing dash for lib path
string(REGEX REPLACE "-$" "" PLATFORM_PREFIX_NOTRAIL "${PLATFORM_PREFIX}")
set(TOOLCHAIN_GCC_LIBS "${TOOLCHAIN_DIR}/lib/gcc/${PLATFORM_PREFIX_NOTRAIL}${GCC_VERSION}")

# Set cross-compilation toolchain with FULL PATHS (before project())
set(CMAKE_C_COMPILER "${TOOLCHAIN_BIN}/${PLATFORM_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/${PLATFORM_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_BIN}/${PLATFORM_PREFIX}gcc")
set(CMAKE_AR "${TOOLCHAIN_BIN}/${PLATFORM_PREFIX}ar")
set(CMAKE_RANLIB "${TOOLCHAIN_BIN}/${PLATFORM_PREFIX}ranlib")

# ============================================================================
# Host environment
# ============================================================================

project(Valkyrie C CXX ASM)

message(STATUS "Toolchain GCC libs: ${TOOLCHAIN_GCC_LIBS}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "ASM Compiler: ${CMAKE_ASM_COMPILER}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

add_compile_options(-std=c99)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

if(CONFIG STREQUAL "debug")
    add_compile_options(-O0 -DDEBUG -g)
else()
    add_compile_options(-O3 -DRELEASE)
endif()

# ============================================================================
# Architecture macros
# ============================================================================

if(ARCH STREQUAL "i686")
    add_compile_definitions(I686)
elseif(ARCH STREQUAL "x64")
    add_compile_definitions(X64)
else()
    add_compile_definitions(NOARCH)
endif()

# ============================================================================
# Build subdirectories based on BUILD_TYPE_OPTION
# ============================================================================

if(BUILD_TYPE_OPTION STREQUAL "full" OR BUILD_TYPE_OPTION STREQUAL "usr")
    add_subdirectory(usr)
endif()

if(BUILD_TYPE_OPTION STREQUAL "full" OR BUILD_TYPE_OPTION STREQUAL "kernel")
    add_subdirectory(kernel)
endif()

if(BUILD_TYPE_OPTION STREQUAL "full" OR BUILD_TYPE_OPTION STREQUAL "image")
    add_subdirectory(image)
endif()

# ============================================================================
# Phony targets (custom commands)
# ============================================================================

if(BUILD_TYPE_OPTION STREQUAL "full" OR BUILD_TYPE_OPTION STREQUAL "image")
    add_custom_target(run
        COMMAND ./scripts/base/qemu.sh disk ${OUTPUT_FILE}
        DEPENDS ${OUTPUT_FILE}
    )
    add_custom_target(debug
        COMMAND ./scripts/base/gdb.sh disk ${OUTPUT_FILE}
        DEPENDS ${OUTPUT_FILE}
    )
    add_custom_target(bochs
        COMMAND ./scripts/base/bochs.sh disk ${OUTPUT_FILE}
        DEPENDS ${OUTPUT_FILE}
    )
endif()

add_custom_target(toolchain
    COMMAND python3 ./scripts/base/toolchain.py
)

add_custom_target(fformat
    COMMAND ./scripts/base/format.sh
)

# Export variables for subdirectories
set(TOOLCHAIN_LIBGCC "${TOOLCHAIN_GCC_LIBS}" PARENT_SCOPE)
