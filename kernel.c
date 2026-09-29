// kernel.c - MysticOS 2: рабочий стол + мышь с частичной перерисовкой.
// Окна УНИФИЦИРОВАНЫ: системные окна и окна ELF-программ — один тип window_t.
#include "vbe.h"
#include "idt.h"
#include "pic.h"
#include "icons.h"
#include "io.h"
#include "ata.h"
#include "fat.h"
#include "console.h"
#include "term.h"
#include "api.h"
#include "task.h"
#include "paging.h"
#include "pmem.h"
#include "bmp.h"
#include "fs.h"
#include "msfs.h"
#include "gdt.h"

// ============================================================
// SYSCALL (int 0x80) — заглушка. Этап ring3.
// ============================================================
void syscall_handler(unsigned int* regs) {
    // regs[8] = eax (номер syscall) в структуре pusha+сегменты.
    // Пока — просто печатаем номер (позже — диспетчер).
    (void)regs;
}

void put_char(char c);
void print_string(const char* s);
void print_int(int v);
void print_hex(unsigned int v);

extern const unsigned char* font_glyph8x8(char c);
extern void exec_do_exit(int code);

#define TASKBAR_H 28

// ============================================================
// ПАЛИТРА (современная тёмная тема). Одна таблица на всю ОС.
// ============================================================
#define COL_DESKTOP    FB_RGB(0x1A, 0x1B, 0x26)  // фон рабочего стола (если нет обоев)
#define COL_WIN_BG     FB_RGB(0x25, 0x26, 0x32)  // тело окна
#define COL_WIN_BORDER FB_RGB(0x3A, 0x3C, 0x4E)  // рамка окна
#define COL_TITLE_ACT  FB_RGB(0x2D, 0x4A, 0x7C)  // заголовок активного окна
#define COL_TITLE_INA  FB_RGB(0x2A, 0x2B, 0x38)  // заголовок неактивного
#define COL_TEXT       FB_RGB(0xE8, 0xE8, 0xF0)  // основной текст
#define COL_TEXT_DIM   FB_RGB(0x90, 0x92, 0xA0)  // приглушённый текст
#define COL_ACCENT     FB_RGB(0x4A, 0x8A, 0xD8)  // акцент (кнопки, выделение)
#define COL_ACCENT_HI  FB_RGB(0x6A, 0xA8, 0xF0)  // акцент при наведении
#define COL_SEL        FB_RGB(0x2E, 0x4A, 0x7A)  // фон выделения
#define COL_TASKBAR    FB_RGB(0x1E, 0x1F, 0x2A)  // фон таскбара
#define COL_TASKBAR_HI FB_RGB(0x2A, 0x2C, 0x3C)  // кнопка таскбара при наведении
#define COL_MENU_BG    FB_RGB(0x25, 0x26, 0x34)  // фон меню
#define COL_MENU_SEL   FB_RGB(0x2D, 0x4A, 0x7C)  // пункт меню при наведении
#define COL_SHADOW     FB_RGB(0x0A, 0x0A, 0x10)  // тень окна
#define COL_SEP        FB_RGB(0x38, 0x3A, 0x48)  // разделитель

#define MAXWIN     8
#define NWIN_SYS   3
#define PWIN_MAXW  256
#define PWIN_MAXH  200

typedef struct window_s {
    int used;
    int id;
    int x, y, w, h;
    char title[32];
    int active;
    int visible;
    int maximized;
    int minimized;
    int is_program;
    unsigned char* buf;
    int buf_idx;              // индекс буфера (0..MAXWIN-1) или -1; НЕ номер слота
    int grab_dx, grab_dy;
    int rx, ry, rw, rh;
    void (*draw_content)(struct window_s* w);
} window_t;

static window_t wins[MAXWIN];
static int win_next_id = 100;

// Буферы окон: независимый пул, чтобы номер слота окна не влиял на буфер.
// Буфер k лежит по 0x06000000 + k * PWIN_MAXW * PWIN_MAXH * 3.
static unsigned int g_buf_used = 0;   // битовая маска занятых буферов

static int buf_alloc(void) {
    for (int k = 0; k < MAXWIN; k++)
        if (!(g_buf_used & (1u << k))) { g_buf_used |= (1u << k); return k; }
    return -1;
}
static void buf_free(int k) {
    if (k >= 0 && k < MAXWIN) g_buf_used &= ~(1u << k);
}
static unsigned char* buf_ptr(int k) {
    return (unsigned char*)(0x06000000u + (unsigned int)k * PWIN_MAXW * PWIN_MAXH * 3u);
}

// ============================================================
// ОБОИ РАБОЧЕГО СТОЛА (слой 0 под окнами)
// ============================================================
// Буфер обоев: 800x600x24bpp = 1.44 МБ, фиксированный адрес выше буферов окон
// (те кончаются ~0x612C000) и НИЖЕ pmem (0x08000000).
#define WALL_BUF_ADDR  0x06130000u
// Буфер под СЫРОЙ файл BMP (читаем с диска целиком), до pmem. ~29 МБ лимит.
#define WALL_SRC_ADDR  0x062A0000u
#define WALL_SRC_MAX   (0x08000000u - WALL_SRC_ADDR)

static unsigned char* g_wall = 0;      // буфер обоев (или 0, если не готов)
static int g_wall_w = 0, g_wall_h = 0;
static int g_wall_on = 0;              // 1 = рисуем обои, 0 = fallback-заливка

// Загрузить BMP-файл path с диска и поставить его обоями (масштаб до экрана).
// Возвращает 1 при успехе, 0 при неудаче (обои НЕ меняются при ошибке).
static int wall_load(const char* path) {
    if (!g_wall) return 0;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    unsigned char* src = (unsigned char*)WALL_SRC_ADDR;
    ata_select(0);
    int sz = vfs_read_file(path, src, (unsigned int)WALL_SRC_MAX);
    if (sz <= 0) return 0;
    if (bmp_decode_scaled(src, (unsigned int)sz, g_wall, sw, sh) != BMP_OK) return 0;
    g_wall_w = sw; g_wall_h = sh;
    g_wall_on = 1;
    return 1;
}

// Установить обои программно (из API). Возвращает 1/0.
static int wall_set(const char* path) {
    return wall_load(path);
}

static void win_set_title(window_t* w, const char* s) {
    int i = 0; while (s[i] && i < 31) { w->title[i] = s[i]; i++; } w->title[i] = 0;
}

static int win_by_id(int id) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].used && wins[i].id == id) return i;
    return -1;
}

static int in_title(window_t* w, int mx, int my) {
    return mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + 20;
}

static void bring_to_front(int idx) {
    if (idx < 0 || idx >= MAXWIN - 1) return;
    window_t tmp = wins[idx];
    for (int i = idx; i < MAXWIN - 1; i++) wins[i] = wins[i + 1];
    wins[MAXWIN - 1] = tmp;
}

static int find_win(const char* title) {
    for (int i = 0; i < MAXWIN; i++) {
        if (!wins[i].used) continue;
        const char* a = wins[i].title; const char* b = title;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

static int drag_win = -1;
static int prev_left = 0;
static volatile int g_exit_request = 0;

static int dirty_x = 0, dirty_y = 0, dirty_w = 0, dirty_h = 0;
static void mark_dirty(int x, int y, int w, int h) {
    if (dirty_w == 0 && dirty_h == 0) { dirty_x = x; dirty_y = y; dirty_w = w; dirty_h = h; return; }
    int x2 = dirty_x + dirty_w, y2 = dirty_y + dirty_h;
    int nx2 = x + w, ny2 = y + h;
    if (x < dirty_x) dirty_x = x;
    if (y < dirty_y) dirty_y = y;
    if (nx2 > x2) x2 = nx2;
    if (ny2 > y2) y2 = ny2;
    dirty_w = x2 - dirty_x;
    dirty_h = y2 - dirty_y;
}

static int run_program(const char* name);
static void draw_shadow(int x, int y, int w, int h);
static void render_region(int rx, int ry, int rw, int rh);
static void draw_cursor_bb(int mx, int my);
static int  process_events(int handle_kbd);
static void cmd_execute(void);
static char cmd_buf[128];
static int cmd_len = 0;

static void bb_text(int x, int y, const char* s, unsigned int fg, unsigned int bg) {
    int px = x;
    while (*s) {
        const unsigned char* g = font_glyph8x8(*s++);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                int on = (bits & (0x80 >> col)) ? 1 : 0;
                fb_backbuffer_pixel(px + col, y + row, on ? fg : bg);
            }
        }
        px += 8;
    }
}

// Текст БЕЗ фона: рисуются только непрозрачные (on) пиксели глифа,
// "фоновые" пиксели не трогаются. Нужно для подписей поверх обоев.
static void bb_text_t(int x, int y, const char* s, unsigned int fg) {
    int px = x;
    while (*s) {
        const unsigned char* g = font_glyph8x8(*s++);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) fb_backbuffer_pixel(px + col, y + row, fg);
            }
        }
        px += 8;
    }
}

static int rects_overlap(int ax,int ay,int aw,int ah, int bx,int by,int bw,int bh) {
    return !(ax+aw <= bx || bx+bw <= ax || ay+ah <= by || by+bh <= ay);
}

