// theme.h - единая палитра MysticOS 2 (Light/Dark, акцент).
//
// Раньше цвета были #define COL_* в kernel.c. Теперь - структура,
// которую можно переключать (тема). Стиль: Windows 11 (тёмный/светлый),
// мягкие тона, синий акцент, скруглённые углы, тени.
#ifndef THEME_H
#define THEME_H

// Цвет в формате 0xRRGGBB (как FB_RGB).
typedef struct {
    // Рабочий стол / фон
    unsigned int desktop;       // фон рабочего стола (если нет обоев)

    // Окна
    unsigned int win_bg;        // тело окна
    unsigned int win_border;    // рамка окна
    unsigned int title_act;     // заголовок активного окна
    unsigned int title_act2;    // градиент заголовка (низ)
    unsigned int title_ina;     // заголовок неактивного
    unsigned int title_ina2;    // градиент неактивного (низ)

    // Текст
    unsigned int text;          // основной текст
    unsigned int text_dim;      // приглушённый текст

    // Акцент
    unsigned int accent;        // акцент (кнопки, выделение)
    unsigned int accent_hi;     // акцент при наведении
    unsigned int sel;           // фон выделения

    // Таскбар
    unsigned int taskbar;       // фон таскбара (верх градиента)
    unsigned int taskbar2;      // низ градиента таскбара
    unsigned int taskbar_hi;    // кнопка таскбара при наведении
    unsigned int taskbar_btn;   // кнопка таскбара (обычная)

    // Меню
    unsigned int menu_bg;       // фон меню
    unsigned int menu_sel;      // пункт меню при наведении

    // Прочее
    unsigned int shadow;        // тень окна
    unsigned int sep;           // разделитель

    // Параметры оформления (для будущего: скругления, стекло)
    unsigned char corner_radius;   // радиус скругления окна
    unsigned char shadow_alpha;    // сила тени (0..255)
    unsigned char glass_alpha;     // прозрачность "матового стекла"
} theme_t;

// Текущая тема (глобально).
extern theme_t g_theme;

// Готовые темы.
extern const theme_t THEME_DARK;
extern const theme_t THEME_LIGHT;

// Установить тему. theme_set(0) = dark, theme_set(1) = light.
void theme_set(int light);

// Инициализация (ставит тему по умолчанию — dark).
void theme_init(void);

#endif // THEME_H
