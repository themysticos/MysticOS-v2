// example_msgui.c - ПРИМЕР .msdll для MysticOS 2.
//
// Это не настоящий UI-фреймворк, а проверка цепочки: сборка .msdll,
// загрузка ядром, resolve символов программой.
//
// Сборка (см. dll/build_dll.sh):
//   gcc -m32 -ffreestanding -nostdlib -fno-pic -O2 -c example_msgui.c -o x.o
//   gcc -m32 -nostdlib -Wl,-T,msdll.ld -Wl,-e,_start x.o -o x.elf
//   python3 ../tools/msdll_pack.py x.elf example_msgui.msdll
#include "msdll.h"

// Глобальная переменная с абсолютным адресом — даст R_386_32 релокацию.
static unsigned int g_counter = 0x1000;

int msgui_add(int a, int b) {
    g_counter += a;
    return a + b + g_counter;
}

int msgui_version(void) {
    return 1;
}

// _start для линкера (DllMain не задаём — entry_rva будет _start).
int _start(unsigned int base, unsigned int reason) {
    (void)base; (void)reason;
    return 0;
}

MSDLL_EXPORT(add, msgui_add);
MSDLL_EXPORT(version, msgui_version);