typedef struct { int x, y; icon_t* ic; const char* label; const char* elf; } desktop_icon_t;
#define NICON 5
// Динамические иконки из корня диска (кроме /kernel).
#define MAXDYN 32
static desktop_icon_t dyn_icons[MAXDYN];
static char dyn_names[MAXDYN][13];       // 8.3 имена
static int  dyn_isdir[MAXDYN];
static int  dyn_count = 0;
static desktop_icon_t dicons[NICON];
static int last_icon = -1;
static unsigned int last_icon_tick = 0;

// Иконки запускают ELF-программы из /kernel/SYSPR/ (не встроенные окна).
static void icons_layout(void) {
    dicons[0].x = 24;  dicons[0].y = 24;  dicons[0].ic = &ICON_MYPC;
    dicons[0].label = "My PC";    dicons[0].elf = "/kernel/SYSPR/ABOUT.ELF";
    dicons[1].x = 24;  dicons[1].y = 110; dicons[1].ic = &ICON_TERMINAL;
    dicons[1].label = "Terminal"; dicons[1].elf = "/kernel/SYSPR/TERM.ELF";
    dicons[2].x = 24;  dicons[2].y = 196; dicons[2].ic = &ICON_FILES;
    dicons[2].label = "Docs";     dicons[2].elf = "/kernel/SYSPR/FILES.ELF";
    dicons[3].x = 24;  dicons[3].y = 282; dicons[3].ic = &ICON_WALLPAPER;
    dicons[3].label = "Wallpaper"; dicons[3].elf = "/kernel/SYSPR/WPICK.ELF";
    // Второй диск (D1) — открывает Files с аргументом "D1:/".
    dicons[4].x = 24;  dicons[4].y = 368; dicons[4].ic = &ICON_DISK;
    dicons[4].label = "Disk D1"; dicons[4].elf = "/kernel/SYSPR/FILES.ELF";
}

// Проверка расширения имени (регистронезависимо).
static int name_has_ext(const char* nm, const char* ext) {
    for (int k = 0; nm[k]; k++) {
        if (nm[k] == '.') {
            int j = 0, ok = 1;
            while (ext[j]) {
                char a = nm[k+1+j]; if (a >= 'a' && a <= 'z') a -= 32;
                char b = ext[j];   if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) { ok = 0; break; }
                j++;
            }
            if (ok) return 1;
        }
    }
    return 0;
}

// Сканировать корень диска и разложить файлы иконками на рабочем столе.
// Исключаем /kernel (системный). Системные иконки (dicons) — отдельно слева.
static void desktop_scan(void) {
    static char nm[MAXDYN * 13];
    static int  isdir[MAXDYN];
    static unsigned int szs[MAXDYN];
    int n = vfs_read_dir("/", nm, isdir, szs, MAXDYN);
    if (n < 0) n = 0;
    dyn_count = 0;
    for (int i = 0; i < n && dyn_count < MAXDYN; i++) {
        const char* name = nm + i * 13;
        // Пропускаем системный каталог KERNEL.
        if (name[0]=='K'&&name[1]=='E'&&name[2]=='R'&&name[3]=='N'&&name[4]=='E'&&name[5]=='L'&&name[6]==0) continue;
        int c = dyn_count;
        int k = 0;
        while (name[k] && k < 12) { dyn_names[c][k] = name[k]; k++; }
        dyn_names[c][k] = 0;
        dyn_isdir[c] = isdir[i];
        // Раскладка сеткой: столбцы через 96px начиная со 2-го столбца.
        int col = c / 5;      // 5 иконок в столбце
        int row = c % 5;
        dyn_icons[c].x = 120 + col * 96;
        dyn_icons[c].y = 24 + row * 86;
        dyn_icons[c].ic = isdir[i] ? &ICON_FOLDER
                        : name_has_ext(name, "bmp") ? &ICON_IMG
                        : name_has_ext(name, "elf") ? &ICON_ELF
                        : &ICON_FILE;
        dyn_icons[c].label = dyn_names[c];
        dyn_icons[c].elf = 0;   // путь построим при клике
        dyn_count++;
    }
}

static void draw_icons_region(int rx,int ry,int rw,int rh) {
    // Подпись рисуем ПРОЗРАЧНО (bb_text_t): фон — обои, а не сплошная плашка.
    // Тень (чёрный глиф на 1px ниже/правее) — для читаемости на светлых обоях.
    unsigned int fg = FB_RGB(0xFF,0xFF,0xFF);
    unsigned int shadow = FB_RGB(0x00,0x00,0x00);
    for (int i = 0; i < NICON; i++) {
        int ix = dicons[i].x, iy = dicons[i].y;
        if (!rects_overlap(rx,ry,rw,rh, ix - 2, iy - 2, 96, 56)) continue;
        draw_icon(ix, iy, dicons[i].ic);
        bb_text_t(ix + 3, iy + 37, dicons[i].label, shadow);   // тень
        bb_text_t(ix + 2, iy + 36, dicons[i].label, fg);       // белый текст
    }
    // Динамические иконки из корня диска.
    for (int i = 0; i < dyn_count; i++) {
        int ix = dyn_icons[i].x, iy = dyn_icons[i].y;
        if (!rects_overlap(rx,ry,rw,rh, ix - 2, iy - 2, 92, 56)) continue;
        draw_icon(ix, iy, dyn_icons[i].ic);
        bb_text_t(ix + 3, iy + 37, dyn_icons[i].label, shadow);
        bb_text_t(ix + 2, iy + 36, dyn_icons[i].label, fg);
    }
}

static int icon_hit(int i, int mx, int my) {
    return mx >= dicons[i].x && mx < dicons[i].x + 32 &&
           my >= dicons[i].y && my < dicons[i].y + 32;
}

#define FMAX 64
static char f_names[FMAX * 13];
static int  f_isdir[FMAX];
static unsigned int f_sizes[FMAX];
static int  f_count = -1;
static char g_files_path[128] = "/";   // текущий каталог эксплорера

static int files_at_root(void) { return g_files_path[0] == '/' && g_files_path[1] == 0; }

static void files_reload(void) {
    ata_select(0);
    f_count = fat_read_dir(0, g_files_path, f_names, f_isdir, f_sizes, FMAX);
    if (f_count < 0) f_count = 0;
}

static void files_render(int x, int y, int w, int h) {
    if (f_count < 0) files_reload();
    fb_backbuffer_fill(x, y, w, h, FB_RGB(0x18, 0x18, 0x22));
    int row_h = 16;
    int rows = h / row_h;
    // Заголовок с текущим путём.
    bb_text(x + 4, y + 2, g_files_path, FB_RGB(0x80,0xC0,0xFF), FB_RGB(0x18,0x18,0x22));
    fb_backbuffer_line(x, y + 12, x + w, y + 12, FB_RGB(0x40,0x40,0x50));
    // Если не корень — строка ".." (выход наверх).
    int special = files_at_root() ? 0 : 1;
    if (special) {
        bb_text(x + 6, y + 16, "..", FB_RGB(0xFF,0xC0,0x60), FB_RGB(0x18,0x18,0x22));
    }
    if (f_count == 0 && !special) {
        bb_text(x + 6, y + 16, "(empty)", FB_RGB(0xA0,0xA0,0xB0), FB_RGB(0x18,0x18,0x22));
        return;
    }
    int maxrows = rows - 1 - special;
    for (int i = 0; i < f_count && i < maxrows; i++) {
        int ry = y + 16 + (i + special) * row_h;
        unsigned int fgc = f_isdir[i] ? FB_RGB(0xFF,0xE0,0x60) : FB_RGB(0xE0,0xE0,0xF0);
        bb_text(x + 6, ry, f_names + i * 13, fgc, FB_RGB(0x18,0x18,0x22));
        if (!f_isdir[i]) {
            char sz[16]; int v = (int)f_sizes[i], p = 0;
            if (v == 0) sz[p++] = '0';
            char tmp[12]; int t = 0;
            while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
            while (t) sz[p++] = tmp[--t];
            sz[p] = 0;
            int sx = x + 6 + 13 * 8;
            bb_text(sx, ry, sz, FB_RGB(0x80,0x80,0x90), FB_RGB(0x18,0x18,0x22));
        }
    }
}

// Что под курсором в окне Files:
//   -2 = строка ".." (наверх),  -1 = ничего,  >=0 = индекс файла.
// Геометрия совпадает с files_render: заголовок 16px, затем строки по 16px.
static int files_pick(window_t* w, int mx, int my) {
    int lx = mx - (w->x + 6);
    int ly = my - (w->y + 26 + 16);
    if (lx < 0 || lx >= w->w - 12) return -1;
    if (ly < 0) return -1;
    int row = ly / 16;
    int special = files_at_root() ? 0 : 1;
    if (special && row == 0) return -2;   // ".."
    int idx = row - special;
    if (idx < 0 || idx >= f_count) return -1;
    return idx;
}

// Собрать полный путь к элементу текущего каталога в out (buf 160).
static void files_full_path(const char* name, char* out) {
    int n = 0; while (g_files_path[n] && n < 150) { out[n] = g_files_path[n]; n++; }
    if (n > 0 && out[n-1] != '/') out[n++] = '/';
    for (int k = 0; name[k] && n < 158; k++) out[n++] = name[k];
    out[n] = 0;
}

