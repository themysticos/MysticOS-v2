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

#endif
