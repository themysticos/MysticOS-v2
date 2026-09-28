// bmp.h - декодер BMP (24/32bpp, BI_RGB) с масштабированием.
// Пишет в 24bpp BGR-буфер (как backbuffer MysticOS 2). Автономный модуль.
#ifndef MYSTIC_BMP_H
#define MYSTIC_BMP_H

// Результат декодирования.
//   0  - успех
//  <0  - ошибка (см. BMP_ERR_*)
#define BMP_OK          0
#define BMP_ERR_SIG    -1   // не 'BM'
#define BMP_ERR_HDR    -2   // файл/заголовок обрезан
#define BMP_ERR_BPP    -3   // не 24 и не 32 bpp
#define BMP_ERR_COMPR  -4   // biCompression != 0 (BI_RGB)
#define BMP_ERR_DIM    -5   // некорректные размеры
#define BMP_ERR_SIZE   -6   // не влезает в буфер dst
#define BMP_ERR_OFFSET -7   // bfOffBits за пределами файла

// Декодировать BMP из файла в буфер назначения dst_w*dst_h (24bpp BGR,
// построчно, pitch = dst_w*3). Изображение МАСШТАБИРУЕТСЯ (nearest neighbor)
// до dst_w x dst_h. src_limit - размер файла в байтах (граница чтения).
//
// src       - указатель на данные BMP в памяти
// src_size  - фактический размер данных (сколько байт доступно)
// dst       - буфер назначения (24bpp BGR, dst_w*3*dst_h байт)
// dst_w/h   - размеры назначения (например 800x600)
//
// Возвращает BMP_OK или BMP_ERR_*.
int bmp_decode_scaled(const unsigned char* src, unsigned int src_size,
                      unsigned char* dst, int dst_w, int dst_h);

#endif // MYSTIC_BMP_H
