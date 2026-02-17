# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

BUILDER_BIN := build/builder
BUILDER_SRCS := tools/builder/build.c tools/builder/config.c tools/builder/image.c

.PHONY: all builder menuconfig build clean run debug bochs toolchain deps fformat

all: build

builder: $(BUILDER_BIN)

$(BUILDER_BIN): $(BUILDER_SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(BUILDER_SRCS) -lncurses -o $@

menuconfig: builder
	./$(BUILDER_BIN) --menu

build: builder
	./$(BUILDER_BIN) --build

clean: builder
	./$(BUILDER_BIN) --clean

run: builder
	./$(BUILDER_BIN) --target run

debug: builder
	./$(BUILDER_BIN) --target debug

bochs: builder
	./$(BUILDER_BIN) --target bochs

toolchain: builder
	./$(BUILDER_BIN) --target toolchain

deps: builder
	./$(BUILDER_BIN) --target deps

fformat: builder
	./$(BUILDER_BIN) --target fformat
