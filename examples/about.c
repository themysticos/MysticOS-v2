// about.c - SYSPR: окно "About" как ELF-программа (API v2).
#include "../api.h"

int _start(const mystic_api_t* api) {
    int W = 260, H = 150;
    int id = api->window_create("About", 200, 320, W, H);
    if (id < 0) return 1;

    unsigned int bg = 0xE0E0E8;
    api->window_fill(id, 0, 0, W, H, bg);
    api->window_text(id, 10, 10, "MysticOS 2",          0x202040, bg);
    api->window_text(id, 10, 28, "Graphics edition",   0x404060, bg);
    api->window_text(id, 10, 44, "C + NASM, VBE 24bpp", 0x404060, bg);
    api->window_text(id, 10, 70, "SYSPR program (ELF)", 0x006020, bg);
    api->window_text(id, 10, 100, "Press any key / close", 0x808090, bg);

    // Показывать окно и реагировать, пока не нажата клавиша или не закрыто.
    api->yield();
    while (!api->key_available()) {
        api->yield();
        api->sleep(30);
    }
    api->key_getchar();
    api->window_close(id);
    return 0;
}
