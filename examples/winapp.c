// winapp.c - тестовая ELF-программа: создаёт окно и рисует.
#include "../api.h"

int _start(const mystic_api_t* api) {
    int id = api->window_create("Hello", 150, 120, 220, 160);
    if (id < 0) { api->print_string("window_create failed\n"); return 1; }

    // Фон
    api->window_fill(id, 0, 0, 220, 160, 0x202840);
    // Рамка
    api->window_rect(id, 0, 0, 220, 160, 0x80C0FF);
    // Текст
    api->window_text(id, 10, 10, "Hello from ELF!", 0xFFFFFF, 0x202840);
    api->window_text(id, 10, 30, "MysticOS 2 window API", 0xC0FFC0, 0x202840);

    // Цветные квадраты
    api->window_fill(id, 10,  60, 40, 40, 0xFF4040);
    api->window_fill(id, 60,  60, 40, 40, 0x40FF40);
    api->window_fill(id, 110, 60, 40, 40, 0x4040FF);
    api->window_fill(id, 160, 60, 40, 40, 0xFFFF40);

    api->window_text(id, 10, 120, "Press any key to exit.", 0xE0E0E0, 0x202840);
    api->yield();   // показать окно + обслужить события

    // Анимация: двигаем квадрат, пока не нажата клавиша или не закрыто окно.
    int px = 10, dir = 1;
    while (!api->key_available()) {
        api->window_fill(id, px, 100, 30, 30, 0x202840);   // стереть старый
        px += dir * 4;
        if (px < 5 || px > 180) dir = -dir;
        api->window_fill(id, px, 100, 30, 30, 0xFF8000);   // нарисовать новый
        api->yield();
        api->sleep(30);   // ~33 FPS
    }
    api->key_getchar();

    return 42;
}
