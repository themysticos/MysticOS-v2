// imgview.c - SYSPR: просмотрщик изображений (BMP) как ELF-программа.
// Открывает файл, переданный через run_program_arg -> get_arg().
#include "../api.h"

int _start(const mystic_api_t* api) {
    const char* path = api->get_arg ? api->get_arg() : "";

    int W = 400, H = 320;
    int id = api->window_create("Image Viewer", 160, 100, W, H);
    if (id < 0) return 1;

    unsigned int bg = 0x202028;
    api->window_fill(id, 0, 0, W, H, bg);

    if (!path || !path[0]) {
        api->window_text(id, 10, 10, "No image selected.", 0xFF8080, bg);
        api->window_text(id, 10, 30, "Open a BMP from desktop.", 0xA0A0B0, bg);
    } else {
        // Шапка с именем файла.
        api->window_text(id, 6, 4, path, 0x80C0FF, bg);
        // Картинка: область под шапкой (высота шапки ~16px).
        int iy = 20, ih = H - iy;
        int ok = api->window_image ? api->window_image(id, 0, iy, W, ih, path) : 0;
        if (!ok) {
            api->window_text(id, 10, iy + 10, "Cannot open (not BMP / missing).", 0xFF8080, bg);
        }
    }

    api->yield();
    // Ждём клавишу или закрытие окна.
    while (!api->key_available()) {
        api->yield();
        api->sleep(30);
    }
    api->key_getchar();
    api->window_close(id);
    return 0;
}
