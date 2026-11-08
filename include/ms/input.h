// ms/input.h - ввод (клавиатура, мышь) для программ MysticOS.
#ifndef MS_INPUT_H
#define MS_INPUT_H

#include "syscall.h"

// Клавиатура
static inline int ms_key_avail(void)  { return __ms_sc0(SYS_KEY_AVAILABLE); }
static inline int ms_key_get(void)    { return __ms_sc0(SYS_KEY_GETCHAR); }
// Событие клавиши для игр: (pressed<<8)|scancode, или -1 (неблокирующе).
static inline int ms_key_event(void)  { return __ms_sc0(SYS_KEY_EVENT); }

// Мышь
static inline int ms_mouse_x(void)    { return __ms_sc0(SYS_MOUSE_X); }
static inline int ms_mouse_y(void)    { return __ms_sc0(SYS_MOUSE_Y); }
static inline int ms_mouse_left(void) { return __ms_sc0(SYS_MOUSE_LEFT); }

// Игры: захват мыши (курсор ОС стоит) + относительная дельта (свободный разворот).
static inline void ms_mouse_grab(int on) { __ms_sc1(SYS_MOUSE_GRAB, (unsigned int)on); }
// Забрать накопленную дельту. Возвращает 1 если было движение. dx/dy — указатели.
static inline int ms_mouse_rel(int* dx, int* dy) {
    return __ms_sc2(SYS_MOUSE_REL, (unsigned int)dx, (unsigned int)dy);
}

// Событие мыши (неблокирующе): -1 если пусто, иначе упакованное событие.
//   bits 0-11: x; bits 12-23: y; bits 24-27: type (0=move,1=down,2=up);
//   bits 28-31: button (1=левая, 2=правая).
static inline int ms_mouse_event(void) {
    return __ms_sc0(SYS_MOUSE_EVENT);
}

// Разбор события мыши в поля (type: 0/1/2, button: 1/2, x/y — экранные).
static inline void ms_mouse_event_split(int ev, int* type, int* button,
                                        int* x, int* y) {
    if (type)   *type   = (ev >> 24) & 0xF;
    if (button) *button = (ev >> 28) & 0xF;
    if (x)      *x      = ev & 0xFFF;
    if (y)      *y      = (ev >> 12) & 0xFFF;
}

#endif
