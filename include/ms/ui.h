// ms/ui.h - UI-кит MysticOS 2 (header-only).
#ifndef MS_UI_H
#define MS_UI_H

#include "color.h"
#include "window.h"
#include "input.h"
#include "system.h"

#define UI_BG        0x1E1F2A
#define UI_PANEL     0x262838
#define UI_PANEL2    0x2E3044
#define UI_BORDER    0x3A3C50
#define UI_TEXT      0xE8E8F0
#define UI_DIM       0x9092A0
#define UI_ACCENT    0x4A8AD8
#define UI_ACCENT_HI 0x5A9AE8
#define UI_SEL       0x2A4A8A
#define UI_OK        0x70E070
#define UI_WARN      0xFFD060
#define UI_ERR       0xFF7070
#define UI_FOLDER    0xFFC860
#define UI_EXE       0x70E070
#define UI_TXT       0xE0E0F0
#define UI_IMG       0x80D0FF
#define UI_DISK      0xB0B0C8

static inline int ui_slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static inline void ui_size_str(unsigned int v, char* out) {
    int p = 0;
    if (v >= 1024u * 1024u) {
        unsigned int m = v / (1024u * 1024u);
        char t[12]; int n = 0;
        if (m == 0) t[n++] = '0';
        while (m) { t[n++] = '0' + (m % 10); m /= 10; }
        while (n) out[p++] = t[--n];
        out[p++] = 'M';
    } else if (v >= 1024u) {
        unsigned int k = v / 1024u;
        char t[12]; int n = 0;
        if (k == 0) t[n++] = '0';
        while (k) { t[n++] = '0' + (k % 10); k /= 10; }
        while (n) out[p++] = t[--n];
        out[p++] = 'K';
    } else {
        char t[12]; int n = 0;
        if (v == 0) t[n++] = '0';
        while (v) { t[n++] = '0' + (v % 10); v /= 10; }
        while (n) out[p++] = t[--n];
        out[p++] = 'B';
    }
    out[p] = 0;
}

static inline void ui_int_str(int v, char* out) {
    int p = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    char t[12]; int n = 0; int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    if (neg) out[p++] = '-';
    while (n) out[p++] = t[--n];
    out[p] = 0;
}

static inline void ui_strcpy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// ============================================================
// Утилиты: геометрия, клики, цвета, форматирование.
// ============================================================
// Ширина текста в пикселях (системный шрифт 8x8 -> 8px на символ).
static inline int ui_text_w(const char* s) { return ui_slen(s) * 8; }
// Попадает ли точка (mx,my) в прямоугольник (x,y,w,h).
static inline int ui_hover(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}
// Клик: hover + фронт нажатия ЛКМ (не удержание). pressed_last — ЛКМ в
// предыдущем кадре (для детекта фронта). Иначе ЛКМ удержание считалось бы
// кликом каждый кадр.
static inline int ui_click(int mx, int my, int left, int pressed_last,
                           int x, int y, int w, int h) {
    return ui_hover(mx, my, x, y, w, h) && left && !pressed_last;
}
// Ограничить v в [lo, hi].
static inline int ui_clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
// Левый отступ для центрирования блока ширины tw в [x, x+w).
static inline int ui_center_x(int x, int w, int tw) { return x + (w - tw) / 2; }
// Затемнить RGB: factor 0..256 (256 = без изменений, 128 = вдвое).
static inline unsigned int ui_dim(unsigned int c, int factor) {
    int r = (int)((c >> 16) & 0xFF) * factor >> 8;
    int g = (int)((c >>  8) & 0xFF) * factor >> 8;
    int b = (int)( c        & 0xFF) * factor >> 8;
    return ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}
// Смешать два RGB: t=0 -> a, t=256 -> b.
static inline unsigned int ui_blend(unsigned int a, unsigned int b, int t) {
    int ia = 256 - t;
    unsigned int r = (((a >> 16) & 0xFF) * ia + ((b >> 16) & 0xFF) * t) >> 8;
    unsigned int g = (((a >>  8) & 0xFF) * ia + ((b >>  8) & 0xFF) * t) >> 8;
    unsigned int bl= (( a        & 0xFF) * ia + ( b        & 0xFF) * t) >> 8;
    return (r << 16) | (g << 8) | bl;
}
// Левый паддинг нулями: ui_lpad(42, 4, buf) -> "0042".
static inline void ui_lpad(unsigned int v, int width, char* out) {
    char t[16]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = '0' + (int)(v % 10u); v /= 10u; }
    int p = 0;
    while (n < width) { out[p++] = '0'; width--; }
    while (n) out[p++] = t[--n];
    out[p] = 0;
}

