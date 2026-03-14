# Usermode Makefile for Valkyrie OS

# Inherit settings from root Makefile (CC, LD, TOOLCHAIN_PREFIX exported)

# Usermode Environment Setup matching usr/SConscript
# (Note: Using shell find/glob for sources)
ARCH_CONFIG_PY := python3 $(abspath $(CURDIR)/../scripts/scons/config_helper.py) --arch $(ARCH)
TARGET_TRIPLE  := $(shell $(ARCH_CONFIG_PY) --get target_triple)
SYSROOT        := $(TOOLCHAIN)/$(TARGET_TRIPLE)/sysroot/usr

# Usermode Flags
UCCFLAGS := -D_POSIX_C_SOURCE=200809L
UCCFLAGS += $(shell $(ARCH_CONFIG_PY) --get ccflags)
UCCFLAGS += -I$(SYSROOT)/include -I$(CURDIR)/../include

.PHONY: all clean libmath sh

all: libmath sh

# Build libmath (shared library)
libmath:
	@$(MAKE) -C libmath -f Makefile.mk UCCFLAGS="$(UCCFLAGS)" SYSROOT="$(SYSROOT)"

# Build sh (application)
sh:
	@$(MAKE) -C sh -f Makefile.mk UCCFLAGS="$(UCCFLAGS)" SYSROOT="$(SYSROOT)"

clean:
	@$(MAKE) -C libmath -f Makefile.mk clean
	@$(MAKE) -C sh -f Makefile.mk clean
