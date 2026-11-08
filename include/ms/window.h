// ms/window.h - оконное API для программ MysticOS (без api->).
//
// Заменяет старый стиль api->window_create(...) на прямой ms_window_create(...).
// Внутри всё идёт через int 0x80 (см. ms/syscall.h).
#ifndef MS_WINDOW_H
#define MS_WINDOW_H

#include "syscall.h"

// Создать окно. Возвращает id (>=0) или -1.
static inline int ms_window(const char* title, int x, int y, int w, int h) {
    return __ms_sc5(SYS_WINDOW_CREATE,
                    (unsigned int)title, (unsigned int)x, (unsigned int)y,
                    (unsigned int)w, (unsigned int)h);
}

// Создать окно с типом (WIN_KIND_PROGRAM/WIN_KIND_SYSTEM).
static inline int ms_window_ex(const char* title, int x, int y, int w, int h, int kind) {
    return __ms_sc6(SYS_WINDOW_CREATE_EX,
                    (unsigned int)title, (unsigned int)x, (unsigned int)y,
                    (unsigned int)w, (unsigned int)h, (unsigned int)kind);
}

// Закрыть окно (обычно не нужно: exit закрывает всё).
static inline void ms_window_close(int id) {
    __ms_sc1(SYS_WINDOW_CLOSE, (unsigned int)id);
}

// Показать кадр (в конце каждой итерации цикла).
static inline void ms_yield(void) {
    __ms_sc0(SYS_YIELD);
}

// ============================================================
// РИСОВАНИЕ
// ============================================================

// Один пиксель.
static inline void ms_pixel(int id, int x, int y, unsigned int color) {
    __ms_sc4(SYS_WINDOW_PIXEL, (unsigned int)id, (unsigned int)x,
             (unsigned int)y, color);
}

// Прямоугольник (рамка).
static inline void ms_rect(int id, int x, int y, int w, int h, unsigned int color) {
    __ms_sc6(SYS_WINDOW_RECT, (unsigned int)id, (unsigned int)x,
             (unsigned int)y, (unsigned int)w, (unsigned int)h, color);
}

// Заливка.
static inline void ms_fill(int id, int x, int y, int w, int h, unsigned int color) {
    __ms_sc6(SYS_WINDOW_FILL, (unsigned int)id, (unsigned int)x,
             (unsigned int)y, (unsigned int)w, (unsigned int)h, color);
}

// Залить всё окно одним цветом.
static inline void ms_clear(int id, unsigned int color) {
    __ms_sc2(SYS_WINDOW_CLEAR, (unsigned int)id, color);
}

// Текст с фоном.
static inline void ms_text_bg(int id, int x, int y, const char* s,
                              unsigned int fg, unsigned int bg) {
    __ms_sc6(SYS_WINDOW_TEXT, (unsigned int)id, (unsigned int)x,
             (unsigned int)y, (unsigned int)s, fg, bg);
}

// Текст без фона (на прозрачном фоне — быстрее, часто то что нужно).
static inline void ms_text(int id, int x, int y, const char* s, unsigned int fg) {
    __ms_sc6(SYS_WINDOW_TEXT, (unsigned int)id, (unsigned int)x,
             (unsigned int)y, (unsigned int)s, fg, MS_TRANSPARENT);
}

// Статичный текст в НИЖНИЙ слой окна: анимация в buf его не затирает.
// Где buf залит MS_TRANSPARENT — виден этот текст.
static inline void ms_static_text(int id, int x, int y, const char* s, unsigned int fg) {
    __ms_sc5(SYS_WINDOW_STATIC_TEXT, (unsigned int)id, (unsigned int)x,
             (unsigned int)y, (unsigned int)s, fg);
}

// Очистить нижний (статичный) слой окна.
static inline void ms_static_clear(int id) {
    __ms_sc1(SYS_WINDOW_STATIC_CLEAR, (unsigned int)id);
}

// Быстрый blit кадра: src — 32bpp ARGB (как DG_ScreenBuffer), w x h.
// Копирует в окно с конвертацией 32->24 и масштабом под клиентскую область.
// Для игр (Doom 640x400). Возвращает 0.
static inline int ms_frame(int id, const unsigned int* src, int w, int h) {
    return __ms_sc4(SYS_WINDOW_FRAME, (unsigned int)id, (unsigned int)src,
                    (unsigned int)w, (unsigned int)h);
}

// Размер КЛИЕНТСКОЙ области окна (без заголовка) как (w<<16)|h.
static inline int ms_window_client(int id) {
    return __ms_sc1(SYS_WINDOW_CLIENT, (unsigned int)id);
}

// Текст с масштабом глифа (для заголовков). scale 1..4.
static inline void ms_text_scaled(int id, int x, int y, const char* s, unsigned int fg, int scale) {
    __ms_sc6(SYS_WINDOW_TEXT_SCALED, (unsigned int)id, (unsigned int)x,
             (unsigned int)y, (unsigned int)s, fg, (unsigned int)scale);
}

// Нарисовать картинку из ПАМЯТИ (BMP/GIF/PNG) в окно. 1 = успех, 0 = ошибка.
static inline int ms_image_mem(int id, int x, int y, int w, int h, const void* ptr, unsigned int size) {
    return __ms_sc7(SYS_WINDOW_IMAGE_MEM, (unsigned int)id, (unsigned int)x,
                    (unsigned int)y, (unsigned int)w, (unsigned int)h,
                    (unsigned int)ptr, size);
}

// Позиция КЛИЕНТСКОЙ области окна в экране как (x<<16)|y.
// Нужна для пересчёта абсолютных координат мыши при maximize/перемещении.
static inline int ms_window_pos(int id) {
    return __ms_sc1(SYS_WINDOW_POS, (unsigned int)id);
}

// Картинка (BMP), масштабируется до w x h.
static inline int ms_image(int id, int x, int y, int w, int h, const char* path) {
    return __ms_sc6(SYS_WINDOW_IMAGE, (unsigned int)id, (unsigned int)x,
                    (unsigned int)y, (unsigned int)w, (unsigned int)h,
                    (unsigned int)path);
}

#endif
