# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

BUILDER_BIN := tools/builder/build
CONFIG_BIN := tools/builder/config

.PHONY: all builder config build clean run debug bochs toolchain deps fformat

all: build

builder: $(BUILDER_BIN)

config: $(CONFIG_BIN)

$(BUILDER_BIN): tools/builder/build.c
	$(CC) $(CFLAGS) $< -o $@

$(CONFIG_BIN): tools/builder/config.c
	$(CC) $(CFLAGS) $< -lncurses -o $@

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
