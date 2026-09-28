// term.h - текстовый терминал внутри окна (рисует в backbuffer)
#ifndef TERM_H
#define TERM_H

void term_set_area(int x, int y, int w, int h);  // область окна
void term_clear(void);
void term_putc(char c);
void term_print(const char* s);
void term_print_int(int v);
void term_newline(void);
void term_render(void);
void term_print_hex(unsigned int v);

// Мигающий курсор терминала: рисуется поверх содержимого.
// on=1 — показать блок курсора на текущей позиции ввода, on=0 — стереть.
void term_draw_cursor(int on);
// Позиция курсора ввода в пикселях (левый верхний угол знакоместа).
void term_cursor_px(int* px, int* py);

#endif