// Войти в каталог: добавить компонент к текущему пути.
static void files_enter(const char* name) {
    int n = 0; while (g_files_path[n]) n++;
    if (n > 0 && g_files_path[n-1] != '/') { if (n < 126) g_files_path[n++] = '/'; }
    for (int k = 0; name[k] && n < 126; k++) g_files_path[n++] = name[k];
    g_files_path[n] = 0;
    f_count = -1;   // перечитать
}

// Выйти на уровень выше (обрезать последний компонент).
static void files_go_up(void) {
    int n = 0; while (g_files_path[n]) n++;
    if (n <= 1) { g_files_path[0] = '/'; g_files_path[1] = 0; f_count = -1; return; }
    if (g_files_path[n-1] == '/') n--;          // хвостовой слэш
    while (n > 1 && g_files_path[n-1] != '/') n--;
    if (n <= 1) { g_files_path[0] = '/'; g_files_path[1] = 0; }
    else g_files_path[n-1] = 0;
    f_count = -1;
}

// Двойной клик по файлу в окне Files.
static unsigned int g_files_last_tick = 0;
static int g_files_last_idx = -1;

// Мигание курсора терминала: период ~0.5с (50 тиков по 10мс).
static unsigned int g_term_cursor_tick = 0;
static int g_term_cursor_on = 0;

static void content_terminal(window_t* w) {
    term_set_area(w->x + 4, w->y + 24, w->w - 8, w->h - 28);
    term_render();
    // Курсор ввода виден только когда окно терминала активно.
    // Фазу мигания (g_term_cursor_on) переключает главный цикл по таймеру,
    // а здесь лишь рисуем текущее состояние — иначе мигание зависело бы
    // от того, есть ли события (мышь/клавиатура).
    if (w->active) term_draw_cursor(g_term_cursor_on);
}

static void content_files(window_t* w) {
    files_render(w->x + 6, w->y + 26, w->w - 12, w->h - 32);
}

static void content_about(window_t* w) {
    unsigned int bg = FB_RGB(0xE0,0xE0,0xE8);
    bb_text(w->x + 10, w->y + 30, "MysticOS 2",        FB_RGB(0x20,0x20,0x40), bg);
    bb_text(w->x + 10, w->y + 48, "Graphics edition", FB_RGB(0x40,0x40,0x60), bg);
    bb_text(w->x + 10, w->y + 64, "C + NASM, VBE 24bpp", FB_RGB(0x40,0x40,0x60), bg);
}

static void windows_init(void) {
    for (int i = 0; i < MAXWIN; i++) {
        wins[i].used = 0; wins[i].buf = 0; wins[i].buf_idx = -1; wins[i].draw_content = 0;
        wins[i].active = 0; wins[i].visible = 0;
        wins[i].maximized = 0; wins[i].minimized = 0; wins[i].is_program = 0;
    }
    // НИЧЕГО не создаём. Все окна (в т.ч. системные) создаются по требованию
    // функцией open_sys_win() при открытии из иконки/меню.
}

// Открыть системное окно по заголовку. Если его ещё нет — СОЗДАТЬ в свободном
// слоте (геометрия и content — по типу). Идемпотентно: повторный вызов просто
// показывает существующее и делает его активным.
static void open_sys_win(const char* title) {
    int wi = find_win(title);
    if (wi < 0) {
        for (int i = 0; i < MAXWIN; i++) {
            if (wins[i].used) continue;
            window_t* w = &wins[i];
            w->used = 1; w->id = win_next_id++;
            w->buf = 0; w->is_program = 0;
            w->maximized = 0; w->minimized = 0; w->visible = 0; w->active = 0;
            win_set_title(w, title);
            if (title[0] == 'T') {
                w->x = 80;  w->y = 80;  w->w = 320; w->h = 200; w->draw_content = content_terminal;
                // Первое создание терминала: задать область и напечатать приветствие.
                term_set_area(w->x + 4, w->y + 24, w->w - 8, w->h - 28);
                term_print("MysticOS 2 terminal\nType 'help' for commands.\n\n");
                term_print("[/]> ");
            }
            else if (title[0] == 'F') { w->x = 420; w->y = 140; w->w = 300; w->h = 180; w->draw_content = content_files; }
            else { w->x = 200; w->y = 320; w->w = 260; w->h = 150; w->draw_content = content_about; }
            wi = i;
            break;
        }
        if (wi < 0) return;   // нет свободных слотов
    }
    wins[wi].visible = 1;
    wins[wi].minimized = 0;
    for (int j = 0; j < MAXWIN; j++) wins[j].active = (j == wi) ? 1 : 0;
    mark_dirty(wins[wi].x - 4, wins[wi].y - 4, wins[wi].w + 8, wins[wi].h + 8);
}

static int api_window_create(const char* title, int x, int y, int cw, int ch) {
    if (cw > PWIN_MAXW) cw = PWIN_MAXW;
    if (ch > PWIN_MAXH) ch = PWIN_MAXH;
    if (cw <= 0 || ch <= 0) return -1;
    for (int i = 0; i < MAXWIN; i++) {
        if (wins[i].used) continue;
        window_t* w = &wins[i];
        w->used = 1;
        w->id = win_next_id++;
        w->is_program = 1;
        w->x = x; w->y = y;
        w->w = cw; w->h = ch + 20;
        w->maximized = 0; w->minimized = 0; w->visible = 1;
        w->draw_content = 0;
        w->grab_dx = 0; w->grab_dy = 0;
        win_set_title(w, title);
        int bk = buf_alloc();
        if (bk < 0) { w->used = 0; return -1; }   // нет свободных буферов
        w->buf_idx = bk;
        w->buf = buf_ptr(bk);
        for (int k = 0; k < cw * ch * 3; k++) w->buf[k] = 0x00;
        int nid = w->id;
        bring_to_front(i);
        for (int j = 0; j < MAXWIN; j++) wins[j].active = 0;
        wins[MAXWIN - 1].active = 1;
        mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
        return nid;
    }
    return -1;
}

// Создание окна с указанием типа (программное/системное). Пока различия
// минимальны: оба дают буфер для рисования; "системное" остаётся видимым
// и ведёт себя как окно ядра (кнопки min/max/close, таскбар).
static int api_window_create_ex(const char* title, int x, int y, int cw, int ch, int kind) {
    int id = api_window_create(title, x, y, cw, ch);
    if (id < 0) return id;
    int idx = win_by_id(id);
    if (idx >= 0) {
        // WIN_KIND_PROGRAM: is_program=1 (по умолчанию). Системные ELF-окна
        // тоже считаем программными — у них есть буфер и свои кнопки.
        (void)kind;
        wins[idx].is_program = 1;
    }
    return id;
}

static void api_window_pixel(int id, int x, int y, unsigned int color) {
    int idx = win_by_id(id);
    if (idx < 0) return;
    window_t* w = &wins[idx];
    int ch = w->h - 20;
    if (x < 0 || y < 0 || x >= w->w || y >= ch) return;
    unsigned char* p = w->buf + (y * w->w + x) * 3;
    p[0] = color & 0xFF; p[1] = (color >> 8) & 0xFF; p[2] = (color >> 16) & 0xFF;
}

static void api_window_fill(int id, int x, int y, int ww, int hh, unsigned int color) {
    for (int yy = y; yy < y + hh; yy++)
        for (int xx = x; xx < x + ww; xx++)
            api_window_pixel(id, xx, yy, color);
}

static void api_window_rect(int id, int x, int y, int ww, int hh, unsigned int color) {
    for (int xx = x; xx < x + ww; xx++) { api_window_pixel(id, xx, y, color); api_window_pixel(id, xx, y + hh - 1, color); }
    for (int yy = y; yy < y + hh; yy++) { api_window_pixel(id, x, yy, color); api_window_pixel(id, x + ww - 1, yy, color); }
}

static void api_window_text(int id, int x, int y, const char* s, unsigned int fg, unsigned int bg) {
    int px = x;
    while (*s) {
        const unsigned char* g = font_glyph8x8(*s++);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                int on = (bits & (0x80 >> col)) ? 1 : 0;
                api_window_pixel(id, px + col, y + row, on ? fg : bg);
            }
        }
        px += 8;
    }
}

static void api_window_clear(int id, unsigned int color) {
    int idx = win_by_id(id);
    if (idx < 0) return;
    window_t* w = &wins[idx];
    api_window_fill(id, 0, 0, w->w, w->h - 20, color);
    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
}

static void api_window_close(int id) {
    int idx = win_by_id(id);
    if (idx < 0) return;
    buf_free(wins[idx].buf_idx);
    wins[idx].used = 0; wins[idx].buf = 0; wins[idx].buf_idx = -1;
    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
}

static void api_window_flush(void) {
    render_region(0, 0, (int)vbe_width(), (int)vbe_height());
    draw_cursor_bb(mouse_x(), mouse_y());
    fb_flip();
}

static void api_exit_stub(int code) { (void)code; }

static void api_sleep(unsigned int ms) {
    unsigned int need = (ms + 9u) / 10u;
    unsigned int start = timer_ticks();
    while ((timer_ticks() - start) < need) { __asm__ volatile ("hlt"); }
}

