// vbe.c - линейный framebuffer (VBE) + примитивы рисования.
//
//   VBE это отдельный вид боли. QEMU отдаёт 24bpp, реальное железо —
//   что захочет. мы научились принимать 16/24/32 и энумерировать
//   режимы, потому что хардкод 0x115 работал только в QEMU. урок выучен.
//
//   fb_flip_rect когда-то сдвигал картинку. я его починил. наверное.
//   если снова сдвигается — это не баг, это фича (нет).
#include "vbe.h"

// Параметры VBE, сохранённые загрузчиком по адресу 0x9000:
//   0x9000: dword fb_addr | 0x9004: word pitch | 0x9006: w | 0x9008: h
#define VBE_PARAM_ADDR 0x9000u

static unsigned int g_fb = 0;
static unsigned int g_pitch = 0;
static int g_w = 0, g_h = 0;
static int g_active = 0;
static int g_lfb_bpp = 24;   // формат LFB: 24 (3 байта/пиксель) или 32 (4 байта)

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
    g_w = (int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 6));
    g_h = (int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 8));
    // BPP из 0x900A (пишет bootloader). 16/24/32.
    unsigned int bpp = *(volatile unsigned char*)(VBE_PARAM_ADDR + 10);
    if (bpp != 16 && bpp != 24 && bpp != 32) bpp = 24;
    g_lfb_bpp = (int)bpp;
    // PITCH из ModeInfo (BytesPerScanLine). Проверено на ноуте: 1664 — ВЕРНЫЙ.
    // (Попытка вычислить w*bpp/8=1600 сделала хуже — значит BIOS НЕ врёт.)
    g_pitch = (unsigned int)(*(volatile unsigned short*)(VBE_PARAM_ADDR + 4));
    g_active = 1;
    return 1;
}