// ============================================================
// Процедурные иконки 16x16.
// ============================================================
#define UI_IC_FOLDER   0
#define UI_IC_FILE     1
#define UI_IC_ELF      2
#define UI_IC_IMG      3
#define UI_IC_TXT      4
#define UI_IC_DISK     5
#define UI_IC_PC       6
#define UI_IC_TERM     7
#define UI_IC_BACK     8
#define UI_IC_UP       9
#define UI_IC_HOME    10
#define UI_IC_REFRESH 11
#define UI_IC_SAVE    12
#define UI_IC_PLAY    13
#define UI_IC_PAUSE   14
#define UI_IC_STOP    15
#define UI_IC_TOOL    16
#define UI_IC_SETTINGS 17
#define UI_IC_WALL    18
#define UI_IC_PLUS    19
#define UI_IC_SEARCH  20

// Путь к BMP-иконке типа kind (относительно корня). Если файла нет —
// ui_icon нарисует процедурный fallback.
static inline const char* ui_icon_path(int kind) {
    switch (kind) {
        case UI_IC_FOLDER:   return "/icons/ui/folder.bmp";
        case UI_IC_FILE:     return "/icons/ui/file.bmp";
        case UI_IC_ELF:      return "/icons/ui/elf.bmp";
        case UI_IC_IMG:      return "/icons/ui/img.bmp";
        case UI_IC_TXT:      return "/icons/ui/txt.bmp";
        case UI_IC_DISK:     return "/icons/ui/disk.bmp";
        case UI_IC_PC:       return "/icons/ui/pc.bmp";
        case UI_IC_TERM:     return "/icons/ui/term.bmp";
        case UI_IC_BACK:     return "/icons/ui/back.bmp";
        case UI_IC_UP:       return "/icons/ui/up.bmp";
        case UI_IC_HOME:     return "/icons/ui/home.bmp";
        case UI_IC_REFRESH:  return "/icons/ui/refresh.bmp";
        case UI_IC_SAVE:     return "/icons/ui/save.bmp";
        case UI_IC_PLAY:     return "/icons/ui/play.bmp";
        case UI_IC_PAUSE:    return "/icons/ui/pause.bmp";
        case UI_IC_STOP:     return "/icons/ui/stop.bmp";
        case UI_IC_TOOL:     return "/icons/ui/tool.bmp";
        case UI_IC_SETTINGS: return "/icons/ui/settings.bmp";
        case UI_IC_WALL:     return "/icons/ui/wall.bmp";
        case UI_IC_PLUS:     return "/icons/ui/plus.bmp";
        case UI_IC_SEARCH:   return "/icons/ui/search.bmp";
        default:             return "/icons/ui/file.bmp";
    }
}

