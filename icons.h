// icons.h - спрайты-иконки (пиксельные, ARGB)
#ifndef ICONS_H
#define ICONS_H

typedef struct {
    int w, h;
    const unsigned int* pixels;
} icon_t;

void draw_icon(int x, int y, const icon_t* ic);
// Масштабированная отрисовка (ближайший сосед). scale100 = масштаб*100
// (110 => +10%). Центрируется относительно (x,y,ic->w,ic->h) — hover-эффект.
void draw_icon_scaled(int x, int y, const icon_t* ic, int scale100);

// Заглушки заполняются в icons_init(). После — указывают на реальные
// массивы (в т.ч. сгенерированные png2icon.py).
extern icon_t ICON_TERMINAL;
extern icon_t ICON_FILES;
extern icon_t ICON_SETTINGS;
extern icon_t ICON_MYPC;
extern icon_t ICON_WALLPAPER;   // иконка выбора обоев (простая, рисуется кодом)
extern icon_t ICON_FOLDER;      // папка
extern icon_t ICON_FILE;        // обычный файл
extern icon_t ICON_IMG;         // картинка (BMP)
extern icon_t ICON_ELF;         // исполняемая программа (ELF)
extern icon_t ICON_DISK;        // дисковод / жёсткий диск
extern icon_t ICON_BTN_CLOSE;
extern icon_t ICON_BTN_MAX;
extern icon_t ICON_BTN_MIN;
// Тёмные версии кнопок (для светлой темы).
extern icon_t ICON_BTN_MAX_DARK;
extern icon_t ICON_BTN_MIN_DARK;

void icons_init(void);

#endif
