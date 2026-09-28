// term.c - терминал в окне. Хранит буфер символов, рисует в backbuffer.
#include "term.h"
#include "vbe.h"
#include "font.h"

#define GW 8
#define GH 8
#define MAX_COLS 128
#define MAX_ROWS 64

static int ax = 0, ay = 0, aw = 0, ah = 0;   // область окна (пиксели)
static int cols = 0, rows = 0;               // размер сетки (символы)
static int cx = 0, cy = 0;                   // курсор (символы)
static unsigned int fg = 0xC0FFC0, bg = 0x101018;

static char buf[MAX_ROWS][MAX_COLS];

static int geom_ready = 0;

void term_set_area(int x, int y, int w, int h) {
    ax = x; ay = y; aw = w; ah = h;
    cols = w / GW; if (cols > MAX_COLS) cols = MAX_COLS;
    rows = h / GH; if (rows > MAX_ROWS) rows = MAX_ROWS;
    // Очищаем буфер ТОЛЬКО один раз — иначе текст стирается каждый кадр.
    if (!geom_ready) {
        for (int r = 0; r < MAX_ROWS; r++) for (int c = 0; c < MAX_COLS; c++) buf[r][c] = ' ';
        cx = cy = 0;
        geom_ready = 1;
    }
}

void term_clear(void) {
    for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) buf[r][c] = ' ';
    cx = cy = 0;
}

// Рисует ВСЁ содержимое буфера в backbuffer (вызывается при отрисовке окна).
static void draw_glyph(int px, int py, char c) {
    const unsigned char* g = font_glyph8x8(c);
    for (int row = 0; row < GH; row++) {
        unsigned char bits = g[row];
        for (int col = 0; col < GW; col++) {
            int on = (bits & (0x80 >> col)) ? 1 : 0;
            fb_backbuffer_pixel(px + col, py + row, on ? fg : bg);
        }
    }
}

void term_render(void);   // forward

void term_render(void) {
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            draw_glyph(ax + c*GW, ay + r*GH, buf[r][c]);
}

static void scroll_up(void) {
    for (int r = 1; r < rows; r++)
        for (int c = 0; c < cols; c++) buf[r-1][c] = buf[r][c];
    for (int c = 0; c < cols; c++) buf[rows-1][c] = ' ';
    cy = rows - 1;
}

void term_putc(char c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\r') { cx = 0; }
    else if (c == '\b') { if (cx > 0) { cx--; buf[cy][cx] = ' '; } }
    else if (c == '\t') { cx = (cx + 4) & ~3; }
    else { if (cx < cols) buf[cy][cx] = c; cx++; }
    if (cx >= cols) { cx = 0; cy++; }
    while (cy >= rows) scroll_up();
}

void term_print(const char* s) { while (*s) term_putc(*s++); }

void term_print_int(int v) {
    if (v == 0) { term_putc('0'); return; }
    char b[12]; int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    if (neg) b[i++] = '-';
    while (i) term_putc(b[--i]);
}

void term_newline(void) { term_putc('\n'); }

void term_print_hex(unsigned int v) {
    term_print("0x");
    char h[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) term_putc(h[(v >> i) & 0xF]);
}

// Позиция курсора ввода в пикселях (левый верхний угол знакоместа).
void term_cursor_px(int* px, int* py) {
    *px = ax + cx * GW;
    *py = ay + cy * GH;
}

// Блок-курсор: on=1 — залить знакоместо инверсным цветом (bg), on=0 — стереть
// (перерисовать символ обычными цветами). Рисует прямо в backbuffer поверх
// содержимого окна. Используется для мигания, когда окно активно.
void term_draw_cursor(int on) {
    int px = ax + cx * GW, py = ay + cy * GH;
    if (on) {
        // Сплошной блок цвета текста.
        for (int row = 0; row < GH; row++)
            for (int col = 0; col < GW; col++)
                fb_backbuffer_pixel(px + col, py + row, fg);
    } else {
        // Стереть: перерисовать символ (или пустое знакоместо) обычными цветами.
        char ch = (cy >= 0 && cy < rows && cx >= 0 && cx < cols) ? buf[cy][cx] : ' ';
        draw_glyph(px, py, ch);
    }
}