// Процедурная отрисовка (fallback, если BMP нет).
static inline void ui_icon_proc(int id, int x, int y, int kind, unsigned int col) {
    switch (kind) {
        case UI_IC_FOLDER:
            ms_fill(id, x + 1, y + 3, 6, 3, col);
            ms_fill(id, x + 1, y + 5, 14, 9, col);
            ms_fill(id, x + 2, y + 6, 12, 2, 0xFFFFFF);
            break;
        case UI_IC_FILE:
            ms_fill(id, x + 3, y + 1, 10, 14, col);
            ms_fill(id, x + 11, y + 1, 2, 2, UI_BG);
            for (int i = 0; i < 4; i++) ms_fill(id, x + 5, y + 4 + i*2, 6, 1, UI_BG);
            break;
        case UI_IC_ELF:
            ms_fill(id, x + 3, y + 1, 10, 14, col);
            ms_fill(id, x + 5, y + 5, 6, 1, UI_BG);
            ms_fill(id, x + 5, y + 5, 1, 6, UI_BG);
            ms_fill(id, x + 5, y + 10, 6, 1, UI_BG);
            ms_fill(id, x + 5, y + 7, 4, 1, UI_BG);
            break;
        case UI_IC_IMG:
            ms_fill(id, x + 1, y + 2, 14, 12, col);
            ms_fill(id, x + 2, y + 3, 12, 10, 0x203040);
            ms_fill(id, x + 3, y + 9, 10, 3, 0x50A050);
            ms_fill(id, x + 5, y + 5, 2, 2, 0xFFFF80);
            break;
        case UI_IC_TXT:
            ms_fill(id, x + 3, y + 1, 10, 14, col);
            for (int i = 0; i < 5; i++) ms_fill(id, x + 5, y + 3 + i*2, 6, 1, 0x404060);
            break;
        case UI_IC_DISK:
            ms_fill(id, x + 2, y + 2, 12, 12, col);
            ms_fill(id, x + 4, y + 9, 8, 4, UI_BG);
            ms_fill(id, x + 5, y + 4, 6, 2, 0xFFFFFF);
            break;
        case UI_IC_PC:
            ms_fill(id, x + 1, y + 3, 14, 9, col);
            ms_fill(id, x + 2, y + 4, 12, 7, 0x203040);
            ms_fill(id, x + 6, y + 12, 4, 2, col);
            ms_fill(id, x + 4, y + 14, 8, 1, col);
            break;
        case UI_IC_TERM:
            ms_fill(id, x + 1, y + 2, 14, 12, col);
            ms_fill(id, x + 2, y + 3, 12, 10, 0x101018);
            ms_text(id, x + 3, y + 5, ">_", UI_OK);
            break;
        case UI_IC_BACK:
            ms_fill(id, x + 4, y + 7, 9, 2, col);
            for (int i = 0; i < 5; i++) ms_fill(id, x + 4 + i, y + 6 - i, 1, 3 + 2*i, col);
            break;
        case UI_IC_UP:
            ms_fill(id, x + 7, y + 4, 2, 9, col);
            for (int i = 0; i < 5; i++) ms_fill(id, x + 7 - i, y + 4 + i, 1 + 2*i, 1, col);
            break;
        case UI_IC_HOME:
            for (int i = 0; i < 7; i++) ms_fill(id, x + 8 - i, y + 3 + i, 1 + 2*i, 1, col);
            ms_fill(id, x + 3, y + 8, 10, 6, col);
            ms_fill(id, x + 6, y + 10, 4, 4, UI_BG);
            break;
        case UI_IC_REFRESH:
            ms_fill(id, x + 3, y + 4, 1, 6, col);
            ms_fill(id, x + 12, y + 4, 1, 6, col);
            for (int i = 0; i < 10; i++) ms_fill(id, x + 3 + i, y + 3, 1, 1, col);
            for (int i = 0; i < 10; i++) ms_fill(id, x + 3 + i, y + 11, 1, 1, col);
            ms_fill(id, x + 10, y + 2, 3, 2, col);
            break;
        case UI_IC_SAVE:
            ms_fill(id, x + 2, y + 2, 12, 12, col);
            ms_fill(id, x + 4, y + 2, 8, 5, 0x404060);
            ms_fill(id, x + 5, y + 9, 6, 5, UI_BG);
            break;
        case UI_IC_PLAY:
            for (int i = 0; i < 8; i++) ms_fill(id, x + 4 + i, y + 3 + i/2, 1, 9 - i, col);
            break;
        case UI_IC_PAUSE:
            ms_fill(id, x + 4, y + 3, 3, 10, col);
            ms_fill(id, x + 9, y + 3, 3, 10, col);
            break;
        case UI_IC_STOP:
            ms_fill(id, x + 4, y + 4, 8, 8, col);
            break;
        case UI_IC_TOOL:
            ms_fill(id, x + 3, y + 10, 8, 3, col);
            ms_fill(id, x + 9, y + 2, 3, 8, col);
            break;
        case UI_IC_SETTINGS:
            ms_fill(id, x + 6, y + 6, 4, 4, col);
            ms_fill(id, x + 7, y + 1, 2, 14, col);
            ms_fill(id, x + 1, y + 7, 14, 2, col);
            break;
        case UI_IC_WALL:
            ms_fill(id, x + 1, y + 2, 14, 12, col);
            ms_fill(id, x + 2, y + 3, 12, 10, 0x204060);
            ms_fill(id, x + 4, y + 10, 8, 2, 0x40A040);
            ms_fill(id, x + 10, y + 4, 2, 2, 0xFFFF80);
            break;
        case UI_IC_PLUS:
            ms_fill(id, x + 7, y + 3, 2, 10, col);
            ms_fill(id, x + 3, y + 7, 10, 2, col);
            break;
        case UI_IC_SEARCH:
            ms_rect(id, x + 3, y + 3, 8, 8, col);
            ms_fill(id, x + 10, y + 10, 4, 3, col);
            break;
        default:
            ms_fill(id, x + 3, y + 3, 10, 10, col);
            break;
    }
}

