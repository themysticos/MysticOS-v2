// about.c - SYSPR: окно "About" как ELF-программа.
// RING3 + int 0x80: api получаем через ms_api(), завершаемся через api->exit.
#include "../api.h"
extern const mystic_api_t* ms_api(void);

int _start(void) {
    const mystic_api_t* api = ms_api();
    int W = 260, H = 150;
    int id = api->window_create("About", 200, 320, W, H);
    if (id < 0) api->exit(1);   // ring3: не return, а exit через int 0x80

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
    api->exit(0);   // ring3: return невозможен, только exit
}
