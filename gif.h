// gif.h - декодер GIF87a/GIF89a (палитра, LZW, кадры, задержки).
// Пишет 24bpp BGR (как backbuffer MysticOS). Поддержка анимации: кадры
// с задержкой; вызывающий сам крутит кадры по timer_ticks.
#ifndef MYSTIC_GIF_H
#define MYSTIC_GIF_H

#define GIF_OK        0
#define GIF_ERR_SIG  -1   // не GIF8
#define GIF_ERR_HDR  -2   // обрезан заголовок
#define GIF_ERR_MEM  -3   // нет кадра/палитры
#define GIF_ERR_DEC  -4   // ошибка LZW

// Информация о GIF-файле после gif_open.
typedef struct {
    int width, height;
    int nframes;
    unsigned char* data;   // весь исходный файл (не копия, указатель)
    unsigned int size;
} gif_info_t;

// Разобрать заголовок, посчитать число кадров. 0 = успех.
int gif_open(const unsigned char* data, unsigned int size, gif_info_t* info);

// Декодировать кадр index (0-based) в dst (24bpp BGR, stride=dst_w*3).
// Возвращает задержку кадра в 1/100 сек (для анимации) или <0 ошибку.
int gif_decode_frame(const gif_info_t* info, int index,
                     unsigned char* dst, int dst_w, int dst_h);

#endif
