#!/bin/sh
# build.sh - сборка ELF-программы для MysticOS 2.
set -e
SRC="${1:-winapp.c}"
OUT="${2:-winapp.elf}"
gcc -m32 -ffreestanding -nostdlib -static -no-pie -fno-pic \
    -Wl,-Ttext-segment=0x4000000 -Wl,-e,_start -Wl,--build-id=none \
    "$SRC" -o "$OUT"
echo "Built $OUT"
