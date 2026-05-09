// SPDX-License-Identifier: GPL-3.0-only
#include <stddef.h>
#include <stdint.h>

struct fs_operations
{
   uint32_t fs_init;
   uint32_t fs_open;
   uint32_t fs_read;
   uint32_t fs_close;
};

int fs_init(void) { return 1; }
int fs_open(void) { return 2; }
int fs_read(void) { return 3; }
int fs_close(void) { return 4; }

#ifdef COREFS

static const struct fs_operations fs_exports
    __attribute__((section(".exports"), used)) = {
        .fs_init = (uint32_t)fs_init,
        .fs_open = (uint32_t)fs_open,
        .fs_read = (uint32_t)fs_read,
        .fs_close = (uint32_t)fs_close,
};

#endif /* COREFS */
