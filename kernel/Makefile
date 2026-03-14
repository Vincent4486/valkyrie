# Kernel Makefile for Valkyrie OS

# Inherit settings from root Makefile (CC, LD, AS are already exported)

# Determine arch-specific path and config (via helper)
ARCH_CONFIG_PY := python3 $(abspath $(CURDIR)/../scripts/scons/config_helper.py) --arch $(ARCH)
ARCH_DIR := arch/$(ARCH)
LINKER_SCRIPT := $(ARCH_DIR)/boot/linker.ld

# Compiler Flags matching kernel/SConscript
KCFLAGS := -ffreestanding -nostdlib -fno-stack-protector -fno-builtin -Wall -Wextra
KCFLAGS += $(shell $(ARCH_CONFIG_PY) --get ccflags)
KCFLAGS += -I. -I../include -I$(ARCH_DIR)

# AS Flags
ASFLAGS := $(shell $(ARCH_CONFIG_PY) --get asflags)
ASFLAGS += -I$(ARCH_DIR) -g -Wa,--noexecstack

# Linker Flags
KLDFLAGS := -nostdlib -Wl,-T,$(LINKER_SCRIPT) -Wl,-Map=core.map -Wl,-z,relro,-z,now -Wl,-z,noexecstack -Wl,--as-needed -Wl,--export-dynamic
KLDFLAGS += $(shell $(ARCH_CONFIG_PY) --get linkflags)

# Toolchain LibGCC
DEPS_GCC_VER := 15.2.0
TOOLCHAIN_LIBGCC := $(TOOLCHAIN)/lib/gcc/$(shell $(ARCH_CONFIG_PY) --get target_triple)/$(DEPS_GCC_VER)

# Source Collection
# This needs to be recursive like GlobSources in scripts/scons/utility.py
# Search recursively from root, excluding generated ISR source
C_SOURCES_RAW := $(shell find . -name "*.c" 2>/dev/null | grep -v isrs_gen.c | sort -u)
ASM_SOURCES_RAW := $(shell find . -name "*.S" 2>/dev/null | sort -u)

# Remove leading ./ for consistency
C_SOURCES := $(patsubst ./%,%,$(C_SOURCES_RAW))
ASM_SOURCES := $(patsubst ./%,%,$(ASM_SOURCES_RAW))

# Explicitly add generated ISR source (will be created by generate_isrs)
C_SOURCES += $(ARCH_DIR)/cpu/isrs_gen.c

OBJECTS := $(C_SOURCES:.c=.o) $(ASM_SOURCES:.S=.o)

# Identify CRT objects to enforce order
# CRT files are in boot directory (architecture-specific)
OBJ_CRTI := $(ARCH_DIR)/boot/crti.o
OBJ_CRTN := $(ARCH_DIR)/boot/crtn.o
# Filter them out of the general objects list
OBJECTS_CORE := $(filter-out $(OBJ_CRTI) $(OBJ_CRTN), $(OBJECTS))

# Final Object Order Matching kernel/SConscript:
# crti, crtbegin, ..., crtend, crtn
LINK_ORDER := $(OBJ_CRTI) $(TOOLCHAIN_LIBGCC)/crtbegin.o $(OBJECTS_CORE) $(TOOLCHAIN_LIBGCC)/crtend.o $(OBJ_CRTN)

# Generation step for ISRs
MKISRS_SH := $(ARCH_DIR)/cpu/mkisrs.sh
ISRS_GEN_C := $(ARCH_DIR)/cpu/isrs_gen.c
ISRS_GEN_INC := $(ARCH_DIR)/cpu/isrs_gen.inc

.PHONY: all clean generate_isrs

all: generate_isrs $(KERNEL_OUTPUT_NAME)

generate_isrs:
	@if [ -f $(MKISRS_SH) ]; then \
		if [ ! -f $(ISRS_GEN_C) ] || [ ! -f $(ISRS_GEN_INC) ]; then \
			bash $(MKISRS_SH) $(ISRS_GEN_C) $(ISRS_GEN_INC); \
		fi; \
	fi

# Object files must be built after ISR generation (order-only prerequisite)
# This ensures mkisrs.sh runs before any .S or .c files are compiled
$(C_SOURCES:.c=.o) $(ASM_SOURCES:.S=.o): | generate_isrs

$(KERNEL_OUTPUT_NAME): $(OBJECTS)
	$(LD) $(KLDFLAGS) -o $@ $(LINK_ORDER) -lgcc

%.o: %.c
	$(CC) $(COMMON_FLAGS) $(KCFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(KERNEL_OUTPUT_NAME) core.map $(ISRS_GEN_C) $(ISRS_GEN_INC)
