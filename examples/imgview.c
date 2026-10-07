// imgview.c - SYSPR: просмотрщик изображений (BMP) как ELF-программа.
// Открывает файл, переданный через run_program_arg -> ms_arg().
// Новый ring3 API (include/mysticos.h), без api->.
#include "../include/mysticos.h"

int _start(void) {
    const char* path = ms_arg();

    int W = 400, H = 320;
    int id = ms_window("Image Viewer", 160, 100, W, H);
    if (id < 0) ms_exit(1);

    unsigned int bg = 0x202028;
    ms_fill(id, 0, 0, W, H, bg);

    if (!path || !path[0]) {
        ms_text_bg(id, 10, 10, "No image selected.", 0xFF8080, bg);
        ms_text_bg(id, 10, 30, "Open a BMP from desktop.", 0xA0A0B0, bg);
    } else {
        ms_text_bg(id, 6, 4, path, 0x80C0FF, bg);
        int iy = 20, ih = H - iy;
        int ok = ms_image(id, 0, iy, W, ih, path);
        if (!ok)
            ms_text_bg(id, 10, iy + 10, "Cannot open (not BMP / missing).", 0xFF8080, bg);
    }

    ms_yield();
    while (!ms_key_avail()) {
        ms_yield();
        ms_sleep(30);
    }
    ms_key_get();
    ms_window_close(id);
    ms_exit(0);
}
