// vbe.h - драйвер линейного framebuffer (VBE, 24bpp)
#ifndef VBE_H
#define VBE_H

#define FB_RGB(r, g, b) (((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))

// Инициализация: читает параметры VBE, сохранённые загрузчиком (0x9000).
// Возвращает 1, если framebuffer доступен.
int vbe_init(void);

unsigned int vbe_fb_addr(void);
unsigned int vbe_width(void);
unsigned int vbe_height(void);
unsigned int vbe_pitch(void);
int vbe_active(void);

// Примитивы.
void fb_put_pixel(int x, int y, unsigned int color);
void fb_fill_rect(int x, int y, int w, int h, unsigned int color);
void fb_clear(unsigned int color);
void fb_draw_line(int x0, int y0, int x1, int y1, unsigned int color);
void fb_draw_rect(int x, int y, int w, int h, unsigned int color);

// Двойная буферизация: off-screen буфер и вывод.
int  fb_backbuffer_init(void);
void fb_backbuffer_clear(unsigned int color);
void fb_backbuffer_pixel(int x, int y, unsigned int color);
void fb_backbuffer_fill(int x, int y, int w, int h, unsigned int color);
void fb_backbuffer_line(int x0, int y0, int x1, int y1, unsigned int color);
void fb_backbuffer_rect(int x, int y, int w, int h, unsigned int color);
// Быстрый блит буфера (24bpp) в backbuffer со сдвигом и обрезкой.
// stride — длина строки источника в байтах.
void fb_backbuffer_blit(int dx, int dy, const unsigned char* src, int w, int h, int stride);
void fb_flip(void);
void fb_flip_rect(int x, int y, int w, int h);

#endif
