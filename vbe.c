// vbe.c - линейный framebuffer (VBE 24bpp) + примитивы рисования.
#include "vbe.h"

// Параметры VBE, сохранённые загрузчиком по адресу 0x9000:
//   0x9000: dword fb_addr | 0x9004: word pitch | 0x9006: w | 0x9008: h
#define VBE_PARAM_ADDR 0x9000u

static unsigned int g_fb = 0;
static unsigned int g_pitch = 0;
static int g_w = 0, g_h = 0;
static int g_active = 0;

unsigned int vbe_fb_addr(void) { return g_fb; }
unsigned int vbe_width(void) { return (unsigned int)g_w; }
unsigned int vbe_height(void) { return (unsigned int)g_h; }
unsigned int vbe_pitch(void) { return g_pitch; }
int vbe_active(void) { return g_active; }

int vbe_init(void) {
    volatile unsigned int* p = (volatile unsigned int*)VBE_PARAM_ADDR;
    unsigned int fb = p[0];
    if (fb == 0) { g_active = 0; return 0; }
    g_fb = fb;
    g_pitch = (unsigned int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 4));
    g_w = (int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 6));
    g_h = (int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 8));
    g_active = 1;
    return 1;
}

// Пиксель (24bpp: 3 байта B,G,R).
void fb_put_pixel(int x, int y, unsigned int color) {
    if (!g_active) return;
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    unsigned char* p = (unsigned char*)(g_fb + (unsigned int)y * g_pitch + (unsigned int)x * 3u);
    p[0] = (unsigned char)(color & 0xFF);
    p[1] = (unsigned char)((color >> 8) & 0xFF);
    p[2] = (unsigned char)((color >> 16) & 0xFF);
}

void fb_fill_rect(int x, int y, int w, int h, unsigned int color) {
    if (!g_active) return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            fb_put_pixel(xx, yy, color);
}

void fb_clear(unsigned int color) {
    if (!g_active) return;
    unsigned char b = (unsigned char)(color & 0xFF);
    unsigned char g = (unsigned char)((color >> 8) & 0xFF);
    unsigned char r = (unsigned char)((color >> 16) & 0xFF);
    for (int y = 0; y < g_h; y++) {
        unsigned char* row = (unsigned char*)(g_fb + (unsigned int)y * g_pitch);
        for (int x = 0; x < g_w; x++) {
            row[x*3+0] = b;
            row[x*3+1] = g;
            row[x*3+2] = r;
        }
    }
}

