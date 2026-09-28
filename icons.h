// icons.h - спрайты-иконки (пиксельные, ARGB)
#ifndef ICONS_H
#define ICONS_H

typedef struct {
    int w, h;
    const unsigned int* pixels;
} icon_t;

void draw_icon(int x, int y, const icon_t* ic);

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

void icons_init(void);

#endif
