#!/bin/sh
# build_dll.sh - собрать пример .msdll на ХОСТЕ (не для ядра).
#
# Проверяет всю цепочку: gcc -> linker script -> msdll_pack.py.
# Требуется gcc-multilib.
#
# Использование:
#   ./build_dll.sh [src.c] [out.msdll]
set -e
cd "$(dirname "$0")"

SRC=${1:-example_msgui.c}
NAME=$(basename "$SRC" .c)
OUT=${2:-$NAME.msdll}
OBJ=/tmp/$NAME.o
ELF=/tmp/$NAME.elf

CC=${CC:-gcc}
CFLAGS="-m32 -ffreestanding -nostdlib -fno-pic -fno-builtin \
        -fno-stack-protector -O2 -Wall -Wextra -fno-pie"
LDFLAGS="-m32 -nostdlib -no-pie -Wl,-T,msdll.ld -Wl,-e,_start \
         -Wl,--build-id=none"

echo "[dll] compile $SRC"
$CC $CFLAGS -c "$SRC" -o "$OBJ"
echo "[dll] link"
$CC $LDFLAGS "$OBJ" -o "$ELF"
echo "[dll] pack"
python3 ../tools/msdll_pack.py "$ELF" "$OUT"
echo "[dll] OK -> $OUT"
