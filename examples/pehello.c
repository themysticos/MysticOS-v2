// pehello.c - ПЕРВАЯ PE-программа для MysticOS 2 (.exe).
//
// Собирается кросс-компилятором i686-w64-mingw32-gcc БЕЗ CRT:
//   i686-w64-mingw32-gcc -m32 -ffreestanding -nostdlib -nostartfiles \
//       -e _start -Wl,--subsystem,console -o pehello.exe pehello.c
//
// Использует НАШ API (int 0x80 через include/ms/*.h), НЕ WinAPI.
// Цель: проверить, что PE-программа грузится и работает в ring3.
#include "../include/mysticos.h"

int _start(void) {
    int id = ms_window("PE Hello", 180, 160, 300, 160);
    if (id < 0) ms_exit(1);

    ms_clear(id, 0xF0F0FF);
    ms_text(id, 10, 10,  "Hello from PE (.exe)!", 0x000080);
    ms_text(id, 10, 34,  "MysticOS 2 PE loader",   0x404060);
    ms_text(id, 10, 58,  "Not ELF, not Windows",   0x606080);
    ms_text(id, 10, 100, "Press any key to exit",  0x808090);
    ms_yield();

    while (!ms_key_avail()) {
        ms_yield();
        ms_sleep(30);
    }
    ms_key_get();
    ms_window_close(id);
    ms_exit(0);
}
