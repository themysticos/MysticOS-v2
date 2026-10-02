// about.c - SYSPR: окно "About" (API без api->).
//
// Демонстрирует новый стиль: ms_window(), ms_clear(), ms_text(), ms_yield(),
// ms_key_avail(), ms_exit(). Внутри — int 0x80.
#include "../include/mysticos.h"

int _start(void) {
    int W = 260, H = 150;
    int id = ms_window("About", 200, 320, W, H);
    if (id < 0) ms_exit(1);

    ms_clear(id, 0xE0E0E8);
    ms_text(id, 10, 10,  "MysticOS 2",           0x202040);
    ms_text(id, 10, 28,  "Graphics edition",    0x404060);
    ms_text(id, 10, 44,  "C + NASM, VBE 24bpp", 0x404060);
    ms_text(id, 10, 70,  "SYSPR program (ELF)", 0x006020);
    ms_text(id, 10, 100, "Press any key / close", 0x808090);

    ms_yield();
    while (!ms_key_avail()) {
        ms_yield();
        ms_sleep(30);
    }
    ms_key_get();
    ms_window_close(id);
    ms_exit(0);
}
