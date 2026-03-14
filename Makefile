# Valkyrie OS Makefile Build System
# Read .config if it exists to match SCons behavior
ifneq ("$(wildcard .config)","")
    include .config
endif

# Sensible defaults for variables not defined in .config
# Note: Makefile variables from .config (like arch = 'i686') 
# might need quotes stripped if they are string literals.
strip_quotes = $(shell echo $(1) | sed "s/['\"]//g")

CONFIG       := $(call strip_quotes,$(config))
ARCH         := $(call strip_quotes,$(arch))
IMAGE_FS     := $(call strip_quotes,$(imageFS))
BUILD_TYPE   := $(call strip_quotes,$(buildType))
IMAGE_SIZE   := $(call strip_quotes,$(imageSize))
TOOLCHAIN_RAW := $(call strip_quotes,$(toolchain))
OUTPUT_FILE  := $(call strip_quotes,$(outputFile))
OUTPUT_FORMAT:= $(call strip_quotes,$(outputFormat))
KERNEL_NAME  := $(call strip_quotes,$(kernelName))

# Fallbacks if .config is missing or partial
CONFIG       ?= debug
ARCH         ?= i686
IMAGE_FS     ?= fat32
BUILD_TYPE   ?= full
IMAGE_SIZE   ?= 250m
TOOLCHAIN_RAW ?= toolchain/
TOOLCHAIN    := $(abspath $(TOOLCHAIN_RAW))
OUTPUT_FILE  ?= valkyrieos
OUTPUT_FORMAT?= img
KERNEL_NAME  ?= valkyrix
KERNEL_MAJOR ?= 0
KERNEL_MINOR ?= 26

# Propagate version
KERNEL_VERSION := $(KERNEL_MAJOR).$(KERNEL_MINOR)
KERNEL_OUTPUT_NAME := $(KERNEL_NAME)-$(KERNEL_VERSION)

# Directory Structure
BUILD_ROOT := build
BUILD_DIR  := $(BUILD_ROOT)/$(ARCH)_$(CONFIG)
STAGING_DIR := $(BUILD_DIR)/$(OUTPUT_FORMAT)

# --- Toolchain Detection ---
# Matching scripts/scons/arch.py logic
ARCH_CONFIG_PY := python3 $(CURDIR)/scripts/scons/config_helper.py --arch $(ARCH)
TARGET_TRIPLE := $(shell $(ARCH_CONFIG_PY) --get target_triple)
TOOLCHAIN_PREFIX := $(shell $(ARCH_CONFIG_PY) --get toolchain_prefix)

# Determine if we use cross-prefix or native
# Use the toolchain prefix (e.g. i686-linux-musl-) before tool names
export CROSS_BIN := $(TOOLCHAIN)/bin/

AS      := $(CROSS_BIN)$(TOOLCHAIN_PREFIX)as
CC      := $(CROSS_BIN)$(TOOLCHAIN_PREFIX)gcc
CXX     := $(CROSS_BIN)$(TOOLCHAIN_PREFIX)g++
LD      := $(CROSS_BIN)$(TOOLCHAIN_PREFIX)gcc
AR      := $(CROSS_BIN)$(TOOLCHAIN_PREFIX)ar
RANLIB  := $(CROSS_BIN)$(TOOLCHAIN_PREFIX)ranlib
STRIP   := $(CROSS_BIN)$(TOOLCHAIN_PREFIX)strip

# --- Flags ---
# Common flags from SConstruct
COMMON_FLAGS := -std=c99 -D$(shell $(ARCH_CONFIG_PY) --get define) \
                -DKERNEL_MAJOR=$(KERNEL_MAJOR) -DKERNEL_MINOR=$(KERNEL_MINOR)

ifeq ($(CONFIG),debug)
    COMMON_FLAGS += -O0 -DDEBUG -g
else
    COMMON_FLAGS += -O3 -DRELEASE -s
endif

export COMMON_FLAGS
export CONFIG ARCH IMAGE_FS IMAGE_SIZE OUTPUT_FILE OUTPUT_FORMAT KERNEL_OUTPUT_NAME TOOLCHAIN STAGING_DIR
export KERNEL_MAJOR KERNEL_MINOR
export TOOLCHAIN_PREFIX CC CXX LD AS AR RANLIB STRIP CROSS_BIN

# --- Targets ---

.PHONY: all kernel usr image clean ensure_toolchain

all: ensure_toolchain
ifeq ($(BUILD_TYPE),full)
	$(MAKE) kernel
	$(MAKE) usr
	$(MAKE) image
else ifeq ($(BUILD_TYPE),kernel)
	$(MAKE) kernel
else ifeq ($(BUILD_TYPE),usr)
	$(MAKE) usr
else ifeq ($(BUILD_TYPE),image)
	$(MAKE) image
endif

ensure_toolchain:
	@python3 ./scripts/base/toolchain.py $(TOOLCHAIN) -t $(TARGET_TRIPLE) --ensure

kernel:
	@$(MAKE) -C kernel

usr:
	@$(MAKE) -C usr

image: kernel usr
	@$(MAKE) -C image

clean:
	rm -rf $(BUILD_ROOT)
	$(MAKE) -C kernel clean
	$(MAKE) -C usr clean
	$(MAKE) -C image clean