// Нарисовать иконку: сначала BMP из icons/ui/, иначе процедурный fallback.
static inline void ui_icon(int id, int x, int y, int kind, unsigned int col) {
    if (ms_image(id, x, y, 16, 16, ui_icon_path(kind))) return;
    ui_icon_proc(id, x, y, kind, col);
}

// ============================================================
// Виджеты
// ============================================================

// Кнопка-иконка 22x22. Возвращает 1 при клике.
// Кнопка-иконка 22x22. Возвращает 1 при клике.
// Исправлено: раньше было `if (hover || (left && hover))` = просто `if (hover)`,
// т.е. нажатие визуально не отличалось от hover. Теперь: нажатие -> ACCENT,
// hover -> PANEL2, иначе -> фон окна (не заливаем).
static inline int ui_icon_button(int id, int x, int y, int kind,
                                 int mx, int my, int left, int pressed_last) {
    int w = 22, h = 22;
    int hover = ui_hover(mx, my, x, y, w, h);
    if (left && hover)          ms_fill(id, x, y, w, h, UI_ACCENT);
    else if (hover)             ms_fill(id, x, y, w, h, UI_PANEL2);
    ui_icon(id, x + 3, y + 3, kind, hover ? 0xFFFFFF : UI_TEXT);
    return ui_click(mx, my, left, pressed_last, x, y, w, h);
}

// Текстовая кнопка.
static inline int ui_button(int id, int x, int y, int w, int h, const char* label,
                            int mx, int my, int left, int pressed_last) {
    int hover = ui_hover(mx, my, x, y, w, h);
    unsigned int bg = hover ? (left ? UI_ACCENT : UI_ACCENT_HI) : UI_PANEL;
    ms_fill(id, x, y, w, h, bg);
    ms_rect(id, x, y, w, h, UI_BORDER);
    int tw = ui_text_w(label);
    ms_text(id, ui_center_x(x, w, tw), y + (h - 8) / 2, label,
            hover ? 0xFFFFFF : UI_TEXT);
    return ui_click(mx, my, left, pressed_last, x, y, w, h);
}

// Кнопка с произвольным цветом фона (danger/success/neutral).
static inline int ui_button_ex(int id, int x, int y, int w, int h, const char* label,
                               unsigned int base, int mx, int my, int left, int pressed_last) {
    int hover = ui_hover(mx, my, x, y, w, h);
    unsigned int bg = hover ? ui_blend(base, 0xFFFFFF, 40) : base;
    ms_fill(id, x, y, w, h, bg);
    ms_rect(id, x, y, w, h, ui_dim(base, 160));
    int tw = ui_text_w(label);
    ms_text(id, ui_center_x(x, w, tw), y + (h - 8) / 2, label, 0xFFFFFF);
    return ui_click(mx, my, left, pressed_last, x, y, w, h);
}

// Панель (фон + нижняя рамка).
static inline void ui_panel(int id, int x, int y, int w, int h, unsigned int bg) {
    ms_fill(id, x, y, w, h, bg);
    ms_fill(id, x, y + h - 1, w, 1, UI_BORDER);
}

// Статусбар внизу окна.
static inline void ui_statusbar(int id, int win_w, int win_h, const char* text) {
    ms_fill(id, 0, win_h - 14, win_w, 14, UI_PANEL);
    ms_fill(id, 0, win_h - 14, win_w, 1, UI_BORDER);
    ms_text(id, 6, win_h - 11, text, UI_DIM);
}

