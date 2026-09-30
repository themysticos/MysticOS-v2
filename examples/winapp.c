// winapp.c - тестовая ELF-программа: окно, анимация и СТАТИЧНЫЙ текст.
//
// Демонстрирует window_static_text: текст лежит в нижнем слое окна и его
// не затирает анимация (двигающийся квадрат). Фон окна залит
// WIN_TRANSPARENT — сквозь него виден текст; квадрат (обычный цвет) его
// накрывает, но не портит.
#include "../api.h"

int _start(const mystic_api_t* api) {
    int id = api->window_create("Hello", 150, 120, 220, 160);
    if (id < 0) { api->print_string("window_create failed\n"); return 1; }

    // Фон — "прозрачный", чтобы видеть статичный текст под анимацией.
    api->window_fill(id, 0, 0, 220, 160, WIN_TRANSPARENT);
    // Рамка.
    api->window_rect(id, 0, 0, 220, 160, 0x80C0FF);

    // СТАТИЧНЫЙ текст — в нижний слой, анимация его не затрёт.
    api->window_static_text(id, 10, 10, "Hello from ELF!", 0xFFFFFF);
    api->window_static_text(id, 10, 30, "MysticOS 2 static text", 0xC0FFC0);
    api->window_static_text(id, 10, 120, "Press any key to exit.", 0xE0E0E0);

    api->yield();

    // Анимация: двигаем квадрат, пока не нажата клавиша или не закрыто окно.
    // Стираем ТОЛЬКО след квадрата (WIN_TRANSPARENT) — не весь буфер!
    int px = 10, dir = 1;
    while (!api->key_available()) {
        api->window_fill(id, px, 90, 30, 30, WIN_TRANSPARENT);  // стереть след
        px += dir * 4;
        if (px < 5 || px > 180) dir = -dir;
        api->window_fill(id, px, 90, 30, 30, 0xFF8000);        // новый квадрат
        api->yield();
        api->sleep(30);
    }
    api->key_getchar();
    return 42;
}
