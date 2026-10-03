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

// Картинка (BMP), масштабируется до w x h.
static inline int ms_image(int id, int x, int y, int w, int h, const char* path) {
    return __ms_sc6(SYS_WINDOW_IMAGE, (unsigned int)id, (unsigned int)x,
                    (unsigned int)y, (unsigned int)w, (unsigned int)h,
                    (unsigned int)path);
}

#endif
