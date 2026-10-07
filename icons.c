// icons.c - встроенные иконки. Реальные спрайты — в icons_gen.c.
#include "icons.h"
#include "vbe.h"

extern const unsigned int terminal_pixels[1024];
extern const unsigned int files_pixels[1024];
extern const unsigned int mypc_pixels[1024];
extern const unsigned int btn_close_pixels[144];
extern const unsigned int btn_max_pixels[144];
extern const unsigned int btn_min_pixels[144];
extern const unsigned int btn_max_dark_pixels[144];
extern const unsigned int btn_min_dark_pixels[144];

// Простая иконка-обои (32x32): рамка-картина, "небо", "солнце", "горы".
// Рисуется кодом, чтобы не зависеть от PNG. ARGB, alpha=0 = прозрачно.
static unsigned int wall_fallback[1024];
static void make_wall_fallback(void) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned int c = 0;            // прозрачный по умолчанию (вне рамки)
            // Рамка картины.
            if (x >= 2 && x <= 29 && y >= 2 && y <= 29) {
                c = 0x90C8F0;              // "небо" — светло-голубой
                if (x <= 3 || x >= 28 || y <= 3 || y >= 28) c = 0x604020; // деревянная рамка
                // "солнце" в правом верхнем углу.
                int sx = x - 22, sy = y - 8;
                if (sx*sx + sy*sy <= 9) c = 0xFFE040;
                // "горы" — два треугольника.
                if (y >= 18) {
                    int h1 = (x >= 8 && x <= 22) ? (22 - (x < 15 ? (15 - x) : (x - 15)) * 3) : 999;
                    if (y >= h1) c = 0x408040;             // зелёная гора
                    int h2 = (x >= 16 && x <= 27) ? (24 - (x < 22 ? (22 - x) : (x - 22)) * 3) : 999;
                    if (y >= h2) c = 0x306030;             // тёмно-зелёная гора
                }
            }
            wall_fallback[y*32+x] = (c == 0) ? 0u : (0xFF000000u | c);
        }
}

// --- Процедурные иконки типов файлов (32x32, ARGB) ---
static unsigned int folder_px[1024], file_px[1024], img_px[1024], elf_px[1024];
static unsigned int disk_px[1024];

static void make_disk_icon(void) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned int c = 0;
            // Корпус накопителя.
            if (x >= 5 && x <= 26 && y >= 6 && y <= 26) {
                c = 0x808890;
                if (x <= 6 || x >= 25 || y <= 7 || y >= 25) c = 0x505860;   // контур
                // Верхняя панель.
                if (y <= 14) c = 0xA0A8B0;
                // "Блинчик" (круг) справа-снизу.
                int dx = x - 19, dy = y - 18;
                int r2 = dx*dx + dy*dy;
                if (r2 <= 36) c = 0xD0D8E0;
                if (r2 <= 9)  c = 0x303840;
                // Индикатор слева-сверху.
                if (x >= 8 && x <= 11 && y >= 9 && y <= 11) c = 0x40E040;
                // Щель слева-снизу.
                if (x >= 8 && x <= 15 && y >= 18 && y <= 20) c = 0x404850;
            }
            disk_px[y*32+x] = (c == 0) ? 0u : (0xFF000000u | c);
        }
}

static void make_folder_icon(void) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned int c = 0;   // прозрачно
            // Язычок папки.
            if (x >= 3 && x <= 14 && y >= 5 && y <= 9) c = 0xE0B040;
            // Тело папки.
            if (x >= 3 && x <= 28 && y >= 9 && y <= 25) {
                c = 0xF0C860;
                if (x <= 4 || x >= 27 || y >= 24) c = 0xC09030;  // тень/контур
                if (y == 9) c = 0xE0B040;
            }
            folder_px[y*32+x] = (c == 0) ? 0u : (0xFF000000u | c);
        }
}

static void make_file_icon(void) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned int c = 0;
            // Лист бумаги с загнутым углом.
            if (x >= 7 && x <= 24 && y >= 3 && y <= 28) {
                c = 0xE8E8F0;
                if (x <= 8 || x >= 23 || y <= 4 || y >= 27) c = 0x9090A0;  // контур
                // Загнутый угол (правый верх).
                if (x >= 20 && y <= 8 && (x - 20) + (8 - y) >= 8) c = 0xC0C0D0;
                // Строчки текста.
                if (y >= 12 && y <= 22 && (y % 4 == 0) && x >= 11 && x <= 21) c = 0x6060A0;
            }
            file_px[y*32+x] = (c == 0) ? 0u : (0xFF000000u | c);
        }
}

