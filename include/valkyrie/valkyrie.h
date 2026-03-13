// SPDX-License-Identifier: GPL-3.0-only

#ifndef VALKYRIE_H
#define VALKYRIE_H

#ifndef KERNEL_MAJOR
#error "KERNEL_MAJOR must be defined by the build system"
#endif

#ifndef KERNEL_MINOR
#error "KERNEL_MINOR must be defined by the build system"
#endif

#define ARCH_I686 1
#define ARCH_X64 2
#define ARCH_AARCH64 3

#endif /* VALKYRIE_H */