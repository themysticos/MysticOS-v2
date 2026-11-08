// gif.c - декодер GIF87a/GIF89a: палитра, LZW, кадры, задержки.
#include "gif.h"

static unsigned int rd16(const unsigned char* p) { return p[0] | (p[1] << 8); }

// LZW-декод одного кадра в индексный буфер (idx, размер w*h).
// min_code_size — из блока изображения. Возвращает 0/-1.
static int gif_lzw(const unsigned char* in, unsigned int inlen, int min_code,
                   unsigned char* idx, int npix) {
    if (min_code < 2 || min_code > 11) return -1;
    int clear = 1 << min_code;
    int eoi = clear + 1;
    int code_size = min_code + 1;
    int next = eoi + 1;
    // Префиксы/суффиксы для кодов.
    static short prefix[4096];
    static unsigned char suffix[4096];
    static unsigned char first[4096];
    for (int i = 0; i < clear; i++) { suffix[i] = (unsigned char)i; prefix[i] = -1; first[i] = (unsigned char)i; }

    unsigned char stack[4096]; int sp = 0;
    int out = 0;
    int prev = -1;
    unsigned int bitpos = 0;
    unsigned int total_bits = inlen * 8;
    while (bitpos + code_size <= total_bits && out < npix) {
        // читаем code_size бит (LSB-first)
        unsigned int code = 0;
        for (int b = 0; b < code_size; b++) {
            unsigned int byte_i = (bitpos + b) >> 3;
            unsigned int bit_i = (bitpos + b) & 7;
            code |= ((in[byte_i] >> bit_i) & 1u) << b;
        }
        bitpos += code_size;
        if (code == (unsigned)clear) { code_size = min_code + 1; next = eoi + 1; prev = -1; continue; }
        if (code == (unsigned)eoi) break;
        int cur = code;
        if (code >= (unsigned)next) {   // KwKwK случай
            if (prev < 0) return -1;
            // строка prev + first(prev)
            int p = prev; sp = 0;
            while (p >= 0) { stack[sp++] = suffix[p]; p = prefix[p]; }
            unsigned char fc = first[prev];
            // вывод prev
            while (sp > 0 && out < npix) idx[out++] = stack[--sp];
            if (out < npix) idx[out++] = fc;
            cur = -1;   // уже вывели
            if (next < 4096) { prefix[next] = prev; suffix[next] = fc; first[next] = first[prev]; next++; }
        } else {
            int p = code; sp = 0;
            while (p >= 0) { stack[sp++] = suffix[p]; p = prefix[p]; }
            while (sp > 0 && out < npix) idx[out++] = stack[--sp];
            if (prev >= 0 && next < 4096) {
                prefix[next] = prev; suffix[next] = first[code]; first[next] = first[prev]; next++;
            }
        }
        prev = (cur >= 0) ? cur : (int)code;
        if (next == (1 << code_size) && code_size < 12) code_size++;
    }
    return (out >= npix) ? 0 : -1;
}