static void make_img_icon(void) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned int c = 0;
            // Рамка-картинка с "пейзажем" (как у обоев, но компактнее).
            if (x >= 4 && x <= 27 && y >= 6 && y <= 26) {
                c = 0x88C8F0;                            // небо
                if (x <= 5 || x >= 26 || y <= 7 || y >= 25) c = 0x405060;  // рамка
                int sx = x - 20, sy = y - 12;
                if (sx*sx + sy*sy <= 6) c = 0xFFE040;    // солнце
                if (y >= 18) {
                    int h1 = (x >= 7 && x <= 17) ? (21 - (x < 12 ? (12 - x) : (x - 12)) * 2) : 999;
                    if (y >= h1) c = 0x40A040;
                    int h2 = (x >= 14 && x <= 24) ? (23 - (x < 19 ? (19 - x) : (x - 19)) * 2) : 999;
                    if (y >= h2) c = 0x308030;
                }
            }
            img_px[y*32+x] = (c == 0) ? 0u : (0xFF000000u | c);
        }
}

static void make_elf_icon(void) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            unsigned int c = 0;
            // Шестерёнка (программа).
            int cx = 16, cy = 16, dx = x - cx, dy = y - cy;
            int r2 = dx*dx + dy*dy;
            if (r2 <= 100) c = 0x5070C0;                 // тело шестерёнки
            if (r2 <= 25)  c = 0x203050;                 // отверстие
            // Зубцы: 8 направлений.
            if (x >= 14 && x <= 18 && y >= 2 && y <= 8) c = 0x5070C0;
            if (x >= 14 && x <= 18 && y >= 24 && y <= 29) c = 0x5070C0;
            if (y >= 14 && y <= 18 && x >= 2 && x <= 8) c = 0x5070C0;
            if (y >= 14 && y <= 18 && x >= 24 && x <= 29) c = 0x5070C0;
            elf_px[y*32+x] = (c == 0) ? 0u : (0xFF000000u | c);
        }
}

void draw_icon(int x, int y, const icon_t* ic) {
    if (!ic->pixels) return;
    for (int row = 0; row < ic->h; row++) {
        for (int col = 0; col < ic->w; col++) {
            unsigned int px = ic->pixels[row * ic->w + col];
            unsigned int a = (px >> 24) & 0xFF;
            if (a == 0) continue;
            fb_backbuffer_pixel(x + col, y + row, px & 0xFFFFFF);
        }
    }
}

icon_t ICON_TERMINAL = { 32, 32, 0 };
icon_t ICON_FILES    = { 32, 32, 0 };
icon_t ICON_SETTINGS = { 32, 32, 0 };
icon_t ICON_MYPC     = { 32, 32, 0 };
icon_t ICON_WALLPAPER = { 32, 32, 0 };
icon_t ICON_FOLDER = { 32, 32, 0 };
icon_t ICON_FILE   = { 32, 32, 0 };
icon_t ICON_IMG    = { 32, 32, 0 };
icon_t ICON_ELF    = { 32, 32, 0 };
icon_t ICON_DISK   = { 32, 32, 0 };
icon_t ICON_BTN_CLOSE = { 12, 12, 0 };
icon_t ICON_BTN_MAX   = { 12, 12, 0 };
icon_t ICON_BTN_MIN   = { 12, 12, 0 };
// Тёмные версии (для СВЕТЛОЙ темы).
icon_t ICON_BTN_MAX_DARK = { 12, 12, 0 };
icon_t ICON_BTN_MIN_DARK = { 12, 12, 0 };

void icons_init(void) {
    ICON_TERMINAL.pixels = terminal_pixels;
    ICON_FILES.pixels    = files_pixels;
    ICON_SETTINGS.pixels = terminal_pixels;
    ICON_MYPC.pixels     = mypc_pixels;
    if (!ICON_WALLPAPER.pixels) { make_wall_fallback(); ICON_WALLPAPER.pixels = wall_fallback; }
    make_folder_icon(); ICON_FOLDER.pixels = folder_px;
    make_file_icon();   ICON_FILE.pixels   = file_px;
    make_img_icon();    ICON_IMG.pixels    = img_px;
    make_elf_icon();    ICON_ELF.pixels    = elf_px;
    make_disk_icon();   ICON_DISK.pixels   = disk_px;
    ICON_BTN_CLOSE.pixels = btn_close_pixels;
    ICON_BTN_MAX.pixels   = btn_max_pixels;
    ICON_BTN_MIN.pixels   = btn_min_pixels;
    ICON_BTN_MAX_DARK.pixels = btn_max_dark_pixels;
    ICON_BTN_MIN_DARK.pixels = btn_min_dark_pixels;
}