// Пиксель. Формат задаётся g_lfb_bpp (24 = BGR, 32 = XRGB8888).
void fb_put_pixel(int x, int y, unsigned int color) {
    if (!g_active) return;
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    if (g_lfb_bpp == 32) {
        unsigned int* p = (unsigned int*)(g_fb + (unsigned int)y * g_pitch + (unsigned int)x * 4u);
        *p = color & 0x00FFFFFFu;
    } else if (g_lfb_bpp == 16) {
        // 565: RRRRRGGGGGGBBBBB
        unsigned short* p = (unsigned short*)(g_fb + (unsigned int)y * g_pitch + (unsigned int)x * 2u);
        unsigned int r = (color >> 16) & 0xFF, gg = (color >> 8) & 0xFF, b = color & 0xFF;
        *p = (unsigned short)(((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3));
    } else {
        unsigned char* p = (unsigned char*)(g_fb + (unsigned int)y * g_pitch + (unsigned int)x * 3u);
        p[0] = (unsigned char)(color & 0xFF);
        p[1] = (unsigned char)((color >> 8) & 0xFF);
        p[2] = (unsigned char)((color >> 16) & 0xFF);
    }
}

void fb_fill_rect(int x, int y, int w, int h, unsigned int color) {
    if (!g_active) return;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            fb_put_pixel(xx, yy, color);
}

void fb_clear(unsigned int color) {
    if (!g_active) return;
    for (int y = 0; y < g_h; y++)
        for (int x = 0; x < g_w; x++) fb_put_pixel(x, y, color);
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

// Сменить текущий backbuffer (для композитинга в отдельный буфер,
// например bg_cache). Возвращает старый — чтобы восстановить.
unsigned char* fb_backbuffer_set_target(unsigned char* new_buf) {
    unsigned char* old = g_bb;
    g_bb = new_buf;
    return old;
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

// Блит с key-цветом (прозрачность). Пиксели источника, совпадающие с key,
// НЕ копируются — на их месте остаётся то, что уже было в backbuffer
// (нижний слой). key — 0xRRGGBB, в источнике байты идут B,G,R.
void fb_backbuffer_blit_key(int dx, int dy, const unsigned char* src, int w, int h, int stride, unsigned int key) {
    if (!g_bb) return;
    unsigned char kb = key & 0xFF, kg = (key >> 8) & 0xFF, kr = (key >> 16) & 0xFF;
    int sy0 = 0, sx0 = 0;
    if (dx < 0) { sx0 = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; h += dy; dy = 0; }
    if (dx + w > g_w) w = g_w - dx;
    if (dy + h > g_h) h = g_h - dy;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        unsigned char* d = g_bb + ((unsigned int)(dy + y) * (unsigned int)g_w + (unsigned int)dx) * 3u;
        const unsigned char* s = src + (unsigned int)(sy0 + y) * (unsigned int)stride + (unsigned int)sx0 * 3u;
        for (int x = 0; x < w; x++) {
            if (s[0] == kb && s[1] == kg && s[2] == kr) { s += 3; d += 3; continue; }
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
            s += 3; d += 3;
        }
    }
}

// ============================================================
// blit_key_alpha: блит с key-прозрачностью И общей альфой окна.
// ============================================================
// src — RGB, key — цвет, который НЕ рисуется (как в blit_key).
// alpha — общая непрозрачность 0..255, применяется ко всем
// НЕ-key пикселям. Если alpha == 255, ведёт себя как blit_key.
void fb_backbuffer_blit_key_alpha(int dx, int dy, const unsigned char* src,
                                  int w, int h, int stride, unsigned int key,
                                  unsigned int alpha) {
    if (!g_bb) return;
    unsigned char kb = key & 0xFF, kg = (key >> 8) & 0xFF, kr = (key >> 16) & 0xFF;
    if (alpha > 255) alpha = 255;
    unsigned int ia = 255 - alpha;
    int sy0 = 0, sx0 = 0;
    if (dx < 0) { sx0 = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; h += dy; dy = 0; }
    if (dx + w > g_w) w = g_w - dx;
    if (dy + h > g_h) h = g_h - dy;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        unsigned char* d = g_bb + ((unsigned int)(dy + y) * (unsigned int)g_w + (unsigned int)dx) * 3u;
        const unsigned char* s = src + (unsigned int)(sy0 + y) * (unsigned int)stride + (unsigned int)sx0 * 3u;
        for (int x = 0; x < w; x++) {
            if (s[0] == kb && s[1] == kg && s[2] == kr) { s += 3; d += 3; continue; }
            if (alpha >= 255) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
            else {
                d[0] = (unsigned char)((s[0] * alpha + d[0] * ia) / 255);
                d[1] = (unsigned char)((s[1] * alpha + d[1] * ia) / 255);
                d[2] = (unsigned char)((s[2] * alpha + d[2] * ia) / 255);
            }
            s += 3; d += 3;
        }
    }
}

// ============================================================
// blit_alpha: блит с ПОПИКСЕЛЬНОЙ альфой (композитинг).
// ============================================================
// Каждый пиксель src смешивается с тем, что уже лежит в backbuffer:
//     dst = (src * a + dst * (255 - a)) / 255
//
// ВАЖНО: в src хранятся RGB (B,G,R), альфа — отдельным массивом
// (или через специальный формат — см. draw_window с маской).
// Здесь — упрощённый вариант: alpha как 4-й байт в src (RGBA),
// но наш формат 24bpp. Поэтому альфа передаётся как 4-й параметр-массив.
//
// Для практики используется так: у окна есть buf (RGB 24bpp) и
// mask (1 байт на пиксель, 0..255). Если mask == 0 — пиксель прозрачен
// полностью (как key в blit_key). Если 255 — плотный.
void fb_backbuffer_blit_alpha(int dx, int dy,
                              const unsigned char* src,
                              const unsigned char* mask,
                              int w, int h, int src_stride, int mask_stride) {
    if (!g_bb) return;
    int sy0 = 0, sx0 = 0;
    if (dx < 0) { sx0 = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; h += dy; dy = 0; }
    if (dx + w > g_w) w = g_w - dx;
    if (dy + h > g_h) h = g_h - dy;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        unsigned char* d = g_bb + ((unsigned int)(dy + y) * (unsigned int)g_w + (unsigned int)dx) * 3u;
        const unsigned char* s = src + (unsigned int)(sy0 + y) * (unsigned int)src_stride + (unsigned int)sx0 * 3u;
        const unsigned char* m = mask ? (mask + (unsigned int)(sy0 + y) * (unsigned int)mask_stride + (unsigned int)sx0) : 0;
        for (int x = 0; x < w; x++) {
            unsigned int a = m ? m[x] : 255;
            if (a == 0) { s += 3; d += 3; continue; }
            if (a == 255) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
            else {
                unsigned int ia = 255 - a;
                d[0] = (unsigned char)((s[0] * a + d[0] * ia) / 255);
                d[1] = (unsigned char)((s[1] * a + d[1] * ia) / 255);
                d[2] = (unsigned char)((s[2] * a + d[2] * ia) / 255);
            }
            s += 3; d += 3;
        }
    }
}

