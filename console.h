// console.h - текстовый вывод поверх framebuffer
#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);
void console_set_color(unsigned int fg, unsigned int bg);
void console_clear(void);
void console_putc(char c);
void console_print(const char* s);
void console_print_int(int v);
void console_print_hex(unsigned int v);

// Рисует строку в точке (x,y) напрямую (не влияет на курсор консоли).
void draw_text_at(int x, int y, const char* s, unsigned int fg, unsigned int bg);

#endif