// ============ ФС-API для ELF-программ (v2) ============
extern int fat_read_dir(int base_dir, const char* path, char* names, int* is_dir,
                        unsigned int* sizes, int max);
extern int fat_read_file_buf(const char* path, unsigned char* buf, unsigned int max);
extern int fat_find(int base_dir, const char* path);

static int api_fs_list(const char* path, char* names, int* is_dir,
                       unsigned int* sizes, int max) {
    ata_select(0);
    return vfs_read_dir(path, names, is_dir, sizes, max);
}

static int api_fs_read(const char* path, unsigned char* buf, unsigned int max) {
    ata_select(0);
    return vfs_read_file(path, buf, max);
}

static int api_fs_size(const char* path) {
    ata_select(0);
    return vfs_size(path);
}

static int api_fs_exists(const char* path) {
    ata_select(0);
    return vfs_find(path) ? 1 : 0;
}

// API: записать текстовый файл (перезапись). 1/0.
static int api_fs_write(const char* path, const char* content) {
    ata_select(0);
    return vfs_write(path, content);
}

// API: форматировать диск (drive 0/1). fs: 0=FAT, 1=MSFS. 1/0.
// ВНИМАНИЕ: уничтожает данные на диске. После — переопределяем VFS.
static int api_format(int drive, int fs, int version) {
    if (drive < 0 || drive > 1) return 0;
    ata_select(drive);
    fat_use_drive(drive);
    msfs_use_drive(drive);
    int ok = 0;
    if (fs == 1) { msfs_format(); ok = msfs_probe(); }
    else { ok = fat_format(version); }
    if (ok) {
        vfs_init();          // переопределить ФС на дисках
        desktop_scan();      // обновить иконки (если форматировали D0)
        mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
    }
    return ok;
}

static unsigned int api_screen_w(void) { return vbe_width(); }
static unsigned int api_screen_h(void) { return vbe_height(); }

// API: установить обои из BMP-файла (масштаб до экрана). 1/0.
static int api_set_wallpaper(const char* path) {
    int ok = wall_set(path);
    // Если обои сменились — пометить весь экран грязным для полной перерисовки.
    if (ok) mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
    return ok;
}

// Аргумент, с которым запущена текущая ELF-программа (общий на ядро).
// Упрощение: считаем, что активна одна «пользовательская» программа.
static char g_prog_arg[160] = "";
static const char* api_get_arg(void) { return g_prog_arg; }

// API: запустить ELF с аргументом. Аргумент копируется в g_prog_arg и будет
// доступен программе через get_arg(). 0 при успехе, -1 при ошибке.
static int api_run_program_arg(const char* name, const char* arg) {
    // run_program() сбрасывает g_prog_arg, поэтому: запускаем (задача только
    // создаётся, но не выполняется до task_yield), затем восстанавливаем аргумент.
    int rc = run_program(name);
    int i = 0;
    if (arg) { while (arg[i] && i < 158) { g_prog_arg[i] = arg[i]; i++; } }
    g_prog_arg[i] = 0;
    return rc;
}

// API: декодировать BMP и нарисовать в окно id (масштаб до w x h). 1/0.
static int api_window_image(int id, int x, int y, int w, int h, const char* path) {
    int idx = win_by_id(id);
    if (idx < 0) return 0;
    window_t* win = &wins[idx];
    if (!win->buf) return 0;
    if (w <= 0 || h <= 0) return 0;
    // Ограничиваем область клиентской частью окна (без заголовка 20px).
    int cw = win->w, ch = win->h - 20;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > cw) w = cw - x;
    if (y + h > ch) h = ch - y;
    if (w <= 0 || h <= 0) return 0;
    // Читаем сырой BMP во временный буфер, декодируем ПРЯМО в буфер окна
    // (построчный старт с offset), промежуточный буфер не нужен.
    unsigned char* src = (unsigned char*)WALL_SRC_ADDR;
    ata_select(0);
    int sz = vfs_read_file(path, src, (unsigned int)WALL_SRC_MAX);
    if (sz <= 0) return 0;
    // Указываем декодеру на начало области (x,y) в буфере окна.
    unsigned char* dst = win->buf + ((unsigned int)y * (unsigned int)win->w + (unsigned int)x) * 3u;
    int rc = bmp_decode_scaled(src, (unsigned int)sz, dst, w, h);
    (void)cw; (void)ch;
    return (rc == BMP_OK) ? 1 : 0;
}

static int g_last_cmx = -100, g_last_cmy = -100;
static unsigned int g_yield_frames = 0;
static unsigned int g_yield_fps = 60;
static unsigned int g_fps_tick = 0;
static unsigned int g_last_draw_tick = 0;
static unsigned int g_min_draw_ticks = 1;

// Реализация вывода кадра. Обёрнута api_yield, которая в конце уступает CPU.
static void api_yield_impl(void) {
    int changed = process_events(0);
    // Окно программы закрыли — завершить ЗАДАЧУ (не exec_do_exit: мы теперь задача).
    if (g_exit_request) { g_exit_request = 0; task_exit(0); }

    unsigned int now = timer_ticks();
    g_yield_frames++;
    if (now - g_fps_tick >= 100) {
        g_yield_fps = g_yield_frames;
        g_yield_frames = 0;
        g_fps_tick = now;
        if (g_yield_fps < 6) {
            g_min_draw_ticks += 1;
            if (g_min_draw_ticks > 50) g_min_draw_ticks = 50;
        } else if (g_yield_fps > 20) {
            if (g_min_draw_ticks > 1) g_min_draw_ticks--;
        }
    }

    // Троттлинг НЕ применяем, если что-то изменилось (клик, drag, свёрнуто/
    // развёрнуто окно): иначе событие потеряется и экран застынет.
    if (drag_win < 0 && !changed && (now - g_last_draw_tick < g_min_draw_ticks)) return;
    g_last_draw_tick = now;

    int cmx = mouse_x(), cmy = mouse_y();
    // Тащим окно ИЛИ что-то изменилось (клик, свёрнуто/развёрнуто/закрыто окно,
    // смена z-порядка) — полная перерисовка сцены. Иначе, например, при
    // сворачивании окна программы экран застывал бы со старой картинкой.
    if (drag_win >= 0 || changed) {
        render_region(0, 0, (int)vbe_width(), (int)vbe_height());
        draw_cursor_bb(cmx, cmy);
        fb_flip();
        g_last_cmx = cmx; g_last_cmy = cmy;
        dirty_w = 0; dirty_h = 0;
        return;
    }
    // Область перерисовки: СТАРЫЙ и НОВЫЙ курсор + dirty + ВСЕ видимые окна
    // программ (их содержимое — анимация — меняется каждый кадр). Рисуем
    // ВСЕГДА, даже если окон нет: иначе курсор замирает.
    int rx = cmx, ry = cmy, rx2 = cmx + 12, ry2 = cmy + 19;
    if (g_last_cmx > -50) {
        if (g_last_cmx < rx) rx = g_last_cmx;
        if (g_last_cmy < ry) ry = g_last_cmy;
        if (g_last_cmx + 12 > rx2) rx2 = g_last_cmx + 12;
        if (g_last_cmy + 19 > ry2) ry2 = g_last_cmy + 19;
    }
    if (dirty_w > 0 && dirty_h > 0) {
        if (dirty_x < rx) rx = dirty_x;
        if (dirty_y < ry) ry = dirty_y;
        if (dirty_x + dirty_w > rx2) rx2 = dirty_x + dirty_w;
        if (dirty_y + dirty_h > ry2) ry2 = dirty_y + dirty_h;
    }
    // Включаем видимые окна программ — иначе их содержимое (анимация)
    // не перерисовывается после отпускания мыши.
    for (int i = 0; i < MAXWIN; i++) {
        if (!wins[i].used || !wins[i].is_program || !wins[i].visible) continue;
        int wx = wins[i].x - 4, wy = wins[i].y - 4;
        int wx2 = wins[i].x + wins[i].w + 4, wy2 = wins[i].y + wins[i].h + 4;
        if (wx < rx) rx = wx;
        if (wy < ry) ry = wy;
        if (wx2 > rx2) rx2 = wx2;
        if (wy2 > ry2) ry2 = wy2;
    }
    render_region(rx, ry, rx2 - rx, ry2 - ry);
    draw_cursor_bb(cmx, cmy);
    fb_flip_rect(rx, ry, rx2 - rx, ry2 - ry);
    g_last_cmx = cmx; g_last_cmy = cmy;
    dirty_w = 0; dirty_h = 0;
}

// Программный yield: показать кадр и уступить CPU другим задачам.
static void api_yield(void) {
    api_yield_impl();
    task_yield();   // кооперативное переключение (если включено)
}

static mystic_api_t g_api = {
    MYSTIC_API_VERSION,
    put_char, print_string, print_int, print_hex,
    api_window_create, api_window_create_ex,
    api_window_pixel, api_window_rect, api_window_fill,
    api_window_text, api_window_clear, api_window_close, api_window_flush,
    api_yield, timer_ticks, api_sleep,
    kb_available, kb_getchar, mouse_x, mouse_y, mouse_left,
    api_fs_list, api_fs_read, api_fs_size, api_fs_exists, run_program,
    api_screen_w, api_screen_h,
    api_exit_stub,
    api_set_wallpaper,    // в КОНЕЦ — ABI v2 сохранён
    api_run_program_arg,
    api_get_arg,
    api_window_image,
    api_fs_write,
    api_format
};

