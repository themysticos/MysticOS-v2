#!/bin/sh
# build_test.sh - собрать и запустить тесты msdll/PE на ХОСТЕ.
#
# Это НЕ сборка для MysticOS. Проверка логики парсера/релокаций/resolve
# и импортов PE, обычным gcc с -m32. Требуется gcc-multilib.
#
# Использование:
#   ./build_test.sh          # собрать и запустить всё
#   ./build_test.sh build    # только собрать
#   ./build_test.sh clean    # удалить артефакты

set -e
cd "$(dirname "$0")"

OUT=test_msdll
REAL=test_real_dll
IMP=test_dllimport

CC=${CC:-gcc}
CFLAGS="-m32 -ffreestanding -nostdlib -fno-builtin -fno-pic \
        -fno-stack-protector -no-pie -O2 -Wall -Wextra"
MINGW=${MINGW:-i686-w64-mingw32-gcc}

build() {
    echo "[build] unit tests"
    $CC $CFLAGS test_msdll.c msdll.c -o "$OUT"
    echo "[build] OK -> $OUT"
    if command -v python3 >/dev/null 2>&1; then
        echo "[build] example .msdll"
        sh build_dll.sh example_msgui.c example_msgui.msdll >/dev/null
        echo "[build] real-dll test"
        $CC $CFLAGS test_real_dll.c msdll.c -o "$REAL"
        echo "[build] OK -> $REAL"
    fi
    # Тест импортов PE (exe -> dll). Нужен mingw + dlltool.
    if command -v "$MINGW" >/dev/null 2>&1; then
        echo "[build] PE test dll + exe"
        $MINGW -m32 -shared -nostdlib -nostartfiles \
            -Wl,--image-base,0x0C000000 -o testdll.dll ../examples/testdll.c
        i686-w64-mingw32-dlltool -d ../examples/testdll.def -l libtestdll.a
        $MINGW -m32 -ffreestanding -nostdlib -nostartfiles -e _start \
            -Wl,--subsystem,console -Wl,--image-base,0x04000000 \
            -Wl,--disable-dynamicbase -o testexe.exe ../examples/testexe.c \
            -L. -ltestdll
        echo "[build] import test"
        $CC $CFLAGS test_dllimport.c ../pe.c msdll.c -o "$IMP"
        echo "[build] OK -> $IMP"
    else
        echo "[skip] mingw not found: import test skipped"
    fi
}

run() {
    echo "[run] ./$OUT"
    ./"$OUT"
    if [ -x "./$REAL" ]; then
        echo ""; echo "[run] ./$REAL"; ./"$REAL"
    fi
    if [ -x "./$IMP" ]; then
        echo ""; echo "[run] ./$IMP"; ./"$IMP"
    fi
}

clean() {
    rm -f "$OUT" "$REAL" "$IMP"
    rm -f example_msgui.msdll testdll.dll testexe.exe libtestdll.a
    rm -f msdll.o test_msdll.o test_real_dll.o test_dllimport.o pe.o
    echo "[clean] done"
}

case "${1:-all}" in
    build) build ;;
    run)   run ;;
    clean) clean ;;
    all)   build; run ;;
    *)     echo "usage: $0 [build|run|clean|all]"; exit 1 ;;
esac
