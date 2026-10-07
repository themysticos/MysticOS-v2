// painter.c - SYSPR: простой графический редактор (ring3 API).
//
// Возможности:
//   * ЛКМ (зажата) — рисовать текущим цветом;
//   * клик по палитре слева — выбрать цвет;
//   * клавиши 1..8 — быстрый выбор цвета;
//   * 'c' — очистить холст;
//   * Esc — выход.
//
// Демонстрирует: ms_window/ms_fill/ms_pixel/ms_rect/ms_text_bg,
// мышь (ms_mouse_x/y/left), ввод (ms_key_avail/get), ms_yield/ms_sleep.
#include "../include/mysticos.h"

#define WIN_W 520
#define WIN_H 380
#define WIN_X 120
#define WIN_Y 70
#define TITLE_H 20          // высота заголовка окна (см. draw_window)
#define PAL_W  40           // ширина палитры слева

// 8 цветов палитры.
static const unsigned int PAL[8] = {
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00,
    0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF
};
static const char* PAL_NAMES[8] = {
    "black", "white", "red", "green", "blue", "yellow", "magenta", "cyan"
};

static int win_id = -1;
static int cur_color = 0;           // индекс в PAL
static int prev_left = 0;
static int last_draw_x = -1, last_draw_y = -1;

// Нарисовать палитру (слева) и индикатор текущего цвета.
static void draw_palette(void) {
    for (int i = 0; i < 8; i++) {
        int y = i * 24 + 24;
        ms_fill(win_id, 2, y, PAL_W - 4, 22, PAL[i]);
        // Рамка выбранного цвета.
        if (i == cur_color)
            ms_rect(win_id, 1, y - 1, PAL_W - 2, 24, 0xFFFFFF);
    }
}

// Толстая точка (кисть 3x3), чтобы линии были заметны.
static void brush(int x, int y, unsigned int color) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            ms_pixel(win_id, x + dx, y + dy, color);
}

// Линия между двумя точками (алгоритм Брезенхэма) — чтобы при быстром
// движении мыши не было разрывов.
static void line(int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        brush(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

static void clear_canvas(void) {
    // Заливаем только область холста (правее палитры), не трогая палитру.
    ms_fill(win_id, PAL_W, 20, WIN_W - PAL_W, WIN_H - 20, 0xFFFFFF);
}

int _start(void) {
    win_id = ms_window("Painter", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win_id < 0) ms_exit(1);

    // Фон + шапка.
    ms_clear(win_id, 0x303040);
    ms_fill(win_id, 0, 0, WIN_W, 20, 0x505070);
    ms_text_bg(win_id, 4, 6, "Painter - draw with mouse, 1-8=color, c=clear, Esc=exit",
               0xFFFFFF, 0x505070);
    clear_canvas();
    draw_palette();
    ms_yield();

    while (1) {
        // --- Клавиатура ---
        while (ms_key_avail()) {
            int ch = ms_key_get();
            if (ch == 27) { ms_exit(0); }              // Esc
            else if (ch >= '1' && ch <= '8') { cur_color = ch - '1'; draw_palette(); }
            else if (ch == 'c' || ch == 'C') { clear_canvas(); }
        }

        // --- Мышь ---
        int mx = ms_mouse_x(), my = ms_mouse_y();
        int left = ms_mouse_left();
        // Координаты относительно КЛИЕНТСКОЙ области окна (без заголовка).
        int lx = mx - WIN_X;
        int ly = my - WIN_Y - TITLE_H;

        if (left && !prev_left) {
            // Клик: если по палитре — выбрать цвет.
            if (lx >= 0 && lx < PAL_W && ly >= 24 && ly < 24 + 8 * 24) {
                int idx = (ly - 24) / 24;
                if (idx >= 0 && idx < 8) { cur_color = idx; draw_palette(); }
                last_draw_x = -1;
            } else {
                last_draw_x = lx; last_draw_y = ly;
            }
        } else if (left) {
            // Рисование (только в области холста).
            if (lx >= PAL_W && lx < WIN_W && ly >= 20 && ly < WIN_H) {
                if (last_draw_x >= 0)
                    line(last_draw_x, last_draw_y, lx, ly, PAL[cur_color]);
                else
                    brush(lx, ly, PAL[cur_color]);
                last_draw_x = lx; last_draw_y = ly;
            }
        } else {
            last_draw_x = -1;
        }
        prev_left = left;

        ms_yield();
        ms_sleep(15);
    }
    return 0;
}
