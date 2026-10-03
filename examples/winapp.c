// winapp.c - ELF-программа: окно, анимация и СТАТИЧНЫЙ текст.
//
// Демонстрирует ms_static_text: текст лежит в нижнем слое окна, анимация
// его не затирает. Буфер окна залит MS_TRANSPARENT — сквозь него виден
// статичный текст; квадрат его накрывает, но не портит.
#include "../include/mysticos.h"

int _start(void) {
    int id = ms_window("Hello", 150, 120, 220, 160);
    if (id < 0) ms_exit(1);

    // Фон — "прозрачный", чтобы видеть статичный текст под анимацией.
    ms_fill(id, 0, 0, 220, 160, MS_TRANSPARENT);
    // Рамка.
    ms_rect(id, 0, 0, 220, 160, 0x80C0FF);

    // СТАТИЧНЫЙ текст — в нижний слой, анимация его не затрёт.
    ms_static_text(id, 10, 10,  "Hello from ELF!",          MS_WHITE);
    ms_static_text(id, 10, 30,  "MysticOS 2 static text",   0xC0FFC0);
    ms_static_text(id, 10, 120, "Press any key to exit.",   0xE0E0E0);

    ms_yield();

    // Анимация: двигаем квадрат, пока не нажата клавиша.
    // Стираем ТОЛЬКО след квадрата (MS_TRANSPARENT).
    int px = 10, dir = 1;
    while (!ms_key_avail()) {
        ms_fill(id, px, 90, 30, 30, MS_TRANSPARENT);   // стереть след
        px += dir * 4;
        if (px < 5 || px > 180) dir = -dir;
        ms_fill(id, px, 90, 30, 30, MS_ORANGE);        // новый квадрат
        ms_yield();
        ms_sleep(30);
    }
    ms_key_get();
    ms_exit(42);
}
