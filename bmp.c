// bmp.c - декодер BMP 24/32bpp (BI_RGB) с масштабированием nearest-neighbor.
//
// Особенности:
//   - Поддержка bottom-up (biHeight > 0) и top-down (biHeight < 0).
//   - Строки BMP выровнены на 4 байта (row padding).
//   - 32bpp: 4-й байт (alpha) игнорируется (BI_RGB, alpha обычно 0).
//   - Масштаб: для каждого пикселя назначения берём ближайший исходный.
//   - Без libc, без malloc — всё на переданных буферах.
#include "bmp.h"

// Маленькие хелперы чтения little-endian (без зависимости от выравнивания).
static unsigned int rd_u32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static int rd_i32(const unsigned char* p) {
    return (int)rd_u32(p);
}
static unsigned int rd_u16(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

int bmp_decode_scaled(const unsigned char* src, unsigned int src_size,
                      unsigned char* dst, int dst_w, int dst_h) {
    if (!src || !dst) return BMP_ERR_HDR;
    if (dst_w <= 0 || dst_h <= 0) return BMP_ERR_DIM;

    // --- Заголовок файла (BITMAPFILEHEADER, 14 байт) ---
    if (src_size < 54) return BMP_ERR_HDR;
    if (src[0] != 'B' || src[1] != 'M') return BMP_ERR_SIG;
    unsigned int bf_off = rd_u32(src + 10);   // bfOffBits — где начинаются пиксели

    // --- Заголовок изображения (BITMAPINFOHEADER, 40 байт) ---
    // src+14: biSize(4) biWidth(4) biHeight(4) biPlanes(2) biBitCount(2)
    //         biCompression(4) biSizeImage(4) ...
    unsigned int hdr_size = rd_u32(src + 14);
    if (hdr_size < 40) return BMP_ERR_HDR;
    int      bi_w   = rd_i32(src + 18);
    int      bi_h   = rd_i32(src + 22);
    unsigned int bi_bpp = rd_u16(src + 28);
    unsigned int bi_comp = rd_u32(src + 30);

    if (bi_comp != 0) return BMP_ERR_COMPR;             // только BI_RGB
    if (bi_bpp != 24 && bi_bpp != 32) return BMP_ERR_BPP;
    if (bi_w <= 0 || bi_h == 0) return BMP_ERR_DIM;

    int top_down = 0;
    if (bi_h < 0) { top_down = 1; bi_h = -bi_h; }

    // Байт на пиксель и выровненная длина строки исходника (кратна 4).
    int src_bpp_bytes = (bi_bpp == 32) ? 4 : 3;
    unsigned int src_row = (unsigned int)bi_w * (unsigned int)src_bpp_bytes;
    src_row = (src_row + 3u) & ~3u;                    // выравнивание на 4

    // Проверка, что пиксельные данные целиком внутри файла.
    // (защита от битых/усечённых файлов; всё в 32-бит, без __divdi3/__muldi3)
    if (bf_off > src_size) return BMP_ERR_OFFSET;
    // need = bf_off + src_row*bi_h. Проверяем переполнение 32-бит заранее.
    if (bi_h != 0 && src_row > (0xFFFFFFFFu - bf_off) / (unsigned int)bi_h)
        return BMP_ERR_SIZE;   // переполнение — файл нереально велик
    unsigned int need = bf_off + src_row * (unsigned int)bi_h;
    if (need > src_size) return BMP_ERR_SIZE;

    // --- Масштабирование nearest-neighbor ---
    // Для каждой строки назначения y находим исходную строку sy = y*bi_h/dst_h.
    // ВАЖНО: только 32-битная арифметика (long long на -m32 тянет libgcc
    // __divdi3, которой нет при -nostdlib). Размеры картинок < 2^15, так что
    // произведение y*bi_h гарантированно влезает в 32 бита.
    for (int y = 0; y < dst_h; y++) {
        int sy = (int)(((unsigned int)y * (unsigned int)bi_h) / (unsigned int)dst_h);
        if (sy >= bi_h) sy = bi_h - 1;
        // bottom-up: строка 0 в файле = нижняя строка картинки.
        int file_row = top_down ? sy : (bi_h - 1 - sy);
        const unsigned char* srow = src + bf_off + (unsigned int)file_row * src_row;
        unsigned char* drow = dst + (unsigned int)y * (unsigned int)dst_w * 3u;
        for (int x = 0; x < dst_w; x++) {
            int sx = (int)(((unsigned int)x * (unsigned int)bi_w) / (unsigned int)dst_w);
            if (sx >= bi_w) sx = bi_w - 1;
            const unsigned char* sp = srow + (unsigned int)sx * (unsigned int)src_bpp_bytes;
            // BMP хранит B,G,R — совпадает с форматом backbuffer MysticOS 2.
            drow[x * 3 + 0] = sp[0];
            drow[x * 3 + 1] = sp[1];
            drow[x * 3 + 2] = sp[2];
        }
    }
    return BMP_OK;
}