void fb_backbuffer_rect(int x, int y, int w, int h, unsigned int color) {
    fb_backbuffer_line(x, y, x + w - 1, y, color);
    fb_backbuffer_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    fb_backbuffer_line(x, y, x, y + h - 1, color);
    fb_backbuffer_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

// Копирует прямоугольник backbuffer на экран. Формат LFB — g_lfb_bpp.
void fb_flip_rect(int x, int y, int w, int h) {
    if (!g_bb || !g_active) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= g_w || y >= g_h) return;
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    if (w <= 0 || h <= 0) return;
    int bytes_pp = g_lfb_bpp / 8;
    for (int yy = y; yy < y + h; yy++) {
        const unsigned char* src = g_bb + ((unsigned int)yy * (unsigned int)g_w + (unsigned int)x) * 3u;
        unsigned char* dst = (unsigned char*)(g_fb + (unsigned int)yy * g_pitch + (unsigned int)x * (unsigned int)bytes_pp);
        if (g_lfb_bpp == 32) {
            unsigned int* d32 = (unsigned int*)dst;
            for (int i = 0; i < w; i++) {
                unsigned int b = src[0], g = src[1], r = src[2];
                d32[i] = b | (g << 8) | (r << 16);
                src += 3;
            }
        } else if (g_lfb_bpp == 16) {
            unsigned short* d16 = (unsigned short*)dst;
            for (int i = 0; i < w; i++) {
                unsigned int b = src[0], g = src[1], r = src[2];
                d16[i] = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                src += 3;
            }
        } else {
            for (int i = 0; i < w; i++) { dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; src+=3; dst+=3; }
        }
    }
}

void fb_flip(void) {
    if (!g_bb || !g_active) return;
    for (int y = 0; y < g_h; y++) {
        const unsigned char* src = g_bb + (unsigned int)y * (unsigned int)g_w * 3u;
        unsigned char* dst = (unsigned char*)(g_fb + (unsigned int)y * g_pitch);
        if (g_lfb_bpp == 32) {
            unsigned int* d32 = (unsigned int*)dst;
            for (int x = 0; x < g_w; x++) {
                unsigned int b = src[0], g = src[1], r = src[2];
                d32[x] = b | (g << 8) | (r << 16);
                src += 3;
            }
        } else if (g_lfb_bpp == 16) {
            unsigned short* d16 = (unsigned short*)dst;
            for (int x = 0; x < g_w; x++) {
                unsigned int b = src[0], g = src[1], r = src[2];
                d16[x] = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                src += 3;
            }
        } else {
            unsigned int bytes = (unsigned int)g_w * 3u;
            unsigned int dwords = bytes / 4u;
            unsigned int tail = bytes % 4u;
            const unsigned char* src0 = src;
            unsigned char* dst0 = dst;
            unsigned char* sd = (unsigned char*)src;
            __asm__ volatile ("cld\n\trep movsl\n\t" : "+S"(sd), "+D"(dst), "+c"(dwords) :: "memory");
            for (unsigned int i = 0; i < tail; i++) dst0[dwords * 4u + i] = src0[dwords * 4u + i];
        }
    }
}
