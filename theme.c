// theme.c - палитры MysticOS 2 (Dark/Light) + переключение.
//
// Стиль: Windows 11. Тёмная - глубокий синевато-серый, светлая -
// мягкий белый/серый, акцент - синий. Мягкие тона, без визуального шума.
#include "theme.h"

// Хелпер: цвет из R,G,B.
#define RGB(r,g,b) (((unsigned int)(r) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(b))

// ---- Тёмная тема (Windows 11 Dark) ----
const theme_t THEME_DARK = {
    .desktop     = RGB(0x1C, 0x1C, 0x1E),   // нейтральный тёмный (Liquid Glass)

    .win_bg      = RGB(0x2C, 0x2C, 0x2E),
    .win_border  = RGB(0x3A, 0x3A, 0x3C),
    .title_act   = RGB(0x34, 0x34, 0x38),   // стекло заголовка
    .title_act2  = RGB(0x2C, 0x2C, 0x30),
    .title_ina   = RGB(0x28, 0x28, 0x2A),
    .title_ina2  = RGB(0x24, 0x24, 0x26),

    .text        = RGB(0xE8, 0xE8, 0xF0),
    .text_dim    = RGB(0x92, 0x92, 0x9A),

    .accent      = RGB(0x0A, 0x84, 0xFF),   // «iOS blue»
    .accent_hi   = RGB(0x3E, 0x9E, 0xFF),
    .sel         = RGB(0x2A, 0x3D, 0x5C),

    .taskbar     = RGB(0x20, 0x20, 0x24),
    .taskbar2    = RGB(0x18, 0x18, 0x1C),
    .taskbar_hi  = RGB(0x2E, 0x2E, 0x34),
    .taskbar_btn = RGB(0x34, 0x34, 0x3A),

    .menu_bg     = RGB(0x32, 0x32, 0x38),   // светлее окна (стекло)
    .menu_sel    = RGB(0x0A, 0x84, 0xFF),

    .shadow      = RGB(0x00, 0x00, 0x00),
    .sep         = RGB(0x3A, 0x3A, 0x40),

    .corner_radius = 10,
    .shadow_alpha  = 150,
    .glass_alpha   = 180,
};

// ---- Светлая тема (Windows 11 Light) ----
const theme_t THEME_LIGHT = {
    .desktop     = RGB(0xF2, 0xF2, 0xF4),

    .win_bg      = RGB(0xFB, 0xFB, 0xFD),
    .win_border  = RGB(0xD6, 0xD6, 0xDC),
    .title_act   = RGB(0xEC, 0xF2, 0xFC),   // лёгкое стекло
    .title_act2  = RGB(0xE2, 0xEC, 0xF9),
    .title_ina   = RGB(0xF2, 0xF2, 0xF4),
    .title_ina2  = RGB(0xEA, 0xEA, 0xEE),

    .text        = RGB(0x1A, 0x1A, 0x1E),
    .text_dim    = RGB(0x60, 0x60, 0x68),

    .accent      = RGB(0x0A, 0x84, 0xFF),   // тот же акцент
    .accent_hi   = RGB(0x3E, 0x9E, 0xFF),
    .sel         = RGB(0xCD, 0xE4, 0xFB),

    .taskbar     = RGB(0xF4, 0xF4, 0xF6),
    .taskbar2    = RGB(0xE8, 0xE8, 0xEC),
    .taskbar_hi  = RGB(0xDE, 0xDE, 0xE4),
    .taskbar_btn = RGB(0xE9, 0xE9, 0xEE),

    .menu_bg     = RGB(0xFB, 0xFB, 0xFD),
    .menu_sel    = RGB(0xE2, 0xEF, 0xFD),

    .shadow      = RGB(0x60, 0x60, 0x68),
    .sep         = RGB(0xCE, 0xCE, 0xD6),

    .corner_radius = 10,
    .shadow_alpha  = 110,
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
