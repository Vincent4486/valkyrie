# SPDX-License-Identifier: BSD-3-Clause

# Get the kernel source directory
set(KERNEL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Create kernel executable
add_executable(${KERNEL_NAME})

# ============================================================================
# Compilation and Linker Flags
# ============================================================================

target_compile_options(${KERNEL_NAME} PRIVATE
    -fPIC
)

target_link_options(${KERNEL_NAME} PRIVATE
    -Wl,-T,${KERNEL_SRC_DIR}/arch/${ARCH}/boot/linker.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/core.map
    -Wl,-z,notext
    -Wl,--as-needed
    -Wl,--export-dynamic
    -pie
)

# ============================================================================
# Include Paths
# ============================================================================

target_include_directories(${KERNEL_NAME} PRIVATE
    ${KERNEL_SRC_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# ============================================================================
# Assembly Configuration
# ============================================================================

# ASM compiler is already set in the root CMakeLists.txt
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -m32 -g -I${KERNEL_SRC_DIR}/arch/${ARCH}")

# ============================================================================
# Source Files
# ============================================================================

file(GLOB_RECURSE KERNEL_SOURCES
    "${KERNEL_SRC_DIR}/*.c"
    "${KERNEL_SRC_DIR}/*.cpp"
    "${KERNEL_SRC_DIR}/*.S"
)

# Find and separate special object files (crti, crtn)
set(CRTI_FILE "")
set(CRTN_FILE "")
set(OTHER_SOURCES "")

foreach(source_file ${KERNEL_SOURCES})
    get_filename_component(filename "${source_file}" NAME)
    if(filename STREQUAL "crti.S" OR filename STREQUAL "crti.c")
        set(CRTI_FILE "${source_file}")
    elseif(filename STREQUAL "crtn.S" OR filename STREQUAL "crtn.c")
        set(CRTN_FILE "${source_file}")
    else()
        list(APPEND OTHER_SOURCES "${source_file}")
    endif()
endforeach()

# ============================================================================
# Compile Objects
# ============================================================================

# Compile crti first
if(CRTI_FILE)
    set(OBJ_CRTI "${CMAKE_CURRENT_BINARY_DIR}/crti.o")
    add_custom_command(OUTPUT "${OBJ_CRTI}"
        COMMAND ${CMAKE_C_COMPILER} -m32 -ffreestanding -nostdlib -fstack-protector-all -fPIC -g -I${KERNEL_SRC_DIR} -I${CMAKE_SOURCE_DIR}/include -c "${CRTI_FILE}" -o "${OBJ_CRTI}"
        DEPENDS "${CRTI_FILE}"
    )
    list(APPEND OBJ_FILES "${OBJ_CRTI}")
endif()

# Compile crtn last
if(CRTN_FILE)
    set(OBJ_CRTN "${CMAKE_CURRENT_BINARY_DIR}/crtn.o")
    add_custom_command(OUTPUT "${OBJ_CRTN}"
        COMMAND ${CMAKE_C_COMPILER} -m32 -ffreestanding -nostdlib -fstack-protector-all -fPIC -g -I${KERNEL_SRC_DIR} -I${CMAKE_SOURCE_DIR}/include -c "${CRTN_FILE}" -o "${OBJ_CRTN}"
        DEPENDS "${CRTN_FILE}"
    )
endif()

# Add sources to target
target_sources(${KERNEL_NAME} PRIVATE ${OTHER_SOURCES})

# ============================================================================
# Link with GCC libraries and startup files
# ============================================================================

# Get the crtbegin/crtend objects from the toolchain
set(CRTBEGIN "${TOOLCHAIN_LIBGCC}/crtbegin.o")
set(CRTEND "${TOOLCHAIN_LIBGCC}/crtend.o")

# Link GCC library
target_link_libraries(${KERNEL_NAME} PRIVATE gcc)
target_link_directories(${KERNEL_NAME} PRIVATE "${TOOLCHAIN_LIBGCC}")

# ============================================================================
# Set link order using linker script or object file ordering
# ============================================================================

if(OBJ_CRTI AND CRTBEGIN AND CRTEND AND OBJ_CRTN)
    # This ensures the linker receives objects in the correct order
    set_target_properties(${KERNEL_NAME} PROPERTIES
        LINK_FLAGS "-Wl,--start-group"
    )
    
    # We need to manually control the link order
    # CMake doesn't provide direct control, so we'll use linker scripts
    target_link_libraries(${KERNEL_NAME} PRIVATE
        ${OBJ_CRTI}
        ${CRTBEGIN}
        ${CRTEND}
        ${OBJ_CRTN}
    )
endif()
