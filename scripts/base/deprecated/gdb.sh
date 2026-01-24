#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

QEMU_ARGS="-S -gdb stdio -m 32"

if [ "$#" -le 1 ]; then
    echo "Usage: ./debug.sh <image_type> <image>"
    exit 1
fi

case "$1" in
    "floppy")   QEMU_ARGS="${QEMU_ARGS} -fda $2"
    ;;
    "disk")     QEMU_ARGS="${QEMU_ARGS} -hda $2"
    ;;
    *)          echo "Unknown image type $1."
                exit 2
esac


cat > .gdb_script.gdb << EOF
    target remote | qemu-system-i386 $QEMU_ARGS
    set disassembly-flavor intel
    b *0x7c00
    layout asm
EOF

gdb -x .gdb_script.gdb
rm -f .gdb_script.gdb