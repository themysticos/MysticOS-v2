// ms/input.h - ввод (клавиатура, мышь) для программ MysticOS.
#ifndef MS_INPUT_H
#define MS_INPUT_H

#include "syscall.h"

// Клавиатура
static inline int ms_key_avail(void)  { return __ms_sc0(SYS_KEY_AVAILABLE); }
static inline int ms_key_get(void)    { return __ms_sc0(SYS_KEY_GETCHAR); }

// Мышь
static inline int ms_mouse_x(void)    { return __ms_sc0(SYS_MOUSE_X); }
static inline int ms_mouse_y(void)    { return __ms_sc0(SYS_MOUSE_Y); }
static inline int ms_mouse_left(void) { return __ms_sc0(SYS_MOUSE_LEFT); }

#endif