// Декодировать кадр index в dst (24bpp BGR). Возвращает задержку (1/100с).
int gif_decode_frame(const gif_info_t* info, int index,
                     unsigned char* dst, int dst_w, int dst_h) {
    const unsigned char* data = info->data; unsigned int size = info->size;
    if (!data || size < 13) return GIF_ERR_HDR;
    unsigned int p = 13;
    unsigned char packed = data[10];
    const unsigned char* gct = 0; int gct_n = 0;
    if (packed & 0x80) { gct_n = 1 << ((packed & 7) + 1); gct = data + p; p += 3 * gct_n; }
    int g_w = info->width, g_h = info->height;
    int transparent = -1; int delay = 10;   // default 100ms
    int cur = 0;
    while (p < size) {
        unsigned char b = data[p++];
        if (b == 0x3B) break;
        if (b == 0x21) {
            unsigned char label = data[p++];
            if (label == 0xF9) {   // Graphic Control Extension
                unsigned int bs = data[p++];   // block size (4)
                if (bs >= 4 && p + 4 <= size) {
                    unsigned char fl = data[p + 0];
                    delay = rd16(data + p + 2) * 10;   // 1/100 -> ms*10? храним 1/100
                    delay = rd16(data + p + 2);
                    if (fl & 1) transparent = data[p + 3];
                    p += bs;
                    p++;   // terminator
                } else { p += bs; p++; }
            } else {
                while (p < size && data[p]) p += 1 + data[p];
                p++;
            }
            if (cur == index) { /* заголовок текущего кадра — запомнили */ }
            continue;
        }
        if (b == 0x2C) {
            if (p + 9 > size) break;
            int ix = rd16(data + p + 0), iy = rd16(data + p + 2);
            int iw = rd16(data + p + 4), ih = rd16(data + p + 6);
            unsigned char lp = data[p + 8];
            p += 9;
            const unsigned char* lct = 0; int lct_n = 0;
            if (lp & 0x80) { lct_n = 1 << ((lp & 7) + 1); lct = data + p; p += 3 * lct_n; }
            int interlaced = lp & 0x40;
            if (p >= size) break;
            int min_code = data[p++];
            // собрать данные изображения (sub-blocks) в один буфер
            static unsigned char lzwbuf[65536];
            unsigned int lz = 0;
            while (p < size && data[p]) {
                unsigned int blk = data[p++];      // длина блока
                for (unsigned int i = 0; i < blk; i++) {
                    if (lz < sizeof(lzwbuf)) lzwbuf[lz++] = data[p + i];
                }
                p += blk;
            }
            p++;   // terminator
            if (cur == index) {
                // декодировать индексы кадра
                static unsigned char idx[65536];
                int npix = iw * ih;
                if (npix > (int)sizeof(idx)) npix = sizeof(idx);
                if (gif_lzw(lzwbuf, lz, min_code, idx, npix) != 0) return GIF_ERR_DEC;
                const unsigned char* pal = lct ? lct : gct; int pal_n = lct ? lct_n : gct_n;
                if (!pal) return GIF_ERR_MEM;
                // записать в dst с масштабом
                for (int yy = 0; yy < dst_h; yy++) {
                    int sy = (g_h > 0) ? (yy * g_h / dst_h) : 0;
                    int fy = sy - iy; if (fy < 0 || fy >= ih) continue;
                    int dy = 0;   // для интерлейса считаем отдельно ниже
                    int row = fy;
                    if (interlaced) {
                        // порядок строк: 0,8,16.. 4,12.. 2,6.. 1,3..
                        int pass[4][2] = {{0,8},{4,8},{2,4},{1,2}};
                        int found = -1, cnt = 0;
                        for (int ps = 0; ps < 4 && found < 0; ps++)
                            for (int r = pass[ps][0]; r < ih; r += pass[ps][1]) { if (cnt == fy) { found = r; break; } cnt++; }
                        if (found < 0) continue; row = found;
                    }
                    for (int xx = 0; xx < dst_w; xx++) {
                        int sx = (g_w > 0) ? (xx * g_w / dst_w) : 0;
                        int fx = sx - ix; if (fx < 0 || fx >= iw) continue;
                        int pi = row * iw + fx;
                        if (pi < 0 || pi >= npix) continue;
                        int ci = idx[pi];
                        if (ci == transparent) continue;
                        if (ci >= pal_n) ci = 0;
                        unsigned char r = pal[ci*3+0], g = pal[ci*3+1], bl = pal[ci*3+2];
                        unsigned char* o = dst + ((unsigned int)yy * dst_w + xx) * 3;
                        o[0] = bl; o[1] = g; o[2] = r;
                    }
                    (void)dy;
                }
                return delay > 0 ? delay : 10;
            }
            cur++;
            continue;
        }
        break;
    }
    return GIF_ERR_DEC;
}

int gif_open(const unsigned char* data, unsigned int size, gif_info_t* info) {
    if (!data || size < 13) return GIF_ERR_HDR;
    if (!(data[0]=='G'&&data[1]=='I'&&data[2]=='F'&&data[3]=='8')) return GIF_ERR_SIG;
    info->width = rd16(data + 6);
    info->height = rd16(data + 8);
    info->data = (unsigned char*)data;
    info->size = size;
    // Пройти по блокам, посчитать кадры (image descriptors).
    unsigned int p = 13;
    unsigned char packed = data[10];
    if (packed & 0x80) p += 3u * (1u << ((packed & 7) + 1));   // global color table
    int frames = 0;
    while (p < size) {
        unsigned char b = data[p++];
        if (b == 0x3B) break;            // trailer
        if (b == 0x21) {                 // extension
            p++;                          // label
            while (p < size && data[p]) { p += 1 + data[p]; }
            p++;
        } else if (b == 0x2C) {          // image descriptor
            frames++;
            if (p + 9 > size) break;
            unsigned char lp = data[p + 8];
            p += 9;
            if (lp & 0x80) p += 3u * (1u << ((lp & 7) + 1));
            p++;                          // LZW min code size
            while (p < size && data[p]) { p += 1 + data[p]; }
            p++;
        } else break;
    }
    info->nframes = frames;
    return GIF_OK;
}
