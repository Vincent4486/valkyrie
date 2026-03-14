# sh application Makefile.mk for Valkyrie OS

# Inherit settings from usr/Makefile
CROSS_COMPILE := $(TOOLCHAIN)/bin/$(TOOLCHAIN_PREFIX)
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc

# Simple application flags matching usr/sh/SConscript
APP_CFLAGS := $(COMMON_FLAGS) $(UCCFLAGS) -I.

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
TARGET  := sh

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(APP_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
