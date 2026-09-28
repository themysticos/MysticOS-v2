// cursor.c - курсор-стрелка 12x19 с сохранением фона под ним.
#include "cursor.h"
#include "vbe.h"

#define CUR_W 12
#define CUR_H 19

// Битовая маска стрелки (1 = рисуем, 2 = чёрный контур).
static const unsigned char CUR[CUR_H][CUR_W] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,1,1,1,1,2,0},
    {2,1,1,1,1,1,1,2,2,2,2,2},
    {2,1,1,1,2,1,1,2,0,0,0,0},
    {2,1,1,2,0,2,1,1,2,0,0,0},
    {2,1,2,0,0,2,1,1,2,0,0,0},
    {2,2,0,0,0,0,2,1,1,2,0,0},
    {0,0,0,0,0,0,2,1,1,2,0,0},
    {0,0,0,0,0,0,0,2,1,2,0,0},
    {0,0,0,0,0,0,0,2,2,0,0,0}
};

static int cur_x = 0, cur_y = 0;
static int shown = 0;
static unsigned int saved[CUR_H][CUR_W];

static unsigned int get_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= (int)vbe_width() || y >= (int)vbe_height()) return 0;
    unsigned char* p = (unsigned char*)(vbe_fb_addr() + (unsigned int)y * vbe_pitch() + (unsigned int)x * 3u);
    return ((unsigned int)p[2] << 16) | ((unsigned int)p[1] << 8) | p[0];
}

void cursor_reset(void) { shown = 0; }

void cursor_hide(void) {
    if (!shown) return;
    for (int row = 0; row < CUR_H; row++)
        for (int col = 0; col < CUR_W; col++)
            fb_put_pixel(cur_x + col, cur_y + row, saved[row][col]);
    shown = 0;
}

void cursor_show_at(int x, int y) {
    cur_x = x; cur_y = y;
    for (int row = 0; row < CUR_H; row++) {
        for (int col = 0; col < CUR_W; col++) {
            saved[row][col] = get_pixel(x + col, y + row);
            unsigned char m = CUR[row][col];
            if (m == 1) fb_put_pixel(x + col, y + row, 0xFFFFFF);      // белая заливка
            else if (m == 2) fb_put_pixel(x + col, y + row, 0x000000); // чёрный контур
        }
    }
    shown = 1;
}