// Строка списка: иконка + имя + размер.
static inline void ui_list_row(int id, int x, int y, int w, int h,
                               int icon_kind, unsigned int icon_col,
                               const char* name, const char* size, int selected) {
    if (selected) ms_fill(id, x, y, w, h, UI_SEL);
    ui_icon(id, x + 4, y + (h - 16) / 2, icon_kind, icon_col);
    ms_text(id, x + 26, y + (h - 8) / 2, name, selected ? 0xFFFFFF : UI_TEXT);
    if (size && size[0]) {
        int sw = ui_slen(size) * 8;
        ms_text(id, x + w - sw - 6, y + (h - 8) / 2, size, UI_DIM);
    }
}

// Актуальная геометрия клиентской области окна (позиция + размер).
// Работает при maximize/перемещении: ядро меняет окно, программа ОПРАШИВАЕТ.
// Возвращает 1 если что-то изменилось.
static inline int ui_client_geom(int id, int* out_x, int* out_y, int* out_w, int* out_h) {
    int vc = ms_window_client(id);
    int vp = ms_window_pos(id);
    int w = (vc >> 16) & 0xFFFF;
    int h = vc & 0xFFFF;
    int x = (vp >> 16) & 0xFFFF;
    int y = vp & 0xFFFF;
    int changed = (w != *out_w) || (h != *out_h) || (x != *out_x) || (y != *out_y);
    *out_w = w; *out_h = h; *out_x = x; *out_y = y;
    return changed;
}

// Пустое состояние: большая папка по центру + сообщение.
static inline void ui_empty(int id, int cx, int cy, const char* msg) {
    ms_fill(id, cx - 16, cy - 20, 32, 6, UI_FOLDER);
    ms_fill(id, cx - 16, cy - 14, 32, 22, UI_FOLDER);
    ms_fill(id, cx - 14, cy - 12, 28, 4, 0xFFE0A0);
    int mw = ui_slen(msg) * 8;
    ms_text(id, cx - mw / 2, cy + 16, msg, UI_DIM);
}

// ============================================================
// Новые виджеты (обратная совместимость сохранена).
// ============================================================

// Горизонтальный разделитель.
static inline void ui_separator(int id, int x, int y, int w) {
    ms_fill(id, x, y, w, 1, UI_BORDER);
}

// Заголовок панели: текст + подчёркивание.
static inline void ui_header(int id, int x, int y, int w, const char* title) {
    ms_text(id, x, y, title, UI_TEXT);
    ms_fill(id, x, y + 12, w, 1, UI_BORDER);
}

// Прогресс-бар. value/total — заполнение. show_pct — рисовать "NN%".
static inline void ui_progress(int id, int x, int y, int w, int h,
                               unsigned int value, unsigned int total,
                               int show_pct) {
    if (total == 0) total = 1;
    if (value > total) value = total;
    ms_fill(id, x, y, w, h, UI_PANEL2);
    ms_rect(id, x, y, w, h, UI_BORDER);
    int fill = (int)((unsigned int)(w - 2) * value / total);
    if (fill > 0) ms_fill(id, x + 1, y + 1, fill, h - 2, UI_ACCENT);
    if (show_pct) {
        char b[8]; int p = (int)((unsigned int)value * 100u / total);
        ui_int_str(p, b);
        int bw = ui_text_w(b) + 8;
        ms_text(id, ui_center_x(x, w, bw), y + (h - 8) / 2, b, 0xFFFFFF);
    }
}

// Чекбокс. Возвращает 1 при клике (состояние меняет вызывающий).
static inline int ui_checkbox(int id, int x, int y, const char* label, int checked,
                              int mx, int my, int left, int pressed_last) {
    int box = 14;
    int tw = ui_text_w(label);
    int hov = ui_hover(mx, my, x, y, box + 6 + tw, box);
    ms_fill(id, x, y, box, box, checked ? UI_ACCENT : UI_PANEL2);
    ms_rect(id, x, y, box, box, hov ? UI_ACCENT_HI : UI_BORDER);
    if (checked) {
        ms_fill(id, x + 3, y + 6, 3, 3, 0xFFFFFF);
        ms_fill(id, x + 5, y + 8, 3, 3, 0xFFFFFF);
        ms_fill(id, x + 7, y + 6, 3, 2, 0xFFFFFF);
        ms_fill(id, x + 9, y + 4, 3, 2, 0xFFFFFF);
    }
    ms_text(id, x + box + 6, y + (box - 8) / 2, label, hov ? 0xFFFFFF : UI_TEXT);
    return ui_click(mx, my, left, pressed_last, x, y, box + 6 + tw, box);
}