static int start_open = 0;
#define SM_W 160
#define SM_ITEM_H 26
#define SM_N 5
static const char* sm_items[SM_N] = { "Terminal", "Files", "About", "Wallpaper", "Reboot" };
static int sm_x(void) { return 4; }
static int sm_y(void) { return (int)vbe_height() - TASKBAR_H - SM_N * SM_ITEM_H - 4; }

static void draw_start_menu(int rx,int ry,int rw,int rh) {
    if (!start_open) return;
    int x = sm_x(), y = sm_y();
    int w = SM_W, h = SM_N * SM_ITEM_H + 4;
    if (!rects_overlap(rx,ry,rw,rh, x - 4, y - 4, w + 8, h + 8)) return;
    // Тень меню.
    draw_shadow(x, y, w, h);
    // Фон меню.
    fb_backbuffer_fill(x, y, w, h, COL_MENU_BG);
    fb_backbuffer_rect(x, y, w, h, COL_WIN_BORDER);
    // Пункты с hover-подсветкой.
    int mx = mouse_x(), my = mouse_y();
    for (int i = 0; i < SM_N; i++) {
        int iy = y + 2 + i * SM_ITEM_H;
        int hi = (mx >= x + 2 && mx < x + w - 2 && my >= iy && my < iy + SM_ITEM_H);
        unsigned int bg = hi ? COL_MENU_SEL : COL_MENU_BG;
        if (hi) fb_backbuffer_fill(x + 2, iy, w - 4, SM_ITEM_H, bg);
        bb_text(x + 12, iy + 8, sm_items[i], COL_TEXT, bg);
    }
}

// ============================================================
// КОНТЕКСТНОЕ МЕНЮ (ПКМ)
// ============================================================
static int ctx_open = 0;
static int ctx_x = 0, ctx_y = 0;
static int ctx_n = 0;
static const char* ctx_items[8];
static int ctx_action[8];   // код действия для пункта

// Коды действий контекстного меню.
#define CTX_NONE    0
#define CTX_REFRESH 1
#define CTX_NEWDIR  2
#define CTX_TERM    3
#define CTX_WALL    4
#define CTX_NEWFILE 5
#define CTX_RENAME  6

#define CTX_W 150
#define CTX_IH 22

// Открыть меню с текущим набором пунктов (вызывается из process_events).
static void ctx_open_at(int x, int y) {
    ctx_x = x; ctx_y = y; ctx_open = 1;
    // Базовый набор для рабочего стола.
    ctx_n = 0;
    ctx_items[ctx_n] = "Refresh";        ctx_action[ctx_n++] = CTX_REFRESH;
    ctx_items[ctx_n] = "New folder";     ctx_action[ctx_n++] = CTX_NEWDIR;
    ctx_items[ctx_n] = "New file";       ctx_action[ctx_n++] = CTX_NEWFILE;
    ctx_items[ctx_n] = "Terminal";       ctx_action[ctx_n++] = CTX_TERM;
    ctx_items[ctx_n] = "Wallpaper";      ctx_action[ctx_n++] = CTX_WALL;
}

static void draw_ctx_menu(int rx,int ry,int rw,int rh) {
    if (!ctx_open) return;
    int w = CTX_W, h = ctx_n * CTX_IH + 4;
    int x = ctx_x, y = ctx_y;
    // Не вылезать за экран.
    if (x + w > (int)vbe_width()) x = (int)vbe_width() - w;
    if (y + h > (int)vbe_height()) y = (int)vbe_height() - h;
    if (!rects_overlap(rx,ry,rw,rh, x - 3, y - 3, w + 6, h + 6)) return;
    draw_shadow(x, y, w, h);
    fb_backbuffer_fill(x, y, w, h, COL_MENU_BG);
    fb_backbuffer_rect(x, y, w, h, COL_WIN_BORDER);
    int mx = mouse_x(), my = mouse_y();
    for (int i = 0; i < ctx_n; i++) {
        int iy = y + 2 + i * CTX_IH;
        int hi = (mx >= x + 2 && mx < x + w - 2 && my >= iy && my < iy + CTX_IH);
        unsigned int bg = hi ? COL_MENU_SEL : COL_MENU_BG;
        if (hi) fb_backbuffer_fill(x + 2, iy, w - 4, CTX_IH, bg);
        bb_text(x + 10, iy + 7, ctx_items[i], COL_TEXT, bg);
    }
}

