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
int vbe_bpp(void);

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

// read_pixel: прочитать пиксель из backbuffer (0xRRGGBB).
unsigned int fb_backbuffer_read_pixel(int x, int y);

// blend_pixel: смешать ОДИН пиксель с backbuffer по альфе (0..255).
// Нужно для мягких теней и скруглённых углов.
void fb_backbuffer_blend_pixel(int x, int y, unsigned int color, unsigned int alpha);
// shade_rect: наложить цвет с альфой на прямоугольник (мягкие тени).
void fb_backbuffer_shade_rect(int x, int y, int w, int h, unsigned int color, unsigned int alpha);
// round_rect_alpha: скруглённый прямоугольник с общей альфой и
// сглаженными углами (единый примитив для тени/окна/меню).
void fb_backbuffer_round_rect_alpha(int x, int y, int w, int h, int radius,
                                    unsigned int color, unsigned int alpha);
// Быстрый блит буфера (24bpp) в backbuffer со сдвигом и обрезкой.
// stride — длина строки источника в байтах.
void fb_backbuffer_blit(int dx, int dy, const unsigned char* src, int w, int h, int stride);
// Блит с "прозрачностью": пиксели, чей цвет (BGR) == key, НЕ копируются —
// сквозь них виден нижний слой backbuffer. key задаётся как 0xRRGGBB.
void fb_backbuffer_blit_key(int dx, int dy, const unsigned char* src, int w, int h, int stride, unsigned int key);

// Блит с ПОПИКСЕЛЬНОЙ альфой (композитинг): src смешивается с тем, что
// уже в backbuffer. mask — 1 байт на пиксель (0..255), может быть 0
// (тогда всё считается непрозрачным). Позволяет полупрозрачные окна,
// скруглённые углы, тени и т.п.
void fb_backbuffer_blit_alpha(int dx, int dy,
                              const unsigned char* src,
                              const unsigned char* mask,
                              int w, int h, int src_stride, int mask_stride);

// blit_key_alpha: key-прозрачность + общая альфа окна.
// Пиксели цвета key НЕ рисуются. Остальные смешиваются с backbuffer
// по alpha (0..255). При alpha == 255 — обычный blit_key.
void fb_backbuffer_blit_key_alpha(int dx, int dy, const unsigned char* src,
                                  int w, int h, int stride, unsigned int key,
                                  unsigned int alpha);
void fb_flip(void);
void fb_flip_rect(int x, int y, int w, int h);

// Сменить текущий backbuffer (для композитинга в отдельный буфер,
// например bg_cache). Возвращает старый — чтобы восстановить.
unsigned char* fb_backbuffer_set_target(unsigned char* new_buf);

#endif
