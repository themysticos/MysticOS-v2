// font.h - встроенный bitmap-шрифт 8x8 (ASCII 32..126)
#ifndef FONT_H
#define FONT_H

// Возвращает указатель на 8 байт глифа для символа c.
// Если c вне диапазона — глиф '?'.
const unsigned char* font_glyph8x8(char c);

#endif