static int process_events(int handle_kbd) {
    int mx = mouse_x(), my = mouse_y();
    int left = mouse_left();
    int right = mouse_right();
    static int prev_right = 0;
    int changed = 0;

    if (handle_kbd) {
        int twin = find_win("Terminal");
        // Ввод принимаем ТОЛЬКО если окно терминала активно (в фокусе),
        // а не просто видимо — иначе печать уходила бы в неактивное окно.
        if (twin >= 0 && wins[twin].visible && wins[twin].active) {
            while (kb_available()) {
                char c = (char)kb_getchar();
                if (c == '\n' || c == '\r') { cmd_execute(); }
                else if (c == '\b') { if (cmd_len > 0) { cmd_len--; term_putc('\b'); } }
                else if (c >= ' ' && cmd_len < 120) {
                    cmd_buf[cmd_len++] = c; cmd_buf[cmd_len] = 0;
                    term_putc(c);
                }
                mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
            }
        }
    }

    // --- Контекстное меню (ПКМ) ---
    if (right && !prev_right) {
        ctx_open_at(mx, my);
        mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
        changed = 1;
    }
    prev_right = right;

    // Если меню открыто — сначала обрабатываем клик по нему.
    if (ctx_open && left && !prev_left) {
        int w = CTX_W, h = ctx_n * CTX_IH + 4;
        int x = ctx_x, y = ctx_y;
        if (x + w > (int)vbe_width()) x = (int)vbe_width() - w;
        if (y + h > (int)vbe_height()) y = (int)vbe_height() - h;
        if (mx >= x && mx < x + w && my >= y && my < y + h) {
            int idx = (my - y - 2) / CTX_IH;
            if (idx >= 0 && idx < ctx_n) {
                switch (ctx_action[idx]) {
                    case CTX_REFRESH: desktop_scan(); break;
                    case CTX_TERM:    run_program("/kernel/SYSPR/TERM.ELF"); break;
                    case CTX_WALL:    run_program("/kernel/SYSPR/WPICK.ELF"); break;
                    // Создаём на D1 (безопасно, не трогаем системный D0).
                    // vfs_init() НЕ зовём (перезаход в VFS посреди обработки клика).
                    case CTX_NEWDIR:  vfs_mkdir("D1:/NEWDIR"); desktop_scan(); break;
                    case CTX_NEWFILE: vfs_write("D1:/NEW.TXT", "x"); desktop_scan(); break;
                    default: break;
                }
            }
        }
        ctx_open = 0;
        mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
        changed = 1;
        prev_left = left;
        return changed;
    }

    if (left && !prev_left) {
        ctx_open = 0;   // клик вне меню — закрыть его
        int handled = 0;
        int tb_y = (int)vbe_height() - TASKBAR_H;

        if (my >= tb_y && my < tb_y + TASKBAR_H && mx >= 4 && mx < 64) {
            start_open = !start_open;
            mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
            handled = 1;
        }
        else if (my >= tb_y && my < tb_y + TASKBAR_H) {
            int bx = 70;
            for (int i = 0; i < MAXWIN; i++) {
                if (!wins[i].used || !wins[i].minimized) continue;
                if (mx >= bx && mx < bx + 90) {
                    wins[i].minimized = 0; wins[i].visible = 1; wins[i].active = 1;
                    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                    break;
                }
                bx += 96;
            }
            handled = 1;
        }
        else if (start_open) {
            int x = sm_x(), y = sm_y();
            if (mx >= x && mx < x + SM_W && my >= y && my < y + SM_N * SM_ITEM_H + 4) {
                int idx = (my - y - 2) / SM_ITEM_H;
                if (idx >= 0 && idx < SM_N) {
                    if (idx == 0 || idx == 1 || idx == 2 || idx == 3) {
                        const char* ep = (idx == 0) ? "/kernel/SYSPR/TERM.ELF"
                                       : (idx == 1) ? "/kernel/SYSPR/FILES.ELF"
                                       : (idx == 2) ? "/kernel/SYSPR/ABOUT.ELF"
                                                    : "/kernel/SYSPR/WPICK.ELF";
                        run_program(ep);   // запуск ELF вместо встроенного окна
                    } else {
                        __asm__ volatile ("cli");
                        while (inb(0x64) & 2);
                        outb(0x64, 0xFE);
                    }
                }
            }
            start_open = 0;
            mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
            handled = 1;
        }
        else {
            for (int i = 0; i < NICON && !handled; i++) {
                if (icon_hit(i, mx, my)) {
                    unsigned int now = timer_ticks();
                    if (i == last_icon && (now - last_icon_tick) < 40) {
                        if (i == 4)   // Disk D1 — Files с аргументом "D1:/"
                            api_run_program_arg(dicons[i].elf, "D1:/");
                        else
                            run_program(dicons[i].elf);   // запуск ELF из /kernel/SYSPR/
                    }
                    last_icon = i;
                    last_icon_tick = now;
                    handled = 1;
                }
            }

            // Динамические иконки из корня: двойной клик.
            for (int i = 0; i < dyn_count && !handled; i++) {
                int ix = dyn_icons[i].x, iy = dyn_icons[i].y;
                if (mx >= ix && mx < ix + 32 && my >= iy && my < iy + 32) {
                    unsigned int now = timer_ticks();
                    if ((i + NICON) == last_icon && (now - last_icon_tick) < 40) {
                        char full[160]; full[0] = '/'; int p = 1;
                        const char* nm = dyn_names[i];
                        for (int k = 0; nm[k] && p < 158; k++) full[p++] = nm[k];
                        full[p] = 0;
                        if (dyn_isdir[i]) {
                            run_program("/kernel/SYSPR/FILES.ELF");   // открыть файловый менеджер
                        } else if (name_has_ext(nm, "elf")) {
                            run_program(full);
                        } else if (name_has_ext(nm, "bmp")) {
                            api_run_program_arg("/kernel/SYSPR/IMGVIEW.ELF", full);  // просмотрщик
                        } else if (name_has_ext(nm, "txt")) {
                            api_run_program_arg("/kernel/SYSPR/EDITOR.ELF", full);   // редактор
                        }
                    }
                    last_icon = i + NICON;   // смещаем, чтобы не путать с системными
                    last_icon_tick = now;
                    handled = 1;
                }
            }

            for (int i = MAXWIN - 1; i >= 0 && !handled; i--) {
                window_t* w = &wins[i];
                if (!w->used || !w->visible) continue;
                int cbx = w->x + w->w - 18, cby = w->y + 4;
                int hit_btn = (my >= cby && my < cby + 12) ? 1 : 0;
                if (hit_btn && mx >= cbx && mx < cbx + 12) {
                    mark_dirty(w->x - 4, w->y - 4, w->w + 8, w->h + 8);
                    if (w->is_program) {
                        buf_free(w->buf_idx);
                        w->used = 0; w->buf = 0; w->buf_idx = -1;
                        if (!handle_kbd) g_exit_request = 1;
                    } else {
                        w->visible = 0;
                    }
                    drag_win = -1; handled = 1; break;
                }
                if (hit_btn && mx >= cbx - 16 && mx < cbx - 16 + 12) {
                    mark_dirty(w->x - 4, w->y - 4, w->w + 8, w->h + 8);
                    if (!w->maximized) {
                        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
                        w->x = 0; w->y = 0;
                        if (!w->is_program) {
                            w->w = (int)vbe_width(); w->h = (int)vbe_height() - TASKBAR_H;
                        }
                        w->maximized = 1;
                    } else {
                        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
                        w->maximized = 0;
                    }
                    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                    drag_win = -1; handled = 1; break;
                }
                if (hit_btn && mx >= cbx - 32 && mx < cbx - 32 + 12) {
                    mark_dirty(w->x - 4, w->y - 4, w->w + 8, w->h + 8);
                    w->minimized = 1; w->visible = 0; drag_win = -1;
                    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                    handled = 1; break;
                }
                if (in_title(w, mx, my)) {
                    bring_to_front(i);
                    drag_win = MAXWIN - 1;
                    for (int j = 0; j < MAXWIN; j++) wins[j].active = (j == MAXWIN - 1) ? 1 : 0;
                    wins[MAXWIN - 1].grab_dx = mx - wins[MAXWIN - 1].x;
                    wins[MAXWIN - 1].grab_dy = my - wins[MAXWIN - 1].y;
                    for (int j = 0; j < MAXWIN; j++)
                        if (wins[j].used && wins[j].visible)
                            mark_dirty(wins[j].x - 4, wins[j].y - 4, wins[j].w + 8, wins[j].h + 8);
                    handled = 1; break;
                }
                // Клик в клиентской области окна Files: выбор файла.
                // Двойной клик по .ELF — запуск программы.
                if (!w->is_program && w->title[0] == 'F' && w->title[1] == 'i' &&
                    my >= w->y + 24 && my < w->y + w->h) {
                    // Фокус на это окно.
                    for (int j = 0; j < MAXWIN; j++) wins[j].active = (j == i) ? 1 : 0;
                    int fi = files_pick(w, mx, my);
                    if (fi == -2) {
                        // Двойной клик по ".." — выйти на уровень выше.
                        unsigned int now = timer_ticks();
                        if (g_files_last_idx == -2 && (now - g_files_last_tick) < 40) {
                            files_go_up();
                            g_files_last_idx = -1;
                            mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                        } else { g_files_last_idx = -2; g_files_last_tick = now; }
                    } else if (fi >= 0) {
                        unsigned int now = timer_ticks();
                        if (fi == g_files_last_idx && (now - g_files_last_tick) < 40) {
                            const char* nm = f_names + fi * 13;
                            if (f_isdir[fi]) {
                                // Двойной клик по каталогу — войти в него.
                                files_enter(nm);
                                mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                            } else {
                                // Двойной клик по .ELF — запустить программу.
                                int is_elf = 0;
                                for (int k = 0; nm[k]; k++) {
                                    if (nm[k] == '.' && (nm[k+1]|0x20) == 'e' && (nm[k+2]|0x20) == 'l' && (nm[k+3]|0x20) == 'f')
                                        is_elf = 1;
                                }
                                if (is_elf) {
                                    char full[160];
                                    files_full_path(nm, full);
                                    run_program(full);
                                    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                                }
                            }
                            g_files_last_idx = -1;
                        } else {
                            g_files_last_idx = fi;
                            g_files_last_tick = now;
                        }
                    }
                    handled = 1; break;
                }
            }
        }
        prev_left = left; changed = 1;
    }

    if (!left) drag_win = -1;

    if (drag_win >= 0 && drag_win < MAXWIN && wins[drag_win].used && wins[drag_win].visible) {
        window_t* w = &wins[drag_win];
        int nx = mx - w->grab_dx;
        int ny = my - w->grab_dy;
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > (int)vbe_width() - 20)  nx = (int)vbe_width() - 20;
        if (ny > (int)vbe_height() - 20) ny = (int)vbe_height() - 20;
        if (nx != w->x || ny != w->y) {
            int ox = w->x, oy = w->y;
            w->x = nx; w->y = ny;
            mark_dirty(ox - 4, oy - 4, w->w + 8, w->h + 8);
            mark_dirty(nx - 4, ny - 4, w->w + 8, w->h + 8);
            changed = 1;
        }
    }
    prev_left = left;
    return changed;
}

// Тень окна: несколько полупрозрачных смещённых прямоугольников.
// Дёшево (плоские заливки), даёт ощущение «приподнятости».
static void draw_shadow(int x, int y, int w, int h) {
    // Один плотный слой смещён на 4px, полутень на 7px.
    fb_backbuffer_fill(x + 4, y + 4, w, h, FB_RGB(0x08, 0x08, 0x0E));
}

// Градиентная заливка по вертикали: от c1 (верх) к c2 (низ).
static void fill_grad_v(int x, int y, int w, int h, unsigned int c1, unsigned int c2) {
    for (int yy = 0; yy < h; yy++) {
        int t = (h > 1) ? (yy * 255 / (h - 1)) : 0;
        int r = (((c1 >> 16) & 0xFF) * (255 - t) + ((c2 >> 16) & 0xFF) * t) / 255;
        int g = (((c1 >> 8) & 0xFF) * (255 - t) + ((c2 >> 8) & 0xFF) * t) / 255;
        int b = ((c1 & 0xFF) * (255 - t) + (c2 & 0xFF) * t) / 255;
        fb_backbuffer_fill(x, y + yy, w, 1, FB_RGB(r, g, b));
    }
}

// Скруглить углы: затереть «уголки» цветом фона (простое сглаживание).
static void round_corners(int x, int y, int w, int h, unsigned int col) {
    // Отсекаем по 2 пикселя в каждом углу (радиус 2).
    fb_backbuffer_pixel(x, y, col);           fb_backbuffer_pixel(x + 1, y, col);
    fb_backbuffer_pixel(x, y + 1, col);
    fb_backbuffer_pixel(x + w - 1, y, col);   fb_backbuffer_pixel(x + w - 2, y, col);
    fb_backbuffer_pixel(x + w - 1, y + 1, col);
    fb_backbuffer_pixel(x, y + h - 1, col);   fb_backbuffer_pixel(x + 1, y + h - 1, col);
    fb_backbuffer_pixel(x, y + h - 2, col);
    fb_backbuffer_pixel(x + w - 1, y + h - 1, col); fb_backbuffer_pixel(x + w - 2, y + h - 1, col);
    fb_backbuffer_pixel(x + w - 1, y + h - 2, col);
}