// Радио-кнопка. sel — выбрана ли (одна на группу).
static inline int ui_radio(int id, int x, int y, const char* label, int sel,
                           int mx, int my, int left, int pressed_last) {
    int box = 14;
    int tw = ui_text_w(label);
    int hov = ui_hover(mx, my, x, y, box + 6 + tw, box);
    ms_fill(id, x, y, box, box, UI_PANEL2);
    ms_rect(id, x, y, box, box, hov ? UI_ACCENT_HI : UI_BORDER);
    if (sel) ms_fill(id, x + 4, y + 4, box - 8, box - 8, UI_ACCENT);
    ms_text(id, x + box + 6, y + (box - 8) / 2, label, hov ? 0xFFFFFF : UI_TEXT);
    return ui_click(mx, my, left, pressed_last, x, y, box + 6 + tw, box);
}

// Чекбокс-строка (для списков).
static inline int ui_checkbox_row(int id, int x, int y, int w, int h,
                                  const char* label, int checked,
                                  int mx, int my, int left, int pressed_last) {
    int hov = ui_hover(mx, my, x, y, w, h);
    if (hov) ms_fill(id, x, y, w, h, UI_PANEL2);
    int box = 14;
    int cy = y + (h - box) / 2;
    ms_fill(id, x + 6, cy, box, box, checked ? UI_ACCENT : UI_PANEL);
    ms_rect(id, x + 6, cy, box, box, UI_BORDER);
    if (checked) {
        ms_fill(id, x + 9,  cy + 6, 3, 3, 0xFFFFFF);
        ms_fill(id, x + 11, cy + 8, 3, 3, 0xFFFFFF);
        ms_fill(id, x + 13, cy + 6, 3, 2, 0xFFFFFF);
        ms_fill(id, x + 15, cy + 4, 3, 2, 0xFFFFFF);
    }
    ms_text(id, x + 28, y + (h - 8) / 2, label, UI_TEXT);
    return ui_click(mx, my, left, pressed_last, x, y, w, h);
}

// Вкладки (tabs). active — индекс активной. tabs — массив строк.
// Возвращает новый индекс, если кликнули по вкладке (иначе active).
static inline int ui_tabs(int id, int x, int y, int tab_w, int tab_h,
                          const char* const* tabs, int count, int active,
                          int mx, int my, int left, int pressed_last) {
    int new_active = active;
    for (int i = 0; i < count; i++) {
        int tx = x + i * tab_w;
        int hov = ui_hover(mx, my, tx, y, tab_w, tab_h);
        unsigned int bg = (i == active) ? UI_PANEL : (hov ? UI_PANEL2 : UI_BG);
        ms_fill(id, tx, y, tab_w, tab_h, bg);
        ms_rect(id, tx, y, tab_w, tab_h, UI_BORDER);
        int tw = ui_text_w(tabs[i]);
        ms_text(id, ui_center_x(tx, tab_w, tw), y + (tab_h - 8) / 2, tabs[i],
                (i == active) ? UI_ACCENT_HI : (hov ? 0xFFFFFF : UI_DIM));
        if (i == active) ms_fill(id, tx, y + tab_h - 2, tab_w, 2, UI_ACCENT);
        if (ui_click(mx, my, left, pressed_last, tx, y, tab_w, tab_h)) new_active = i;
    }
    return new_active;
}

// Слайдер (горизонтальный). value в [lo, hi]. drag — тащим ли мы (вызывающий
// передаёт своё состояние; при клике внутри дорожки начинается drag).
// Возвращает новое value. Без внутреннего состояния — stateless.
static inline int ui_slider(int id, int x, int y, int w, int value, int lo, int hi,
                            int mx, int my, int left, int pressed_last) {
    int th = 4, knob = 12;
    if (hi <= lo) hi = lo + 1;
    if (value < lo) value = lo;
    if (value > hi) value = hi;
    int track_y = y + (knob - th) / 2;
    ms_fill(id, x, track_y, w, th, UI_PANEL2);
    int knob_x = x + (int)((unsigned int)(w - knob) * (unsigned int)(value - lo) / (unsigned int)(hi - lo));
    ms_fill(id, x, track_y, knob_x - x + knob / 2, th, UI_ACCENT);
    int hov = ui_hover(mx, my, x - 2, y - 2, w + 4, knob + 4);
    ms_fill(id, knob_x, y, knob, knob, hov ? UI_ACCENT_HI : UI_ACCENT);
    ms_rect(id, knob_x, y, knob, knob, UI_DIM);
    if (left && (pressed_last == 0 || hov)) {
        int nv = lo + (int)((unsigned int)(hi - lo) * (unsigned int)ui_clamp(mx - x, 0, w) / (unsigned int)w);
        return ui_clamp(nv, lo, hi);
    }
    return value;
}

