// statictest.c - СТАТИЧНЫЙ текст, который переживает анимацию.
//
// Модель: текст лежит в НИЖНЕМ слое окна (window_static_text). Анимация
// рисуется в буфере окна ПОВЕРХ. Фон окна заливается WIN_TRANSPARENT —
// этот цвет НЕ рисуется, сквозь него виден текст. Где нарисован квадрат
// (обычный цвет) — он текст накрывает.
//
// Т.е.: квадрат наезжает -> накрывает текст; уехал -> текст снова виден
// ЦЕЛЫМ (его никто не стирал).
//
// Выход: любая клавиша или закрытие окна.
#include "../api.h"

int _start(const mystic_api_t* api) {
    int id = api->window_create("Static text test", 150, 100, 240, 170);
    if (id < 0) { api->print_string("window_create failed\n"); return 1; }

    // Фон окна (buf) — "прозрачный": сквозь него виден статичный текст.
    api->window_fill(id, 0, 0, 240, 170, WIN_TRANSPARENT);

    // Статичный текст — в НИЖНИЙ слой (его анимация не сотрёт).
    api->window_static_text(id, 10, 30, "STATIC text (bottom layer)", 0x80FF80);
    api->window_static_text(id, 10, 45, "The square covers it,", 0x80FF80);
    api->window_static_text(id, 10, 60, "but it stays INTACT!", 0x80FF80);

    api->yield();

    // Анимация: квадрат бегает по всей высоте, проходя СКВОЗЬ текст.
    // Стираем только след квадрата (заливаем его WIN_TRANSPARENT).
    int px = 10, py = 2, dx = 3, dy = 2;
    while (!api->key_available()) {
        api->window_fill(id, px, py, 40, 40, WIN_TRANSPARENT);  // стереть след
        px += dx; py += dy;
        if (px < 2 || px > 198) dx = -dx;
        if (py < 2 || py > 130) dy = -dy;
        api->window_fill(id, px, py, 40, 40, 0xFF8000);         // новый квадрат
        api->yield();
        api->sleep(20);
    }
    api->key_getchar();
    return 0;
}