static void draw_window(window_t* w, int rx, int ry, int rw, int rh) {
    if (!w->used || !w->visible) return;
    if (!rects_overlap(rx, ry, rw, rh, w->x - 4, w->y - 4, w->w + 8, w->h + 8)) return;

    // Тень.
    draw_shadow(w->x, w->y, w->w, w->h);

    // Тело окна.
    fb_backbuffer_fill(w->x, w->y, w->w, w->h, COL_WIN_BG);

    // Заголовок: градиент (активный — синий, неактивный — серый).
    unsigned int t1 = w->active ? COL_TITLE_ACT : COL_TITLE_INA;
    unsigned int t2 = w->active ? COL_ACCENT : FB_RGB(0x22, 0x23, 0x2E);
    fill_grad_v(w->x, w->y, w->w, 20, t1, t2);

    // Кнопки окна.
    draw_icon(w->x + w->w - 18, w->y + 4, &ICON_BTN_CLOSE);
    draw_icon(w->x + w->w - 34, w->y + 4, &ICON_BTN_MAX);
    draw_icon(w->x + w->w - 50, w->y + 4, &ICON_BTN_MIN);

    // Заголовок (текст) — по центру заголовка.
    bb_text(w->x + 8, w->y + 6, w->title, COL_TEXT, t1);

    // Тонкая рамка.
    fb_backbuffer_rect(w->x, w->y, w->w, w->h, COL_WIN_BORDER);

    // Содержимое.
    if (w->is_program && w->buf) {
        int ch = w->h - 20;
        fb_backbuffer_blit(w->x, w->y + 20, w->buf, w->w, ch, w->w * 3);
    } else if (w->draw_content) {
        w->draw_content(w);
    }

    // (Скругление углов отключено: затирало обои под окном. Вернём с alpha-блендом позже.)
    (void)round_corners;
}

static void draw_taskbar_region(int rx,int ry,int rw,int rh) {
    int y = (int)vbe_height() - TASKBAR_H;
    int sw = (int)vbe_width();
    if (!rects_overlap(rx,ry,rw,rh, 0,y,sw,TASKBAR_H)) return;
    // Градиент фона таскбара (заметный: светлее сверху).
    fill_grad_v(0, y, sw, TASKBAR_H, FB_RGB(0x3A, 0x3D, 0x52), FB_RGB(0x16, 0x17, 0x20));
    fb_backbuffer_line(0, y, sw, y, COL_SEP);

    // Кнопка Start с hover.
    int mx = mouse_x(), my = mouse_y();
    int start_hi = (mx >= 4 && mx < 74 && my >= y + 3 && my < y + TASKBAR_H - 3) || start_open;
    unsigned int sc = start_hi ? COL_ACCENT_HI : COL_ACCENT;
    fb_backbuffer_fill(4, y + 4, 70, TASKBAR_H - 8, sc);
    bb_text(12, y + 10, "Start", COL_TEXT, sc);

    // Кнопки свёрнутых окон.
    int bx = 80;
    for (int i = 0; i < MAXWIN; i++) {
        if (!wins[i].used || !wins[i].minimized) continue;
        int hi = (mx >= bx && mx < bx + 100 && my >= y + 3 && my < y + TASKBAR_H - 3);
        unsigned int bc = hi ? COL_TASKBAR_HI : FB_RGB(0x33, 0x35, 0x46);
        fb_backbuffer_fill(bx, y + 4, 100, TASKBAR_H - 8, bc);
        bb_text(bx + 8, y + 10, wins[i].title, COL_TEXT, bc);
        bx += 106;
    }

    // Счётчик секунд справа (тики / 100). Позже — RTC-часы.
    unsigned int tsec = timer_ticks() / 100;
    char clk[16]; int cp = 0; clk[cp++] = '[';
    char nb[12]; int np = 0; unsigned int tv = tsec;
    if (tv == 0) nb[np++] = '0';
    while (tv) { nb[np++] = '0' + (tv % 10); tv /= 10; }
    while (np) clk[cp++] = nb[--np];
    clk[cp++] = ']'; clk[cp] = 0;
    bb_text(sw - 60, y + 10, clk, COL_TEXT_DIM, COL_TASKBAR);
}

static const unsigned char CUR[19][12] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},{2,2,0,0,0,0,0,0,0,0,0,0},{2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},{2,1,1,1,2,0,0,0,0,0,0,0},{2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},{2,1,1,1,1,1,1,2,0,0,0,0},{2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},{2,1,1,1,1,1,1,1,1,1,2,0},{2,1,1,1,1,1,1,2,2,2,2,2},
    {2,1,1,1,2,1,1,2,0,0,0,0},{2,1,1,2,0,2,1,1,2,0,0,0},{2,1,2,0,0,2,1,1,2,0,0,0},
    {2,2,0,0,0,0,2,1,1,2,0,0},{0,0,0,0,0,0,2,1,1,2,0,0},{0,0,0,0,0,0,0,2,1,2,0,0},
    {0,0,0,0,0,0,0,2,2,0,0,0}
};

// Нарисовать ОБЛАСТЬ обоев (rx,ry,rw,rh) в backbuffer. Если обоев нет —
// fallback: сплошная синяя заливка (как было раньше).
static void wall_blit_region(int rx, int ry, int rw, int rh) {
    if (!g_wall_on || !g_wall) {
        fb_backbuffer_fill(rx, ry, rw, rh, COL_DESKTOP);
        return;
    }
    // Копируем построчно только нужный прямоугольник из буфера обоев.
    // pitch обоев = vbe_width()*3 (ровно экран). fb_backbuffer_blit умеет
    // обрезку по экрану, поэтому координаты rx..rx+rw безопасны.
    int sw = (int)vbe_width();
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= g_wall_h) continue;
        const unsigned char* srow = g_wall + ((unsigned int)y * (unsigned int)sw + (unsigned int)rx) * 3u;
        fb_backbuffer_blit(rx, y, srow, rw, 1, sw * 3);
    }
}

static void render_region(int rx,int ry,int rw,int rh) {
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int)vbe_width())  rw = (int)vbe_width() - rx;
    if (ry + rh > (int)vbe_height()) rh = (int)vbe_height() - ry;
    if (rw <= 0 || rh <= 0) return;
    wall_blit_region(rx, ry, rw, rh);
    draw_icons_region(rx, ry, rw, rh);
    for (int i = 0; i < MAXWIN; i++) draw_window(&wins[i], rx, ry, rw, rh);
    draw_taskbar_region(rx, ry, rw, rh);
    draw_start_menu(rx, ry, rw, rh);
    draw_ctx_menu(rx, ry, rw, rh);
}

static void draw_cursor_bb(int mx, int my) {
    for (int row = 0; row < 19; row++)
        for (int col = 0; col < 12; col++) {
            unsigned char m = CUR[row][col];
            if (m == 1) fb_backbuffer_pixel(mx + col, my + row, 0xFFFFFF);
            else if (m == 2) fb_backbuffer_pixel(mx + col, my + row, 0x000000);
        }
}

static void cmd_prompt(void) { term_print("[/]> "); cmd_len = 0; cmd_buf[0] = 0; }

