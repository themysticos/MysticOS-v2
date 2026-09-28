// console.c - текстовый вывод через framebuffer (шрифт 8x8).
#include "console.h"
#include "vbe.h"
#include "font.h"

#define GLYPH_W 8
#define GLYPH_H 8

static int cx = 0, cy = 0;
static int cols = 0, rows = 0;
static unsigned int fg = 0xFFFFFF, bg = 0x000000;

void console_set_color(unsigned int f, unsigned int b) { fg = f; bg = b; }

static void draw_glyph(int px, int py, char c) {
    const unsigned char* g = font_glyph8x8(c);
    for (int row = 0; row < GLYPH_H; row++) {
        unsigned char bits = g[row];
        for (int col = 0; col < GLYPH_W; col++) {
            int on = (bits & (0x80 >> col)) ? 1 : 0;
            fb_put_pixel(px + col, py + row, on ? fg : bg);
        }
    }
}

void console_init(void) {
    cols = (int)vbe_width()  / GLYPH_W;
    rows = (int)vbe_height() / GLYPH_H;
    cx = cy = 0;
    console_clear();
}

void console_clear(void) {
    fb_clear(bg);
    cx = cy = 0;
}

// Прокрутка вверх на одну текстовую строку.
static void scroll(void) {
    unsigned int pitch = vbe_pitch();
    unsigned int fb = vbe_fb_addr();
    int h = (int)vbe_height();
    int shift = GLYPH_H;
    // Сдвигаем изображение вверх на 8 пикселей (по строкам).
    for (int y = 0; y < h - shift; y++) {
        unsigned char* dst = (unsigned char*)(fb + (unsigned int)y * pitch);
        unsigned char* src = (unsigned char*)(fb + (unsigned int)(y + shift) * pitch);
        for (int x = 0; x < (int)vbe_width() * 3; x++) dst[x] = src[x];
    }
    // Чистим нижнюю строку фоном.
    for (int y = h - shift; y < h; y++) {
        unsigned char* row = (unsigned char*)(fb + (unsigned int)y * pitch);
        unsigned char b = (unsigned char)(bg & 0xFF);
        unsigned char g = (unsigned char)((bg >> 8) & 0xFF);
        unsigned char r = (unsigned char)((bg >> 16) & 0xFF);
        for (int x = 0; x < (int)vbe_width(); x++) {
            row[x*3+0] = b; row[x*3+1] = g; row[x*3+2] = r;
        }
    }
    cy = rows - 1;
}

void console_putc(char c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\r') { cx = 0; }
    else if (c == '\b') {
        if (cx > 0) { cx--; draw_glyph(cx*GLYPH_W, cy*GLYPH_H, ' '); }
    }
    else if (c == '\t') { cx = (cx + 8) & ~7; }
    else {
        draw_glyph(cx*GLYPH_W, cy*GLYPH_H, c);
        cx++;
    }
    if (cx >= cols) { cx = 0; cy++; }
    while (cy >= rows) scroll();
}

void console_print(const char* s) { while (*s) console_putc(*s++); }

void console_print_int(int v) {
    if (v == 0) { console_putc('0'); return; }
    char buf[12]; int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    if (neg) buf[i++] = '-';
    while (i) console_putc(buf[--i]);
}

void console_print_hex(unsigned int v) {
    console_print("0x");
    char h[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) console_putc(h[(v >> i) & 0xF]);
}

// Рисует строку в точке (x,y), используя заданные fg/bg (не трогает курсор).
void draw_text_at(int x, int y, const char* s, unsigned int fg2, unsigned int bg2) {
    const unsigned char* g;
    int px = x;
    while (*s) {
        char c = *s++;
        if (c == '\n') { px = x; y += GLYPH_H; continue; }
        g = font_glyph8x8(c);
        for (int row = 0; row < GLYPH_H; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < GLYPH_W; col++) {
                int on = (bits & (0x80 >> col)) ? 1 : 0;
                fb_put_pixel(px + col, y + row, on ? fg2 : bg2);
            }
        }
        px += GLYPH_W;
    }
}