// Скроллбар (вертикальный). pos — текущая позиция (0..total-view),
// view/total — размеры. Возвращает новую pos (если тащили/кликнули).
static inline int ui_scrollbar(int id, int x, int y, int w, int h,
                               int pos, int total, int view,
                               int mx, int my, int left, int pressed_last) {
    if (total <= view) return pos;
    ms_fill(id, x, y, w, h, UI_PANEL2);
    int thumb_h = (int)((unsigned int)h * (unsigned int)view / (unsigned int)total);
    if (thumb_h < 16) thumb_h = 16;
    int max_pos = total - view;
    if (pos < 0) pos = 0;
    if (pos > max_pos) pos = max_pos;
    int thumb_y = y + (int)((unsigned int)(h - thumb_h) * (unsigned int)pos / (unsigned int)max_pos);
    int hov = ui_hover(mx, my, x, y, w, h);
    ms_fill(id, x + 2, thumb_y, w - 4, thumb_h, hov ? UI_ACCENT_HI : UI_ACCENT);
    if (left && (pressed_last == 0 || hov)) {
        int rel = ui_clamp(my - y - thumb_h / 2, 0, h - thumb_h);
        return (int)((unsigned int)rel * (unsigned int)max_pos / (unsigned int)(h - thumb_h));
    }
    return pos;
}

// Текстовое поле. Возвращает 1, если фокус был получен кликом (вызывающий
// хранит focus и текст сам). Заливка + курсор (если focus).
static inline int ui_textfield(int id, int x, int y, int w, int h,
                               const char* text, int focus,
                               int mx, int my, int left, int pressed_last) {
    int hov = ui_hover(mx, my, x, y, w, h);
    ms_fill(id, x, y, w, h, UI_BG);
    ms_rect(id, x, y, w, h, focus ? UI_ACCENT : (hov ? UI_ACCENT_HI : UI_BORDER));
    ms_text(id, x + 4, y + (h - 8) / 2, text, UI_TEXT);
    if (focus) {
        int tw = ui_text_w(text);
        int cx = x + 4 + tw;
        if (cx < x + w - 4) ms_fill(id, cx, y + 3, 1, h - 6, UI_TEXT);
    }
    return ui_click(mx, my, left, pressed_last, x, y, w, h);
}

// Тултип: маленькое окошко рядом с точкой (x,y). border — акцент.
static inline void ui_tooltip(int id, int x, int y, const char* text) {
    int tw = ui_text_w(text);
    int w = tw + 10, h = 16;
    if (x + w > (int)ms_screen_w()) x = (int)ms_screen_w() - w - 2;
    if (y + h > (int)ms_screen_h()) y = (int)ms_screen_h() - h - 2;
    ms_fill(id, x, y, w, h, UI_PANEL2);
    ms_rect(id, x, y, w, h, UI_ACCENT);
    ms_text(id, x + 5, y + 4, text, UI_TEXT);
}

// Бейдж (маленький счётчик/метка) в правом верхнем углу прямоугольника.
static inline void ui_badge(int id, int x, int y, int w, int h, const char* text) {
    int tw = ui_text_w(text);
    int bw = tw + 6, bh = 12;
    int bx = x + w - bw - 2;
    int by = y + 2;
    ms_fill(id, bx, by, bw, bh, UI_ERR);
    ms_text(id, bx + 3, by + 2, text, 0xFFFFFF);
}

// Карточка: панель с рамкой и тенью-полосой снизу.
static inline void ui_card(int id, int x, int y, int w, int h,
                           unsigned int bg, unsigned int border) {
    ms_fill(id, x + 1, y + h, w, 2, 0x101018);   // тень
    ms_fill(id, x, y, w, h, bg);
    ms_rect(id, x, y, w, h, border);
}

