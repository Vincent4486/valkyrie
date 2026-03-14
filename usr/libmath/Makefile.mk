# libmath Makefile.mk for Valkyrie OS

# Inherit settings from usr/Makefile
CROSS_COMPILE := $(TOOLCHAIN)/bin/$(TOOLCHAIN_PREFIX)
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc

# Shared library flags matching usr/libmath/SConscript
LIB_CFLAGS := $(COMMON_FLAGS) $(UCCFLAGS) -fPIC -I.
LIB_LDFLAGS := -shared -Wl,-soname,libmath.so

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
TARGET  := libmath.so

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LD) $(LIB_LDFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(LIB_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
