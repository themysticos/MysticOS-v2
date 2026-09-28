// kprint.c - совместимость print_* для кода ФС.
// Переключаемо: вывод в графическую консоль ИЛИ в терминал (для ls/cat).
#include "console.h"
#include "term.h"

int g_out_to_term = 0;

void put_char(char c) {
    if (g_out_to_term) term_putc(c); else console_putc(c);
}
void print_string(const char* s) {
    if (g_out_to_term) term_print(s); else console_print(s);
}
void print_int(int v) {
    if (g_out_to_term) term_print_int(v); else console_print_int(v);
}
void print_hex(unsigned int v) {
    if (g_out_to_term) term_print_hex(v); else console_print_hex(v);
}