// ============================================================
// EVENT-DRIVEN API (v2). Программы больше не вычисляют клики руками.
// ============================================================
#define UI_EV_MOVE 0
#define UI_EV_DOWN 1
#define UI_EV_UP   2

#define UI_EV_BUF 32
typedef struct {
    int type;
    int button;
    int x, y;
} ui_event_t;

static int  ui_win_cx = 0, ui_win_cy = 0;
static ui_event_t ui_evbuf[UI_EV_BUF];
static int  ui_evn = 0;

static inline void ui_begin(int win_id) {
    int vp = ms_window_pos(win_id);
    ui_win_cx = (vp >> 16) & 0xFFFF;
    ui_win_cy = vp & 0xFFFF;
    ui_evn = 0;
    int ev;
    while ((ev = ms_mouse_event()) >= 0 && ui_evn < UI_EV_BUF) {
        int t, b, x, y;
        ms_mouse_event_split(ev, &t, &b, &x, &y);
        ui_evbuf[ui_evn].type   = t;
        ui_evbuf[ui_evn].button = b;
        ui_evbuf[ui_evn].x      = x - ui_win_cx;
        ui_evbuf[ui_evn].y      = y - ui_win_cy;
        ui_evn++;
    }
}

static inline int ui_event_count(void) { return ui_evn; }
static inline const ui_event_t* ui_event_at(int i) {
    return (i >= 0 && i < ui_evn) ? &ui_evbuf[i] : (const ui_event_t*)0;
}

static inline int ui_clicked(int x, int y, int w, int h) {
    for (int i = 0; i < ui_evn; i++) {
        if (ui_evbuf[i].type == UI_EV_DOWN && ui_evbuf[i].button == 1 &&
            ui_evbuf[i].x >= x && ui_evbuf[i].x < x + w &&
            ui_evbuf[i].y >= y && ui_evbuf[i].y < y + h) {
            return 1;
        }
    }
    return 0;
}

static inline int ui_hover_client(int x, int y, int w, int h) {
    int mx = ms_mouse_x() - ui_win_cx;
    int my = ms_mouse_y() - ui_win_cy;
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static inline int ui_btn(int id, int x, int y, int w, int h, const char* label) {
    int hover = ui_hover_client(x, y, w, h);
    ms_fill(id, x, y, w, h, hover ? UI_ACCENT_HI : UI_PANEL);
    ms_rect(id, x, y, w, h, UI_BORDER);
    int tw = ui_text_w(label);
    ms_text(id, ui_center_x(x, w, tw), y + (h - 8) / 2, label,
            hover ? 0xFFFFFF : UI_TEXT);
    return ui_clicked(x, y, w, h);
}

static inline int ui_icon_btn(int id, int x, int y, int kind) {
    int hover = ui_hover_client(x, y, 22, 22);
    if (hover) ms_fill(id, x, y, 22, 22, UI_PANEL2);
    ui_icon(id, x + 3, y + 3, kind, hover ? 0xFFFFFF : UI_TEXT);
    return ui_clicked(x, y, 22, 22);
}

static inline int ui_checkbox_ev(int id, int x, int y, const char* label, int checked) {
    int box = 14;
    int tw = ui_text_w(label);
    int hover = ui_hover_client(x, y, box + 6 + tw, box);
    ms_fill(id, x, y, box, box, checked ? UI_ACCENT : UI_PANEL2);
    ms_rect(id, x, y, box, box, hover ? UI_ACCENT_HI : UI_BORDER);
    if (checked) {
        ms_fill(id, x + 3, y + 6, 3, 3, 0xFFFFFF);
        ms_fill(id, x + 5, y + 8, 3, 3, 0xFFFFFF);
        ms_fill(id, x + 7, y + 6, 3, 2, 0xFFFFFF);
        ms_fill(id, x + 9, y + 4, 3, 2, 0xFFFFFF);
    }
    ms_text(id, x + box + 6, y + (box - 8) / 2, label, hover ? 0xFFFFFF : UI_TEXT);
    return ui_clicked(x, y, box + 6 + tw, box);
}

#endif // MS_UI_H
