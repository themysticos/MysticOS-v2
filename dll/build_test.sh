#!/bin/sh
# build_test.sh - собрать и запустить тестовый стенд msdll на ХОСТЕ.
#
# Это НЕ сборка для MysticOS. Это проверка логики парсера/релокаций/resolve
# вне ядра, обычным gcc с -m32. Требуется gcc-multilib.
#
# Использование:
#   ./build_test.sh          # собрать и запустить
#   ./build_test.sh build    # только собрать
#   ./build_test.sh clean    # удалить артефакты

set -e
cd "$(dirname "$0")"

OUT=test_msdll

CC=${CC:-gcc}
CFLAGS="-m32 -ffreestanding -nostdlib -fno-builtin -fno-pic \
        -fno-stack-protector -no-pie -O2 -Wall -Wextra"

build() {
    echo "[build] $CC $CFLAGS"
    $CC $CFLAGS test_msdll.c msdll.c -o "$OUT"
    echo "[build] OK -> $OUT"
}

run() {
    echo "[run] ./$OUT"
    ./"$OUT"
}

clean() {
    rm -f "$OUT"
    rm -f msdll.o test_msdll.o
    echo "[clean] done"
}

case "${1:-all}" in
    build) build ;;
    run)   run ;;
    clean) clean ;;
    all)   build; run ;;
    *)     echo "usage: $0 [build|run|clean|all]"; exit 1 ;;
esac
