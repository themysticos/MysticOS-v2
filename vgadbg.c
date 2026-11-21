// vgadbg.c - минимальный текстовый вывод в VGA-текст (0xB8000, 80x25).
// См. vgadbg.h. Никаких зависимостей: только запись в память 0xB8000.
#include "vgadbg.h"

#define VGA_MEM   ((volatile unsigned short*)0xB8000u)
#define VGA_COLS  80
#define VGA_ROWS  25

static int g_col = 0;
static int g_row = 0;
static unsigned char g_attr = 0x07;   // светло-серый на чёрном

void vga_set_attr(unsigned char attr) { g_attr = attr; }

void vga_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEM[i] = ((unsigned short)g_attr << 8) | ' ';
    g_col = 0;
    g_row = 0;
}

void vga_set_pos(int col, int row) {
    if (col < 0) col = 0;
    if (col >= VGA_COLS) col = VGA_COLS - 1;
    if (row < 0) row = 0;
    if (row >= VGA_ROWS) row = VGA_ROWS - 1;
    g_col = col; g_row = row;
}

// Прокрутка на одну строку вверх при переполнении.
static void vga_scroll(void) {
    for (int i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++)
        VGA_MEM[i] = VGA_MEM[i + VGA_COLS];
    for (int i = 0; i < VGA_COLS; i++)
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + i] = ((unsigned short)g_attr << 8) | ' ';
    g_row = VGA_ROWS - 1;
}

static void vga_putc(char c) {
    if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\r') {
        g_col = 0;
    } else if (c == '\t') {
        g_col = (g_col + 8) & ~7;
        if (g_col >= VGA_COLS) { g_col = 0; g_row++; }
    } else {
        VGA_MEM[g_row * VGA_COLS + g_col] = ((unsigned short)g_attr << 8) | (unsigned char)c;
        g_col++;
        if (g_col >= VGA_COLS) { g_col = 0; g_row++; }
    }
    if (g_row >= VGA_ROWS) vga_scroll();
}

void vga_print(const char* s) {
    while (s && *s) vga_putc(*s++);
}

void vga_print_at(int x, int y, const char* s) {
    if (y < 0 || y >= VGA_ROWS) return;
    if (x < 0) x = 0;
    int i = 0;
    while (s && s[i] && (x + i) < VGA_COLS) {
        char c = s[i];
        if (c == '\n') break;
        VGA_MEM[y * VGA_COLS + (x + i)] = ((unsigned short)g_attr << 8) | (unsigned char)c;
        i++;
    }
}

void vga_print_int(int v) {
    char buf[12];
    int n = 0;
    unsigned int u;
    if (v < 0) { vga_putc('-'); u = (unsigned int)(-(long)v); }
    else u = (unsigned int)v;
    if (u == 0) { vga_putc('0'); return; }
    while (u && n < (int)sizeof(buf)) { buf[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n--) vga_putc(buf[n]);
}

void vga_print_hex(unsigned int v) {
    static const char hexd[] = "0123456789ABCDEF";
    vga_print("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putc(hexd[(v >> shift) & 0xF]);
}
