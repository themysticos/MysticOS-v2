// api.h - API ядра MysticOS 2 для ELF-программ. ВЕРСИЯ 2.
#ifndef MYSTIC_API_H
#define MYSTIC_API_H

#define MYSTIC_API_VERSION 2

// Коды для window_create_ex: тип окна.
#define WIN_KIND_PROGRAM 0   // обычное окно программы (без системных кнопок)
#define WIN_KIND_SYSTEM  1   // системное окно (кнопки min/max/close, в таскбаре)

// "Прозрачный" цвет. Залитый им фон окна НЕ рисуется при выводе — сквозь
// него виден СТАТИЧНЫЙ ТЕКСТ (нижний слой). Так анимация накрывает текст,
// а на "прозрачных" местах текст остаётся виден. См. window_static_text.
#define WIN_TRANSPARENT  0xFF00FF

typedef struct {
    unsigned int version;

    // --- Вывод (в терминал/консоль ядра) ---
    void (*put_char)(char c);
    void (*print_string)(const char* s);
    void (*print_int)(int v);
    void (*print_hex)(unsigned int v);

    // --- Оконный API ---
    // Создать окно; возвращает id (>=0) или -1. (kind = WIN_KIND_*)
    int  (*window_create)(const char* title, int x, int y, int w, int h);
    int  (*window_create_ex)(const char* title, int x, int y, int w, int h, int kind);
    // Рисование в окно по id (координаты относительно клиентской области).
    void (*window_pixel)(int id, int x, int y, unsigned int color);
    void (*window_rect)(int id, int x, int y, int w, int h, unsigned int color);
    void (*window_fill)(int id, int x, int y, int w, int h, unsigned int color);
    void (*window_text)(int id, int x, int y, const char* s,
                        unsigned int fg, unsigned int bg);
    void (*window_clear)(int id, unsigned int color);
    void (*window_close)(int id);
    void (*window_flush)(void);
    // Обслужить события (мышь/клавиатура) + обновить экран.
    void (*yield)(void);

    // --- Время/ввод ---
    unsigned int (*ticks)(void);
    void (*sleep)(unsigned int ms);
    int  (*key_available)(void);
    int  (*key_getchar)(void);
    int  (*mouse_x)(void);
    int  (*mouse_y)(void);
    int  (*mouse_left)(void);

    // --- Файловая система (v2) ---
    // Список каталога. names — max*13 (12 симв + '\0'), is_dir — 0/1, sizes.
    // Возвращает число записей или -1.
    int  (*fs_list)(const char* path, char* names, int* is_dir,
                    unsigned int* sizes, int max);
    // Прочитать файл целиком. Возвращает число байт или -1.
    int  (*fs_read)(const char* path, unsigned char* buf, unsigned int max);
    // Размер файла в байтах или -1.
    int  (*fs_size)(const char* path);
    // Существует ли путь (файл или каталог). 1/0.
    int  (*fs_exists)(const char* path);
    // Запустить ELF-программу по имени (как команда 'run'). Возвращает код
    // выхода или -1. Блокирует вызывающую программу до завершения.
    int  (*run_program)(const char* name);

    // --- Экран (v2) ---
    unsigned int (*screen_w)(void);
    unsigned int (*screen_h)(void);

    void (*exit)(int code);

    // --- Обои рабочего стола (v2, добавлено ПОСЛЕ exit — ABI сохранён) ---
    // Установить фон рабочего стола из BMP-файла на диске.
    // Возвращает 1 при успехе, 0 при ошибке (файл не найден/не BMP/слишком велик).
    int  (*set_wallpaper)(const char* path);

    // --- Запуск программы с аргументом (v2, в конец — ABI сохранён) ---
    // Запустить ELF с текстовым аргументом (доступен программе через
    // get_arg). Возвращает id задачи или -1. Для просмотрщика картинок.
    int  (*run_program_arg)(const char* name, const char* arg);
    // Получить аргумент, с которым запущена ТЕКУЩАЯ программа (или "").
    const char* (*get_arg)(void);

    // --- Картинка в окно (v2, в конец — ABI сохранён) ---
    // Декодировать BMP-файл и нарисовать в окно id (масштаб до w x h).
    // Возвращает 1 при успехе, 0 при ошибке (нет файла / не BMP).
    int  (*window_image)(int id, int x, int y, int w, int h, const char* path);

    // --- Запись файла (v2, в конец — ABI сохранён) ---
    // Записать текстовый файл (строка до '\0'). Перезаписывает существующий.
    // Возвращает 1 при успехе, 0 при ошибке. Пустой текст не пишется.
    int  (*fs_write)(const char* path, const char* content);

    // --- Форматирование диска (v2, в конец — ABI сохранён) ---
    // drive: 0/1 (D0/D1). fs: 0=FAT, 1=MSFS. version: для FAT (12/16/32).
    // Возвращает 1 при успехе, 0 при ошибке. ВНИМАНИЕ: стирает диск!
    int  (*format)(int drive, int fs, int version);

    // --- Статичный текст (v2, в конец — ABI сохранён) ---
    // Рисует текст в НИЖНИЙ слой окна. Анимация (window_fill/rect в буфере
    // окна) накрывает его; а где буфер окна залит WIN_TRANSPARENT — текст
    // виден. Т.о. анимация проходит ПОВЕРХ текста, но не стирает его.
    // (x,y) — внутри клиентской области (без заголовка). Рисуются только
    // непрозрачные пиксели букв (фон не трогается), вызовы накапливаются.
    void (*window_static_text)(int id, int x, int y, const char* s,
                               unsigned int fg);
    // Очистить нижний (текстовый) слой окна.
    void (*window_static_clear)(int id);
} mystic_api_t;

#endif
