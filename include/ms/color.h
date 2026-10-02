// ms/color.h - цвета и ms_rgb для программ MysticOS.
#ifndef MS_COLOR_H
#define MS_COLOR_H

// ms_rgb(r,g,b) - собрать цвет 0x00RRGGBB (совместимо с ядром).
#define ms_rgb(r, g, b)  (((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))

// Готовые цвета (часто используемые).
#define MS_BLACK        0x000000u
#define MS_WHITE        0xFFFFFFu
#define MS_GRAY         0x808080u
#define MS_LIGHT_GRAY   0xC0C0C0u
#define MS_DARK_GRAY    0x404040u
#define MS_RED          0xFF0000u
#define MS_GREEN        0x00FF00u
#define MS_BLUE         0x0000FFu
#define MS_YELLOW       0xFFFF00u
#define MS_CYAN         0x00FFFFu
#define MS_MAGENTA      0xFF00FFu
#define MS_ORANGE       0xFF8000u

// "Фон по умолчанию" для окон (совпадает с COL_WIN_BG в ядре).
#define MS_WIN_BG       0x252632u
#define MS_WIN_TITLE    0x2D4A7Cu
#define MS_WIN_TITLE_INA 0x2A2B38u
#define MS_TEXT         0xE8E8F0u
#define MS_TEXT_DIM     0x9092A0u
#define MS_ACCENT       0x4A8AD8u

// Специальный цвет — "прозрачный" для окон (сквозь него виден фон окна).
#define MS_TRANSPARENT  0xFF00FFu

#endif