static void cmd_execute(void) {
    term_newline();
    const char* h = "help";
    int is_help = 1;
    for (int i = 0; h[i]; i++) if (cmd_buf[i] != h[i]) { is_help = 0; break; }
    if (is_help && cmd_len == 4) {
        term_print("commands: help, ls, cat <f>, ver, clear\n");
        term_print("  mofsf.msfs -D0   - format disk as MSFS\n");
        term_print("  mofsf.fat -16 -D1 - format disk as FAT16\n");
    } else if (cmd_len == 2 && cmd_buf[0] == 'l' && cmd_buf[1] == 's') {
        extern int g_out_to_term;
        g_out_to_term = 1;
        ata_select(0);
        vfs_list_print("/");
        g_out_to_term = 0;
    } else if (cmd_len == 2 && cmd_buf[0] == 'f' && cmd_buf[1] == 's') {
        extern void ata_read_sector(unsigned int, unsigned char*);
        static unsigned char sb[512];
        ata_select(0);
        ata_read_sector(0, sb);
        term_print("hdb sec0: ");
        for (int i = 0; i < 16; i++) {
            char ch = (char)sb[i];
            term_putc(ch >= 32 && ch < 127 ? ch : '.');
        }
        term_print(" bps=");
        unsigned short bps = sb[11] | (sb[12] << 8);
        term_print_int((int)bps);
        term_newline();
    } else if (cmd_len > 6 && cmd_buf[0]=='m' && cmd_buf[1]=='o' && cmd_buf[2]=='f' &&
               cmd_buf[3]=='s' && cmd_buf[4]=='f' && cmd_buf[5]=='.') {
        // mofsf.<fs> [-<ver>] -D<n>   — форматировать диск.
        // Разбираем: fs = после 'mofsf.', затем флаги.
        char fsname[8]; int fp = 0;
        int i = 6;
        while (cmd_buf[i] && cmd_buf[i] != ' ' && fp < 7) fsname[fp++] = cmd_buf[i++];
        fsname[fp] = 0;
        int drv = 0, ver = 16;
        // Парсим остаток: ищем -D<n> и -<ver>.
        for (int k = i; k < cmd_len; k++) {
            if (cmd_buf[k] == '-' && (cmd_buf[k+1]=='D' || cmd_buf[k+1]=='d') &&
                cmd_buf[k+2] >= '0' && cmd_buf[k+2] <= '9') {
                drv = cmd_buf[k+2] - '0';
            } else if (cmd_buf[k] == '-' && cmd_buf[k+1] >= '0' && cmd_buf[k+1] <= '9') {
                ver = 0; int k2 = k + 1;
                while (cmd_buf[k2] >= '0' && cmd_buf[k2] <= '9') { ver = ver * 10 + (cmd_buf[k2] - '0'); k2++; }
            }
        }
        // Сравнение имени ФС.
        int is_msfs = (fsname[0]=='m'&&fsname[1]=='s'&&fsname[2]=='f'&&fsname[3]=='s'&&fsname[4]==0);
        int is_fat  = (fsname[0]=='f'&&fsname[1]=='a'&&fsname[2]=='t'&&fsname[3]==0);
        term_print("mofsf: formatting D"); term_print_int(drv); term_print("...\n");
        ata_select(drv); fat_use_drive(drv); msfs_use_drive(drv);
        int ok = 0;
        if (is_msfs) { msfs_format(); ok = msfs_probe(); }
        else if (is_fat) { ok = fat_format(ver); }
        else { term_print("mofsf: unknown fs (use msfs or fat)\n"); }
        if (is_msfs || is_fat) {
            term_print(ok ? "mofsf: done\n" : "mofsf: FAILED\n");
            // Переинициализировать VFS (переопределить ФС на дисках).
            vfs_init();
            desktop_scan();
            mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
        }
    } else if (cmd_len > 4 && cmd_buf[0]=='r' && cmd_buf[1]=='u' && cmd_buf[2]=='n' && cmd_buf[3]==' ') {
        term_print("running "); term_print(cmd_buf + 4); term_print("...\n");
        int rc = run_program(cmd_buf + 4);
        term_print("[exit "); term_print_int(rc); term_print("]\n");
    } else if (cmd_len == 3 && cmd_buf[0] == 'v' && cmd_buf[1] == 'e' && cmd_buf[2] == 'r') {
        term_print("MysticOS 2 graphics\n");
    } else if (cmd_len == 5 && cmd_buf[0]=='c'&&cmd_buf[1]=='l'&&cmd_buf[2]=='e'&&cmd_buf[3]=='a'&&cmd_buf[4]=='r') {
        term_clear();
    } else if (cmd_len > 0) {
        term_print("unknown command: ");
        term_print(cmd_buf);
        term_newline();
    }
    cmd_prompt();
}

int exec_run(const unsigned char* image, unsigned int size, const mystic_api_t* api);
static unsigned char prog_buf[256 * 1024];

// Защита от реентрантности: пока программа работает, повторный run_program
// (например, из рекурсивного process_events по всё ещё нажатой мыши) запрещён.
// Иначе WINAPP -> yield -> process_events -> WINAPP -> ... перетирает стек.

static int run_program(const char* name) {
    // Обычный запуск БЕЗ аргумента: сбросить аргумент, чтобы новая программа
    // не получила путь от предыдущей (баг: FILES открывался по чужому пути).
    g_prog_arg[0] = 0;
    // Ограничение "одна программа" снято: каждая задача изолирована (своя PD).
    // prog_buf используется только на время загрузки — это безопасно.
    ata_select(0);
    int sz = vfs_read_file(name, prog_buf, sizeof(prog_buf));
    if (sz <= 0) return -1;
    while (kb_available()) kb_getchar();
    // Создаём ЗАДАЧУ (своё адресное пространство) вместо блокирующего exec_run.
    int id = task_create_elf(prog_buf, (unsigned int)sz, &g_api);
    if (id < 0) return -1;
    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
    return 0;
}

void kernel_main(void) {
    if (!vbe_init()) { for (;;) __asm__ volatile ("hlt"); }
    fb_backbuffer_init();
    // Буфер обоев: фиксированный адрес в верхней RAM (см. WALL_BUF_ADDR).
    g_wall = (unsigned char*)WALL_BUF_ADDR;
    g_wall_on = 0;   // загрузим дефолт ниже, после инициализации ATA
    // Включить страничную адресацию (identity, PSE 4 МБ). Если система уйдёт
    // в перезагрузку/зависание сразу после старта — закомментируй эту строку.
    paging_init();
    pmem_init();
    icons_init();
    icons_layout();
    windows_init();
    task_init();   // ядро = задача 0; задачи ELF получают своё адресное пространство
    gdt_init_all();   // GDT с ring3 + TSS (этап ring3)
    idt_init();
    pic_init();
    pit_init(100);
    mouse_init();
    interrupts_enable();
    task_enable(); // включить кооперативное переключение (api_yield -> task_yield)

    ata_select(0);
    vfs_init();   // определить ФС и выбрать активный драйвер
    desktop_scan();   // иконки файлов из корня диска (кроме /kernel)

    // Загрузить дефолтные обои (BMP) из образа. Если файла нет/не BMP —
    // останется fallback-заливка (wall_blit_region).
    wall_load("/kernel/BG/BG.BMP");   // FAT16 8.3: каталог 'BG' (не BACKGROUND)

    // Окон при старте нет — все создаются по требованию. Приветствие терминала
    // печатается в open_sys_win() при первом создании его окна.

    render_region(0, 0, (int)vbe_width(), (int)vbe_height());
    int mx = mouse_x(), my = mouse_y();
    draw_cursor_bb(mx, my);
    fb_flip();
    int old_mx = mx, old_my = my;

    for (;;) {
        // Освобождаем память завершённых программ.
        task_reap();
        // Клавиатуру забирает ядро (терминал) ТОЛЬКО когда программа не работает.
        int prog_alive = task_any_alive();
        int changed = process_events(prog_alive ? 0 : 1);
        mx = mouse_x(); my = mouse_y();

        int rx = mx < old_mx ? mx : old_mx;
        int ry = my < old_my ? my : old_my;
        int rw = (mx > old_mx ? mx : old_mx) - rx + 12;
        int rh = (my > old_my ? my : old_my) - ry + 19;

        if (dirty_w > 0 && dirty_h > 0) {
            if (dirty_x < rx) { rw += rx - dirty_x; rx = dirty_x; }
            if (dirty_y < ry) { rh += ry - dirty_y; ry = dirty_y; }
            int dx2 = dirty_x + dirty_w, dy2 = dirty_y + dirty_h;
            if (dx2 > rx + rw) rw = dx2 - rx;
            if (dy2 > ry + rh) rh = dy2 - ry;
        }

        // Постоянное мигание курсора терминала (пока он активен), независимо
        // от событий. Фаза меняется по таймеру, область окна метится dirty.
        int twin = find_win("Terminal");
        if (twin >= 0 && wins[twin].visible && wins[twin].active && drag_win < 0) {
            unsigned int nowt = timer_ticks();
            if (nowt - g_term_cursor_tick >= 50) {
                g_term_cursor_tick = nowt;
                g_term_cursor_on = !g_term_cursor_on;
                mark_dirty(wins[twin].x - 4, wins[twin].y - 4, wins[twin].w + 8, wins[twin].h + 8);
            }
        }

        // ВСЕГДА включаем таскбар (низ экрана) в область перерисовки:
        // иначе hover кнопки Start/окон «залипает» при уходе мыши.
        {
            int full_h = (int)vbe_height();
            int tby = full_h - TASKBAR_H - 2;   // чуть выше таскбара (рамка)
            if (ry < tby) { rh = full_h - ry; }         // расширить до низа
            else { ry = tby; rh = full_h - tby; }        // область только таскбар
        }
        if (start_open) {
            // Меню тоже перерисовываем (hover пунктов).
            int sy = sm_y();
            if (sy < ry) { rh += ry - sy; ry = sy; }
        }

        if (mx != old_mx || my != old_my || changed || dirty_w > 0) {
            render_region(rx, ry, rw, rh);
            draw_cursor_bb(mx, my);
            fb_flip_rect(rx, ry, rw, rh);
            // ВАЖНО: render_region рисует таскбар и меню ЦЕЛИКОМ (по всей
            // ширине), но fb_flip_rect скопировал только область курсора.
            // Копируем таскбар и меню ПОЛНОСТЬЮ (иначе Start/меню не обновятся).
            {
                int fh = (int)vbe_height();
                int fw = (int)vbe_width();
                fb_flip_rect(0, fh - TASKBAR_H, fw, TASKBAR_H);
                if (start_open)
                    fb_flip_rect(sm_x(), sm_y(), SM_W, SM_N * SM_ITEM_H + 4);
                if (ctx_open) {
                    int cw = CTX_W, chh = ctx_n * CTX_IH + 4;
                    int cxm = ctx_x, cym = ctx_y;
                    if (cxm + cw > fw) cxm = fw - cw;
                    if (cym + chh > fh) cym = fh - chh;
                    fb_flip_rect(cxm, cym, cw, chh);
                }
            }
            old_mx = mx; old_my = my;
            dirty_w = 0; dirty_h = 0;
        }

        // Уступить CPU задачам-программам (кооперативно). Вернёмся сюда,
        // когда программа вызовет api->yield() или завершится.
        task_yield();

        __asm__ volatile ("hlt");
    }
}
