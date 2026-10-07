// pic.h - PIC, PIT, клавиатура (IRQ1), мышь (IRQ12)
#ifndef PIC_H
#define PIC_H

#include "idt.h"

void pic_init(void);
void pit_init(unsigned int hz);
void interrupts_enable(void);
void irq_handler(registers_t* r);

// Таймер
unsigned int timer_ticks(void);

// Клавиатура (неблокирующая)
int  kb_available(void);
int  kb_getchar(void);

// Мышь
void mouse_init(void);
int  mouse_x(void);
int  mouse_y(void);
int  mouse_left(void);
int  mouse_right(void);
// Колбэк движения мыши (вызывается из IRQ12 с приращением dx,dy).
void mouse_set_move_hook(void (*hook)(int dx, int dy));

// Захват мыши (для игр): курсор ОС стоит, дельта копится для игры.
void mouse_set_grab(int on);
int  mouse_grabbed(void);
void mouse_take_rel(int* dx, int* dy);

// Клавиатура: очередь событий (нажатие/отпускание) для игр.
void kb_push_event(int pressed, unsigned char sc);
int  kb_event_available(void);
int  kb_get_event(void);

#endif