// Линия (алгоритм Брезенхэма).
void fb_draw_line(int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        fb_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void fb_draw_rect(int x, int y, int w, int h, unsigned int color) {
    fb_draw_line(x, y, x + w - 1, y, color);
    fb_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    fb_draw_line(x, y, x, y + h - 1, color);
    fb_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

// ============================================================
// Двойная буферизация
// ============================================================
// Backbuffer в верхней RAM (16 МБ), формат — тот же 24bpp, pitch = w*3.
#define BB_ADDR 0x01000000u

static unsigned char* g_bb = 0;

int fb_backbuffer_init(void) {
    if (!g_active) return 0;
    g_bb = (unsigned char*)BB_ADDR;
    return 1;
}

void fb_backbuffer_clear(unsigned int color) {
    if (!g_bb) return;
    // Заполняем по одному пикселю (3 байта). Цвет уже в формате 0xRRGGBB.
    unsigned char b = (unsigned char)(color & 0xFF);
    unsigned char g = (unsigned char)((color >> 8) & 0xFF);
    unsigned char r = (unsigned char)((color >> 16) & 0xFF);
    for (int y = 0; y < g_h; y++) {
        unsigned char* row = g_bb + (unsigned int)y * (unsigned int)g_w * 3u;
        for (int x = 0; x < g_w; x++) {
            row[0] = b; row[1] = g; row[2] = r;
            row += 3;
        }
    }
}

void fb_backbuffer_pixel(int x, int y, unsigned int color) {
    if (!g_bb) return;
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    unsigned char* p = g_bb + ((unsigned int)y * (unsigned int)g_w + (unsigned int)x) * 3u;
    p[0] = (unsigned char)(color & 0xFF);
    p[1] = (unsigned char)((color >> 8) & 0xFF);
    p[2] = (unsigned char)((color >> 16) & 0xFF);
}

void fb_backbuffer_fill(int x, int y, int w, int h, unsigned int color) {
    if (!g_bb) return;
    if (w <= 0 || h <= 0) return;
    // Обрезаем по экрану.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    if (w <= 0 || h <= 0) return;
    unsigned char b = (unsigned char)(color & 0xFF);
    unsigned char g = (unsigned char)((color >> 8) & 0xFF);
    unsigned char r = (unsigned char)((color >> 16) & 0xFF);
    for (int yy = y; yy < y + h; yy++) {
        unsigned char* row = g_bb + ((unsigned int)yy * (unsigned int)g_w + (unsigned int)x) * 3u;
        for (int xx = 0; xx < w; xx++) {
            row[0] = b; row[1] = g; row[2] = r;
            row += 3;
        }
    }
}

void fb_backbuffer_line(int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        fb_backbuffer_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

// Быстрый блит 24bpp-буфера в backbuffer: построчно, побайтово, с обрезкой.
void fb_backbuffer_blit(int dx, int dy, const unsigned char* src, int w, int h, int stride) {
    if (!g_bb) return;
    int sy0 = 0, sx0 = 0;
    if (dx < 0) { sx0 = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; h += dy; dy = 0; }
    if (dx + w > g_w) w = g_w - dx;
    if (dy + h > g_h) h = g_h - dy;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        unsigned char* d = g_bb + ((unsigned int)(dy + y) * (unsigned int)g_w + (unsigned int)dx) * 3u;
        const unsigned char* s = src + (unsigned int)(sy0 + y) * (unsigned int)stride + (unsigned int)sx0 * 3u;
        unsigned int bytes = (unsigned int)w * 3u;
        for (unsigned int i = 0; i < bytes; i++) d[i] = s[i];
    }
}

void fb_backbuffer_rect(int x, int y, int w, int h, unsigned int color) {
    fb_backbuffer_line(x, y, x + w - 1, y, color);
    fb_backbuffer_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    fb_backbuffer_line(x, y, x, y + h - 1, color);
    fb_backbuffer_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

// Копирует только прямоугольник backbuffer на экран.
void fb_flip_rect(int x, int y, int w, int h) {
    if (!g_bb || !g_active) return;
    // Обрезаем строго, затем ПОВТОРНО проверяем границы.
    // Иначе при x>=g_w или y>=g_h после вычитания w/h уходят в минус,
    // а (unsigned)(w*3) даёт огромное число -> запись за пределы буфера.
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= g_w || y >= g_h) return;
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        unsigned char* dst = (unsigned char*)(g_fb + (unsigned int)yy * g_pitch + (unsigned int)x * 3u);
        unsigned char* src = g_bb + ((unsigned int)yy * (unsigned int)g_w + (unsigned int)x) * 3u;
        unsigned int bytes = (unsigned int)w * 3u;
        unsigned int dwords = bytes / 4u;
        unsigned int tail = bytes % 4u;
        // ВАЖНО: сохранить исходные указатели — rep movsl их СДВИГАЕТ,
        // иначе хвост (tail байт) скопировался бы со сдвигом (баг!).
        unsigned char* src0 = src;
        unsigned char* dst0 = dst;
        __asm__ volatile (
            "cld\n\t"
            "rep movsl\n\t"
            : "+S"(src), "+D"(dst), "+c"(dwords)
            :
            : "memory"
        );
        for (unsigned int i = 0; i < tail; i++) dst0[dwords * 4u + i] = src0[dwords * 4u + i];
    }
}

// Копирует backbuffer на экран. Быстро: rep movsd (32-битными словами).
void fb_flip(void) {
    if (!g_bb || !g_active) return;
    // Строки могут иметь разный pitch, поэтому построчно.
    for (int y = 0; y < g_h; y++) {
        unsigned char* dst = (unsigned char*)(g_fb + (unsigned int)y * g_pitch);
        unsigned char* src = g_bb + (unsigned int)y * (unsigned int)g_w * 3u;
        unsigned int bytes = (unsigned int)g_w * 3u;
        unsigned int dwords = bytes / 4u;
        unsigned int tail = bytes % 4u;
        unsigned char* src0 = src;
        unsigned char* dst0 = dst;
        __asm__ volatile (
            "cld\n\t"
            "rep movsl\n\t"
            : "+S"(src), "+D"(dst), "+c"(dwords)
            :
            : "memory"
        );
        // Хвост строки (1-3 байта). Используем ИСХОДНЫЕ указатели (rep их сдвинул).
        for (unsigned int i = 0; i < tail; i++) dst0[dwords * 4u + i] = src0[dwords * 4u + i];
    }
}
