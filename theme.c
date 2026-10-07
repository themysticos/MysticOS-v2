// theme.c - палитры MysticOS 2 (Dark/Light) + переключение.
//
// Стиль: Windows 11. Тёмная - глубокий синевато-серый, светлая -
// мягкий белый/серый, акцент - синий. Мягкие тона, без визуального шума.
#include "theme.h"

// Хелпер: цвет из R,G,B.
#define RGB(r,g,b) (((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))

// ---- Тёмная тема (Windows 11 Dark) ----
const theme_t THEME_DARK = {
    .desktop     = RGB(0x1A, 0x1B, 0x26),

    .win_bg      = RGB(0x25, 0x26, 0x32),
    .win_border  = RGB(0x3A, 0x3C, 0x4E),
    .title_act   = RGB(0x2D, 0x4A, 0x7C),
    .title_act2  = RGB(0x24, 0x3A, 0x60),
    .title_ina   = RGB(0x2A, 0x2B, 0x38),
    .title_ina2  = RGB(0x22, 0x23, 0x2E),

    .text        = RGB(0xE8, 0xE8, 0xF0),
    .text_dim    = RGB(0x90, 0x92, 0xA0),

    .accent      = RGB(0x4A, 0x8A, 0xD8),
    .accent_hi   = RGB(0x6A, 0xA8, 0xF0),
    .sel         = RGB(0x2E, 0x4A, 0x7A),

    .taskbar     = RGB(0x1E, 0x1F, 0x2A),
    .taskbar2    = RGB(0x16, 0x17, 0x20),
    .taskbar_hi  = RGB(0x2A, 0x2C, 0x3C),
    .taskbar_btn = RGB(0x33, 0x35, 0x46),

    .menu_bg     = RGB(0x25, 0x26, 0x34),
    .menu_sel    = RGB(0x2D, 0x4A, 0x7C),

    .shadow      = RGB(0x0A, 0x0A, 0x10),
    .sep         = RGB(0x38, 0x3A, 0x48),

    .corner_radius = 8,
    .shadow_alpha  = 96,
    .glass_alpha   = 180,
};

// ---- Светлая тема (Windows 11 Light) ----
const theme_t THEME_LIGHT = {
    .desktop     = RGB(0xF3, 0xF3, 0xF3),

    .win_bg      = RGB(0xFA, 0xFA, 0xFC),
    .win_border  = RGB(0xD0, 0xD0, 0xD8),
    .title_act   = RGB(0xE8, 0xEE, 0xF8),
    .title_act2  = RGB(0xD8, 0xE4, 0xF4),
    .title_ina   = RGB(0xF0, 0xF0, 0xF2),
    .title_ina2  = RGB(0xE8, 0xE8, 0xEC),

    .text        = RGB(0x1A, 0x1A, 0x1E),
    .text_dim    = RGB(0x60, 0x60, 0x68),

    .accent      = RGB(0x2C, 0x6C, 0xC8),
    .accent_hi   = RGB(0x3C, 0x84, 0xE0),
    .sel         = RGB(0xCC, 0xE0, 0xF8),

    .taskbar     = RGB(0xF0, 0xF0, 0xF4),
    .taskbar2    = RGB(0xE0, 0xE0, 0xE6),
    .taskbar_hi  = RGB(0xD8, 0xD8, 0xDE),
    .taskbar_btn = RGB(0xE4, 0xE4, 0xEA),

    .menu_bg     = RGB(0xFA, 0xFA, 0xFC),
    .menu_sel    = RGB(0xE0, 0xEC, 0xF8),

    .shadow      = RGB(0x80, 0x80, 0x88),
    .sep         = RGB(0xC8, 0xC8, 0xD0),

    .corner_radius = 8,
    .shadow_alpha  = 64,
    .glass_alpha   = 200,
};

// Текущая тема.
theme_t g_theme;

void theme_set(int light) {
    g_theme = light ? THEME_LIGHT : THEME_DARK;
}

void theme_init(void) {
    theme_set(0);   // по умолчанию - тёмная (как было)
}
