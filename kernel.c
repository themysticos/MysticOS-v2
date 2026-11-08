// kernel.c - MysticOS 2: рабочий стол + мышь с частичной перерисовкой.
// Окна УНИФИЦИРОВАНЫ: системные окна и окна ELF-программ — один тип window_t.
#include "vbe.h"
#include "idt.h"
#include "pic.h"
#include "icons.h"
#include "io.h"
#include "ata.h"
#include "pe.h"
#include "fat.h"
#include "console.h"
#include "term.h"
#include "api.h"
#include "task.h"
#include "paging.h"
#include "pmem.h"
#include "mmdcm.h"
#include "bmp.h"
#include "gif.h"
#include "fs.h"
#include "msfs.h"
#include "gdt.h"
#include "dll/msdll.h"
#include "pci.h"
#include "driver.h"
#include "serial.h"
#include "virtio.h"
#include "virtio_net.h"
#include "net.h"
#include "tcp.h"
#include "gpu.h"
#include "theme.h"
#include "apic.h"
#include "vss.h"
#include "hda.h"
#include "ac97.h"
#include "amdgpu.h"
#include "music.h"

// ============================================================
// SYSCALL (int 0x80) — заглушка в начале файла (настоящий диспетчер
// см. ниже после всех api_* — он слишком длинный, чтобы держать его тут,
// а api_* ссылаются на forward-decl). Эта пустышка нужна, чтобы
// существующий entry.asm:isr128 слинковался.
// ============================================================
void syscall_handler_dispatch(unsigned int* raw);   // настоящий — ниже

void syscall_handler(unsigned int* regs) {
    syscall_handler_dispatch(regs);
}

void put_char(char c);
void print_string(const char* s);
void print_int(int v);
void print_hex(unsigned int v);

extern const unsigned char* font_glyph8x8(char c);
extern void exec_do_exit(int code);

// bg_cache (определена ниже, в блоке render_region): инвалидирует кэш
// нижнего слоя (обои+иконки). Зовётся при смене обоев / scan диска.
static void bg_cache_invalidate(void);

#define TASKBAR_H 28

// Запас вокруг окна для dirty-области: тень расширяется на ~9px наружу,
// поэтому 4px (как было) не хватало — оставались следы при drag.
// Берём 14px с запасом (тень + скругление).
#define WIN_SHADOW_PAD 14

// Запас вокруг курсора (12x19) для региона перерисовки. Без него остаётся
// тонкий след из битых пикселей при быстром движении.
#define CUR_PAD 2

// ============================================================
// ПАЛИТРА (современная тёмная тема). Одна таблица на всю ОС.
// ============================================================
// Цвета теперь берутся из ТЕМЫ (theme.c). COL_* - макросы к g_theme,
// поэтому весь существующий код работает без изменений, а тему можно
// переключать (theme_set). Стиль: Windows 11 (см. theme.c).
#define COL_DESKTOP    g_theme.desktop
#define COL_WIN_BG     g_theme.win_bg
#define COL_WIN_BORDER g_theme.win_border
#define COL_TITLE_ACT  g_theme.title_act
#define COL_TITLE_INA  g_theme.title_ina
#define COL_TEXT       g_theme.text
#define COL_TEXT_DIM   g_theme.text_dim
#define COL_ACCENT     g_theme.accent
#define COL_ACCENT_HI  g_theme.accent_hi
#define COL_SEL        g_theme.sel
#define COL_TASKBAR    g_theme.taskbar
#define COL_TASKBAR_HI g_theme.taskbar_hi
#define COL_MENU_BG    g_theme.menu_bg
#define COL_MENU_SEL   g_theme.menu_sel
#define COL_SHADOW     g_theme.shadow
#define COL_SEP        g_theme.sep

#define MAXWIN     8
#define NWIN_SYS   3
#define PWIN_MAXW  800
#define PWIN_MAXH  600

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
    // Статичный текст: ОТДЕЛЬНЫЙ буфер (нижний слой). Анимация в buf его
    // не портит. Если buf залит WIN_TRANSPARENT — сквозь него виден этот текст.
    unsigned char* text_buf;
    int text_buf_idx;              // индекс буфера в пуле или -1
    // dirty: 1 = содержимое окна изменилось с прошлой перерисовки.
    // Ставится любой api_window_pixel/fill/text/... и сбрасывается
    // в api_yield_impl после успешной отрисовки. Нужен, чтобы НЕ
    // перерисовывать окна, которые не менялись (иначе при maximize
    // на весь экран перерисовывался весь экран каждый кадр -> лаги).
    int dirty;
    // task_id: какая задача владеет окном. Нужно, чтобы при закрытии
    // окна крестиком помечать на выход ИМЕННО ЕЁ, а не ту, что случайно
    // сейчас в api_yield (старый баг g_exit_request убивал чужую задачу).
    int task_id;
    // alpha: базовая непрозрачность ВСЕГО окна (0..255).
    // 255 = плотное (по умолчанию). Меньше — полупрозрачное.
    // Углы и тень считаются динамически в draw_window (без массива маски).
    unsigned char alpha;
} window_t;

static window_t wins[MAXWIN];
static int win_next_id = 100;

// Буферы окон: независимый пул, чтобы номер слота окна не влиял на буфер.
// Буфер k лежит по 0x06000000 + k * PWIN_MAXW * PWIN_MAXH * 3.
// Пул = 2*MAXWIN: на окно ДВА буфера — w->buf (анимация, верх) и
// w->text_buf (статичный текст, низ).
#define BUF_POOL (MAXWIN * 2)
static unsigned int g_buf_used = 0;   // битовая маска занятых буферов

static int buf_alloc(void) {
    for (int k = 0; k < BUF_POOL; k++)
        if (!(g_buf_used & (1u << k))) { g_buf_used |= (1u << k); return k; }
    return -1;
}
static void buf_free(int k) {
    if (k >= 0 && k < BUF_POOL) g_buf_used &= ~(1u << k);
}
static unsigned char* buf_ptr(int k) {
    return (unsigned char*)(0x06000000u + (unsigned int)k * PWIN_MAXW * PWIN_MAXH * 3u);
}

// ============================================================
// ОБОИ РАБОЧЕГО СТОЛА (слой 0 под окнами)
// ============================================================
// Буфер обоев: 800x600x24bpp = 1.44 МБ, фиксированный адрес выше буферов окон
// и НИЖЕ pmem (0x08000000).
// НОВАЯ КАРТА (PWIN_MAXW/H = 800x600):
//   16 буферов окон по 800*600*3 = 0x15F900
//     -> 0x06000000 .. 0x075F9000 (~23 МБ, ~6 PDE: 24..29)
//   bg_cache 0x07600000..0x0775F900  (1.44 МБ — обои+иконки, готовая сцена)
//   обои     0x07760000..0x078BF900  (1.44 МБ — сюда wall_load декодирует)
//   src      0x078C0000..0x08000000  (~7.75 МБ под сырой BMP)
// ВАЖНО: paging_task_pd_alloc копирует всю ядровую PD, поэтому новые
// PDE (24..29) автоматически identity во всех задачах.
//
// bg_cache — кэш "нижнего слоя": обои + иконки. Рисуется заново только
// когда меняется фон/иконки; при drag окон — просто копируется.
// Огромная экономия: render_region при drag не перерисовывает обои.
#define BG_CACHE_ADDR  0x07600000u
#define WALL_BUF_ADDR  0x07760000u
// Буфер под СЫРОЙ файл BMP (читаем с диска целиком), до pmem. ~7.75 МБ лимит.
#define WALL_SRC_ADDR  0x078C0000u
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
    bg_cache_invalidate();   // обои сменились — кэш невалиден
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

// Роутинг клавиатуры: ввод получает ТОЛЬКО задача активного (в фокусе)
// окна. Раньше kb_available/kb_getchar были глобальными, и при двух
// окнах-«писателях» клавиши распределялись между задачами случайно
// (обе печатали). Теперь фокус — по активному окну: кликнул окно —
// его задача получает ввод.
//
// Возвращает task_id активного видимого окна или -1, если такого нет.
static int focused_task_id(void) {
    for (int i = 0; i < MAXWIN; i++) {
        if (wins[i].used && wins[i].visible && wins[i].active)
            return wins[i].task_id;
    }
    return -1;
}

static int drag_win = -1;
static int prev_left = 0;

static int dirty_x = 0, dirty_y = 0, dirty_w = 0, dirty_h = 0;

// Профилировка рендера (тики PIT, 10мс/тик).
static unsigned int g_prof_render = 0;   // суммарное время render_region
static unsigned int g_prof_frames = 0;   // число отрисованных кадров
static unsigned int g_prof_frame_ticks = 0; // время api_window_frame (блит кадра Doom)
static unsigned int g_prof_frame_calls = 0; // число вызовов ms_frame
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
// Флаг валидности bg_cache (кэш нижнего слоя: обои+иконки).
// Объявлен здесь, т.к. нужен round_corners (выше места сборки кэша).
static int g_bg_cache_valid = 0;
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

// Системные иконки (фиксированные): My PC, Terminal, Docs, Wallpaper.
#define NICON_SYS 4
static desktop_icon_t dicons[NICON_SYS];

// Иконки ДИСКОВ — динамические: показываем только присутствующие (ata_present)
// и с распознанной ФС. Клик — открыть FILES с аргументом "D<n>:/".
static desktop_icon_t disk_icons[ATA_MAX_DRIVES];
static int  disk_drive[ATA_MAX_DRIVES];   // номер ATA-диска для иконки i
static int  disk_count = 0;

// Динамические иконки из корня диска (кроме /kernel).
#define MAXDYN 32
static desktop_icon_t dyn_icons[MAXDYN];
static char dyn_names[MAXDYN][13];       // 8.3 имена
static int  dyn_isdir[MAXDYN];
static int  dyn_count = 0;
static int last_icon = -1;
static unsigned int last_icon_tick = 0;

// Hover рабочего стола: какая иконка под курсором.
// kind: 0=нет, 1=системная, 2=диск, 3=динамическая. idx — индекс в группе.
static int g_hover_kind = 0;
static int g_hover_idx = -1;

// Метки иконок дисков ("Disk D0"..). Живут постоянно.
static char disk_labels[ATA_MAX_DRIVES][12];

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
}

// Пересобрать иконки ДИСКОВ: показать только реально подключённые
// ATA-устройства с распознанной ФС. Вызывать после vfs_init/format.
static void disks_layout(void) {
    disk_count = 0;
    int col_y = NICON_SYS * 86 + 24;   // под системными иконками
    for (int d = 0; d < ATA_MAX_DRIVES && disk_count < ATA_MAX_DRIVES; d++) {
        if (!ata_present(d)) continue;              // не подключён — нет иконки
        if (!vfs_drive_has_fs(d)) continue;         // нет ФС — не показываем
        int i = disk_count;
        disk_icons[i].x = 24;
        disk_icons[i].y = col_y + i * 86;
        disk_icons[i].ic = &ICON_DISK;
        // "Disk D0" .. "Disk D3"
        const char* p = "Disk D"; int k = 0;
        while (p[k]) { disk_labels[i][k] = p[k]; k++; }
        disk_labels[i][k++] = (char)('0' + d);
        disk_labels[i][k] = 0;
        disk_icons[i].label = disk_labels[i];
        disk_icons[i].elf = "/kernel/SYSPR/FILES.ELF";
        disk_drive[i] = d;
        disk_count++;
    }
    bg_cache_invalidate();
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
    bg_cache_invalidate();   // иконки пересобрались — кэш невалиден
}

// Пересчитать hover-иконку рабочего стола по позиции курсора.
// Зона наведения — 96x56 (иконка+подпись), как при отрисовке.
// При смене помечаем dirty старую и новую область.
static void update_icon_hover(void) {
    int mx = mouse_x(), my = mouse_y();
    int nk = 0, ni = -1;
    for (int i = 0; i < NICON_SYS; i++) {
        if (mx >= dicons[i].x - 8 && mx < dicons[i].x + 88 &&
            my >= dicons[i].y - 4 && my < dicons[i].y + 52) { nk = 1; ni = i; break; }
    }
    if (!nk) for (int i = 0; i < disk_count; i++) {
        if (mx >= disk_icons[i].x - 8 && mx < disk_icons[i].x + 88 &&
            my >= disk_icons[i].y - 4 && my < disk_icons[i].y + 52) { nk = 2; ni = i; break; }
    }
    if (!nk) for (int i = 0; i < dyn_count; i++) {
        if (mx >= dyn_icons[i].x - 8 && mx < dyn_icons[i].x + 84 &&
            my >= dyn_icons[i].y - 4 && my < dyn_icons[i].y + 52) { nk = 3; ni = i; break; }
    }
    if (nk != g_hover_kind || ni != g_hover_idx) {
        // Помечаем старую область.
        if (g_hover_kind == 1 && g_hover_idx < NICON_SYS)
            mark_dirty(dicons[g_hover_idx].x - 10, dicons[g_hover_idx].y - 6, 100, 60);
        else if (g_hover_kind == 2 && g_hover_idx < disk_count)
            mark_dirty(disk_icons[g_hover_idx].x - 10, disk_icons[g_hover_idx].y - 6, 100, 60);
        else if (g_hover_kind == 3 && g_hover_idx < dyn_count)
            mark_dirty(dyn_icons[g_hover_idx].x - 10, dyn_icons[g_hover_idx].y - 6, 96, 60);
        g_hover_kind = nk; g_hover_idx = ni;
        // Помечаем новую область.
        if (nk == 1)      mark_dirty(dicons[ni].x - 10, dicons[ni].y - 6, 100, 60);
        else if (nk == 2) mark_dirty(disk_icons[ni].x - 10, disk_icons[ni].y - 6, 100, 60);
        else if (nk == 3) mark_dirty(dyn_icons[ni].x - 10, dyn_icons[ni].y - 6, 96, 60);
    }
}

// Отрисовать ОДНУ иконку рабочего стола: hover-подложка + иконка (с
// увеличением при наведении) + подпись. hover=1 — под курсором.
static void draw_desktop_icon(int ix, int iy, const icon_t* ic, const char* label,
                              int hover, unsigned int fg, unsigned int shadow) {
    if (hover) {
        // Скруглённая полупрозрачная подложка (в духе выделения меню).
        fb_backbuffer_round_rect_alpha(ix - 6, iy - 4, 44, 56, 8, COL_ACCENT, 60);
        fb_backbuffer_round_rect_alpha(ix - 6, iy - 4, 44, 56, 8, COL_ACCENT_HI, 40);
        draw_icon_scaled(ix, iy, ic, 110);   // +10%
    } else {
        draw_icon(ix, iy, ic);
    }
    // Подпись: тень + основной текст. При hover — ярче (accent_hi).
    unsigned int tcol = hover ? COL_ACCENT_HI : fg;
    bb_text_t(ix + 3, iy + 37, label, shadow);
    bb_text_t(ix + 2, iy + 36, label, tcol);
}

static void draw_icons_region(int rx,int ry,int rw,int rh) {
    // Подпись рисуем ПРОЗРАЧНО (bb_text_t): фон — обои, а не сплошная плашка.
    // Тень (чёрный глиф на 1px ниже/правее) — для читаемости на светлых обоях.
    unsigned int fg = FB_RGB(0xFF,0xFF,0xFF);
    unsigned int shadow = FB_RGB(0x00,0x00,0x00);
    for (int i = 0; i < NICON_SYS; i++) {
        int ix = dicons[i].x, iy = dicons[i].y;
        if (!rects_overlap(rx,ry,rw,rh, ix - 10, iy - 6, 100, 60)) continue;
        draw_desktop_icon(ix, iy, dicons[i].ic, dicons[i].label,
                          g_hover_kind == 1 && g_hover_idx == i, fg, shadow);
    }
    // Иконки дисков (только присутствующие).
    for (int i = 0; i < disk_count; i++) {
        int ix = disk_icons[i].x, iy = disk_icons[i].y;
        if (!rects_overlap(rx,ry,rw,rh, ix - 10, iy - 6, 100, 60)) continue;
        draw_desktop_icon(ix, iy, disk_icons[i].ic, disk_icons[i].label,
                          g_hover_kind == 2 && g_hover_idx == i, fg, shadow);
    }
    // Динамические иконки из корня диска.
    for (int i = 0; i < dyn_count; i++) {
        int ix = dyn_icons[i].x, iy = dyn_icons[i].y;
        if (!rects_overlap(rx,ry,rw,rh, ix - 10, iy - 6, 96, 60)) continue;
        draw_desktop_icon(ix, iy, dyn_icons[i].ic, dyn_icons[i].label,
                          g_hover_kind == 3 && g_hover_idx == i, fg, shadow);
    }
}

static int icon_hit(int i, int mx, int my) {
    return mx >= dicons[i].x && mx < dicons[i].x + 32 &&
           my >= dicons[i].y && my < dicons[i].y + 32;
}

// (files-логика удалена из ядра: файловый менеджер теперь полностью
//  реализован в FILES.ELF через API (fs_list/fs_read/run_program_arg).
//  Ядру не нужно дублировать навигацию по ФС — это ответственность программы.)

// Мигание курсора терминала: период ~0.5с (50 тиков по 10мс).
static unsigned int g_term_cursor_tick = 0;
static int g_term_cursor_on = 0;

// (draw_content удалён: все окна теперь программные — рисуют сами через API.
//  Ветка "else if (w->draw_content)" в draw_window оставлена как задел на
//  будущие не-ELF системные окна.)

static void windows_init(void) {
    for (int i = 0; i < MAXWIN; i++) {
        wins[i].used = 0; wins[i].buf = 0; wins[i].buf_idx = -1; wins[i].draw_content = 0;
        wins[i].dirty = 0;
        wins[i].task_id = 0;
        wins[i].alpha = 255;
        wins[i].text_buf = 0; wins[i].text_buf_idx = -1;
        wins[i].active = 0; wins[i].visible = 0;
        wins[i].maximized = 0; wins[i].minimized = 0; wins[i].is_program = 0;
    }
    // НИЧЕГО не создаём. Все окна создаются по требованию:
    // ELF-программы — через api_window_create из api_run_program_arg/run_program.
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
        w->dirty = 1;   // первый кадр нарисовать
        w->task_id = task_current();   // кто владеет окном
        w->alpha = 255;                 // по умолчанию окно плотное
        win_set_title(w, title);
        // Буфер анимации (верхний слой). Изначально залит WIN_TRANSPARENT:
        // пока программа не нарисует — окно "прозрачно", виден text_buf/фон.
        int bk = buf_alloc();
        if (bk < 0) { w->used = 0; return -1; }   // нет свободных буферов
        w->buf_idx = bk;
        w->buf = buf_ptr(bk);
        // Заливаем ВЕСЬ буфер PWIN_MAXW*PWIN_MAXH (не только клиентскую часть
        // cw*ch): при maximize окно растягивается до экрана, и область за
        // пределами cw*ch должна быть WIN_TRANSPARENT, а не мусор.
        for (int k = 0; k < PWIN_MAXW * PWIN_MAXH; k++) {
            w->buf[k*3+0] = 0xFF; w->buf[k*3+1] = 0x00; w->buf[k*3+2] = 0xFF; // WIN_TRANSPARENT (B,G,R)
        }
        // Буфер статичного текста (нижний слой). Зануляем — изначально пуст.
        int tk = buf_alloc();
        if (tk < 0) { buf_free(bk); w->used = 0; return -1; }
        w->text_buf_idx = tk;
        w->text_buf = buf_ptr(tk);
        for (int k = 0; k < PWIN_MAXW * PWIN_MAXH * 3; k++) w->text_buf[k] = 0x00;
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
    // ВАЖНО: stride всегда PWIN_MAXW, а не w->w. При maximize w->w меняется
    // на 800, но буфер был нарисован со stride 256 (или другим). Если читать
    // его с текущим w->w — содержимое «смазывается». PWIN_MAXW — фиксирован.
    unsigned char* p = w->buf + (y * PWIN_MAXW + x) * 3;
    p[0] = color & 0xFF; p[1] = (color >> 8) & 0xFF; p[2] = (color >> 16) & 0xFF;
    w->dirty = 1;   // через эту функцию идут fill/rect/text/clear
}

static void api_window_fill(int id, int x, int y, int ww, int hh, unsigned int color) {
    for (int yy = y; yy < y + hh; yy++)
        for (int xx = x; xx < x + ww; xx++)
            api_window_pixel(id, xx, yy, color);
}

// Быстрый blit кадра: копирует src (32bpp ARGB) в буфер окна (24bpp BGR)
// с масштабом под размер клиентской области. Для игр (Doom 640x400).
// src — указатель в памяти вызывающей ELF-программы (ring3).
static void api_window_frame(int id, const unsigned int* src, int sw, int sh) {
    unsigned int _ft0 = timer_ticks();
    int idx = win_by_id(id);
    if (idx < 0 || !src || sw <= 0 || sh <= 0) return;
    window_t* w = &wins[idx];
    int cw = w->w, chh = w->h - 20;   // клиентская область (без заголовка)
    if (cw <= 0 || chh <= 0) return;
    if (sw == cw && sh == chh) {
        // Быстрый путь 1:1: без масштабирования, только 32->24.
        for (int y = 0; y < chh; y++) {
            unsigned char* drow = w->buf + ((unsigned) y * PWIN_MAXW) * 3;
            const unsigned int* srow = src + (unsigned) y * sw;
            for (int x = 0; x < cw; x++) {
                unsigned int c = srow[x];
                drow[x*3+0] = c & 0xFF;
                drow[x*3+1] = (c >> 8) & 0xFF;
                drow[x*3+2] = (c >> 16) & 0xFF;
            }
        }
        w->dirty = 1; w->alpha = 255;
        mark_dirty(w->x, w->y, w->w, w->h);
        g_prof_frame_ticks += (timer_ticks() - _ft0);
        g_prof_frame_calls++;
        return;
    }
    // Масштабирование БЕЗ деления на каждый пиксель: инкрементальный
    // fixed-point (16.16). Иначе на полном экране (~464k пикселей) это сотни
    // мс и звук уходит в underrun.
    unsigned int xs = (unsigned int)(((unsigned long)sw << 16) / (unsigned)cw);
    unsigned int ys = (unsigned int)(((unsigned long)sh << 16) / (unsigned)chh);
    unsigned int yacc = 0;
    for (int y = 0; y < chh; y++) {
        unsigned int sy = yacc >> 16; if (sy >= (unsigned)sh) sy = sh - 1;
        unsigned char* drow = w->buf + ((unsigned) y * PWIN_MAXW) * 3;
        const unsigned int* srow = src + sy * sw;
        unsigned int xacc = 0;
        for (int x = 0; x < cw; x++) {
            unsigned int c = srow[xacc >> 16];
            drow[x*3+0] = c & 0xFF;          // B
            drow[x*3+1] = (c >> 8) & 0xFF;   // G
            drow[x*3+2] = (c >> 16) & 0xFF;  // R
            xacc += xs;
        }
        yacc += ys;
    }
    w->dirty = 1;
    w->alpha = 255;   // непрозрачно (кадр заливает всё)
    mark_dirty(w->x, w->y, w->w, w->h);
    g_prof_frame_ticks += (timer_ticks() - _ft0);
    g_prof_frame_calls++;
}

static void api_window_rect(int id, int x, int y, int ww, int hh, unsigned int color) {
    for (int xx = x; xx < x + ww; xx++) { api_window_pixel(id, xx, y, color); api_window_pixel(id, xx, y + hh - 1, color); }
    for (int yy = y; yy < y + hh; yy++) { api_window_pixel(id, x, yy, color); api_window_pixel(id, x + ww - 1, yy, color); }
}

static void api_window_text(int id, int x, int y, const char* s, unsigned int fg, unsigned int bg) {
    // Если bg == WIN_TRANSPARENT — рисуем ТОЛЬКО пиксели букв,
    // фон под ними НЕ трогаем. Так текст можно накладывать на любой
    // уже нарисованный фон/градиент без чёрной плашки под буквами.
    int transparent = (bg == WIN_TRANSPARENT);
    int px = x;
    while (*s) {
        const unsigned char* g = font_glyph8x8(*s++);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                int on = (bits & (0x80 >> col)) ? 1 : 0;
                if (!on && transparent) continue;   // фон прозрачный — пропускаем
                api_window_pixel(id, px + col, y + row, on ? fg : bg);
            }
        }
        px += 8;
    }
}

// Текст с масштабом глифа scale x scale (1,2,3). Для заголовков <h1>-<h3>.
static void api_window_text_scaled(int id, int x, int y, const char* s,
                                   unsigned int fg, int scale) {
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    int px = x;
    while (*s) {
        const unsigned char* g = font_glyph8x8(*s++);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) continue;
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                        api_window_pixel(id, px + col*scale + dx, y + row*scale + dy, fg);
            }
        }
        px += 8 * scale;
    }
}

static void api_window_clear(int id, unsigned int color) {
    int idx = win_by_id(id);
    if (idx < 0) return;
    window_t* w = &wins[idx];
    api_window_fill(id, 0, 0, w->w, w->h - 20, color);
    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
}

// Пиксель в text_buf (нижний слой), с обрезкой по клиентской области.
static void textbuf_pixel(window_t* w, int x, int y, unsigned int color) {
    int ch = w->h - 20;
    if (x < 0 || y < 0 || x >= w->w || y >= ch) return;
    // stride всегда PWIN_MAXW (см. комментарий в api_window_pixel).
    unsigned char* p = w->text_buf + (y * PWIN_MAXW + x) * 3;
    p[0] = color & 0xFF; p[1] = (color >> 8) & 0xFF; p[2] = (color >> 16) & 0xFF;
}

// API: статичный текст в НИЖНИЙ слой. Анимация в buf накрывает его там, где
// нарисована; где buf залит WIN_TRANSPARENT — текст виден. Рисуются только
// непрозрачные пиксели букв (фон не трогается), вызовы накапливаются.
static void api_window_static_text(int id, int x, int y, const char* s, unsigned int fg) {
    int idx = win_by_id(id);
    if (idx < 0) return;
    window_t* w = &wins[idx];
    if (!w->text_buf) return;
    int px = x;
    while (*s) {
        const unsigned char* g = font_glyph8x8(*s++);
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++)
                if (bits & (0x80 >> col)) textbuf_pixel(w, px + col, y + row, fg);
        }
        px += 8;
    }
    w->dirty = 1;
    mark_dirty(w->x - WIN_SHADOW_PAD, w->y - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD);
}

// API: очистить нижний (текстовый) слой окна.
static void api_window_static_clear(int id) {
    int idx = win_by_id(id);
    if (idx < 0) return;
    window_t* w = &wins[idx];
    if (!w->text_buf) return;
    // Очищаем ВЕСЬ буфер (PWIN_MAXW*PWIN_MAXH), а не w->w*ch: stride
    // фиксирован, и при maximize клиентская часть больше w->w из начала.
    for (int k = 0; k < PWIN_MAXW * PWIN_MAXH * 3; k++) w->text_buf[k] = 0x00;
    w->dirty = 1;
    mark_dirty(w->x - WIN_SHADOW_PAD, w->y - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD);
}

static void api_window_close(int id) {
    int idx = win_by_id(id);
    if (idx < 0) return;
    buf_free(wins[idx].buf_idx);
    buf_free(wins[idx].text_buf_idx);
    wins[idx].used = 0; wins[idx].buf = 0; wins[idx].buf_idx = -1;
    wins[idx].text_buf = 0; wins[idx].text_buf_idx = -1;
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
    // ВАЖНО: при вызове через int 0x80 (interrupt gate) IF=0. Без sti
    // PIT не сработает и hlt уснёт навсегда. sti на время hlt, cli — обратно.
    while ((timer_ticks() - start) < need) {
        __asm__ volatile ("sti; hlt; cli");
    }
}

// ============ ФС-API для ELF-программ (v2) ============
extern int fat_read_dir(int base_dir, const char* path, char* names, int* is_dir,
                        unsigned int* sizes, int max);
extern int fat_read_file_buf(const char* path, unsigned char* buf, unsigned int max);
extern int fat_find(int base_dir, const char* path);

// ВАЖНО: ata_select(0) здесь НЕ вызываем. Путь может нести префикс
// "D0://"/"D1://" — диск выбирает сам vfs_* через vfs_switch(d).
// Преждевременный ata_select(0) затирал выбор диска и D1: игнорировался.
static int api_fs_list(const char* path, char* names, int* is_dir,
                       unsigned int* sizes, int max) {
    return vfs_read_dir(path, names, is_dir, sizes, max);
}

static int api_fs_read(const char* path, unsigned char* buf, unsigned int max) {
    return vfs_read_file(path, buf, max);
}

static int api_fs_size(const char* path) {
    return vfs_size(path);
}

static int api_fs_exists(const char* path) {
    return vfs_find(path) ? 1 : 0;
}

// API: записать текстовый файл (перезапись). 1/0.
static int api_fs_write(const char* path, const char* content) {
    return vfs_write(path, content);
}

// API: форматировать диск (drive 0/1). fs: 0=FAT, 1=MSFS. 1/0.
// ВНИМАНИЕ: уничтожает данные на диске. После — переопределяем VFS.
static int api_format(int drive, int fs, int version) {
    if (drive < 0 || drive >= ATA_MAX_DRIVES) return 0;
    ata_select(drive);
    fat_use_drive(drive);
    msfs_use_drive(drive);
    int ok = 0;
    if (fs == 1) { msfs_format(); ok = msfs_probe(); }
    else { ok = fat_format(version); }
    if (ok) {
        vfs_init();          // переопределить ФС на дисках
        disks_layout();      // обновить иконки дисков
        desktop_scan();      // обновить иконки файлов
        mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
    }
    return ok;
}

static unsigned int api_screen_w(void) { return vbe_width(); }
static unsigned int api_screen_h(void) { return vbe_height(); }

// API: установить обои из BMP-файла (масштаб до экрана). 1/0.
// HTTP GET: разобрать URL, резолвить хост, подключиться, запросить,
// СКОПИРОВАТЬ тело ответа в буфер программы (ring3). Возвращает длину или -1.
// Поддержка: http://host[:port]/path (только HTTP, без TLS).
static char  g_http_buf[32768];

// ============================================================
// АСИНХРОННЫЙ HTTP. Машина состояний, чтобы долгий приём тела
// НЕ блокировал всю ОС. Программа: http_start -> (цикл: http_poll) -> http_result.
// ============================================================
static char  g_http_buf[32768];
static int   g_http_len = 0;        // сколько тела принято
static int   g_http_state = 0;      // 0=idle 1=recv 2=done 3=error
static int   g_http_total = 0;      // всего принято (с заголовками)
static int   g_http_hdr_end = -1;   // позиция конца заголовков (или -1)
static tcp_conn_t g_http_conn;
static void* g_http_tls = 0;   // TLS-сессия для HTTPS (0 = обычный HTTP)
static int   g_http_binary = 0;     // 1 = ответ не text/* (картинка/файл) — не парсить как HTML
static int   g_http_toobig = 0;     // 1 = Content-Length больше буфера -> обрезано

// Начать HTTP-запрос. Делает DNS + connect + отправку GET (короткие фазы).
// Возвращает 0 при успехе, -1 при ошибке.
// Разбор IPv4-литерала a.b.c.d -> ip[4]. 0 = успех, -1 = не IP-адрес.
static int parse_ipv4(const char* s, unsigned char* ip) {
    int part = 0, val = 0, digits = 0;
    for (const char* p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return -1;
            if (++digits > 3) return -1;
        } else if (*p == '.' || *p == 0) {
            if (digits == 0) return -1;
            if (part > 3) return -1;
            ip[part++] = (unsigned char)val;
            val = 0; digits = 0;
            if (*p == 0) break;
        } else {
            return -1;
        }
    }
    return (part == 4) ? 0 : -1;
}

static int api_http_start(const char* url) {
    g_http_state = 0; g_http_len = 0; g_http_total = 0; g_http_hdr_end = -1;
    if (!url) { g_http_state = 3; return -1; }
    const char* p = url;
    int is_https = 0;
    if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]=='s'&&p[5]==':'&&p[6]=='/'&&p[7]=='/') { is_https = 1; p += 8; }
    else if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]==':'&&p[5]=='/'&&p[6]=='/') p += 7;
    else { g_http_state = 3; return -1; }
    char host[128]; int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < 127) host[hi++] = *p++;
    host[hi] = 0;
    if (hi == 0) { g_http_state = 3; return -1; }
    unsigned short port = is_https ? 443 : 80;
    if (*p == ':') { p++; int pt = 0; while (*p >= '0' && *p <= '9') pt = pt*10 + (*p++ - '0'); if (pt) port = (unsigned short)pt; }
    const char* path = (*p == '/') ? p : "/";
    unsigned char ip[4];
    // Если host — это IP-литерал (напр. 10.0.2.2), НЕ идём в DNS, а парсим.
    // Иначе браузер резолвил бы даже числовой адрес как имя хоста.
    if (parse_ipv4(host, ip) != 0) {
        if (net_dns_resolve(host, ip) != 0) { g_http_state = 3; return -1; }
    }
    char req[512]; int r = 0;
    const char* s1 = "GET "; for (int i=0;s1[i] && r<500;i++) req[r++]=s1[i];
    for (int i=0;path[i] && r<500;i++) req[r++]=path[i];
    const char* s2 = " HTTP/1.0\r\nHost: "; for (int i=0;s2[i] && r<500;i++) req[r++]=s2[i];
    for (int i=0;host[i] && r<500;i++) req[r++]=host[i];
    const char* s3 = "\r\nConnection: close\r\nUser-Agent: MysticOS/2.3.6.4\r\n\r\n"; for (int i=0;s3[i] && r<510;i++) req[r++]=s3[i];
    if (is_https) {
        // HTTPS: TCP + TLS-handshake, затем шлём запрос уже зашифрованным.
        extern void* tls_connect(const unsigned char*, unsigned short, const char*);
        extern int tls_send(void*, const void*, unsigned int);
        g_http_tls = tls_connect(ip, port, host);
        if (!g_http_tls) { g_http_state = 3; return -1; }
        if (tls_send(g_http_tls, req, (unsigned int)r) < 0) { g_http_state = 3; return -1; }
    } else {
        g_http_tls = 0;
        if (net_tcp_connect(&g_http_conn, ip, port) != 0) { g_http_state = 3; return -1; }
        if (net_tcp_send(&g_http_conn, req, (unsigned int)r) < 0) { g_http_state = 3; return -1; }
    }
    g_http_state = 1;
    return 0;
}

// Продвинуть запрос (НЕблокирующе): один net_poll + забрать всё, что есть.
// Возвращает 0=в процессе, 1=готово, -1=ошибка.
static int api_http_poll(void) {
    if (g_http_state == 2) return 1;
    if (g_http_state == 3) return -1;
    if (g_http_state != 1) return 0;
    net_poll();   // один шаг приёма
    const int CAP = (int)sizeof(g_http_buf) - 1;
    int tls_closed = 0;
    if (g_http_tls) {
        extern int tls_recv_nb(void*, void*, unsigned int);
        // НЕБЛОКИРУЮЩИЙ приём: не вешаем ОС, пока ждём данные.
        int room = CAP - g_http_total;
        if (room > 0) {
            int n = tls_recv_nb(g_http_tls, g_http_buf + g_http_total, (unsigned int)room);
            if (n > 0) g_http_total += n;
            else if (n < 0) tls_closed = 1;
        }
    } else {
    for (;;) {
        if (g_http_total >= CAP) break;
        int room = CAP - g_http_total;
        int n = net_tcp_recv_nb(&g_http_conn, g_http_buf + g_http_total, (unsigned int)room);
        if (n <= 0) break;
        g_http_total += n;
    }
    }
    // Проверяем, закончился ли ответ (заголовки + тело по Content-Length
    // мы не парсим — считаем готовым, когда соединение закрылось FIN,
    // т.е. state стал CLOSED/TIME_WAIT).
    int done = g_http_tls ? tls_closed :
                             (g_http_conn.state == TCP_CLOSED || g_http_conn.state == TCP_TIME_WAIT);
    // Для TLS доп. эвристика: если в буфере уже есть полный ответ (есть \r\n\r\n
    // и Content-Length покрыт) — не ждём close, т.к. TLS-сервер держит соединение.
    if (g_http_tls && g_http_total > 0) {
        int he = -1;
        for (int i = 0; i + 3 < g_http_total; i++)
            if (g_http_buf[i]=='\r'&&g_http_buf[i+1]=='\n'&&g_http_buf[i+2]=='\r'&&g_http_buf[i+3]=='\n'){ he=i+4; break; }
        if (he > 0) {
            // ищем Content-Length
            int cl = -1;
            for (int i = 0; i + 15 < he; i++) {
                if ((g_http_buf[i]=='C'||g_http_buf[i]=='c') && g_http_buf[i+1]=='o'&&g_http_buf[i+2]=='n'&&g_http_buf[i+3]=='t'&&g_http_buf[i+4]=='e'&&g_http_buf[i+5]=='n'&&g_http_buf[i+6]=='t'&&g_http_buf[i+7]=='-'&&g_http_buf[i+8]=='L'&&g_http_buf[i+9]=='e'&&g_http_buf[i+10]=='n') {
                    int j=i+11; while(j<he&&(g_http_buf[j]==':'||g_http_buf[j]==' '))j++;
                    cl=0; while(j<he&&g_http_buf[j]>='0'&&g_http_buf[j]<='9'){cl=cl*10+(g_http_buf[j]-'0');j++;}
                    break;
                }
            }
            if (cl >= 0 && g_http_total - he >= cl) done = 1;
            if (tls_closed) done = 1;
        }
    }
    if (done) {
        // соединение закрыто — ответ получен полностью
        if (g_http_total <= 0) { g_http_state = 3; return -1; }
        g_http_buf[g_http_total] = 0;
        g_http_hdr_end = -1;
        for (int i = 0; i + 3 < g_http_total; i++) {
            if (g_http_buf[i]=='\r'&&g_http_buf[i+1]=='\n'&&g_http_buf[i+2]=='\r'&&g_http_buf[i+3]=='\n') { g_http_hdr_end = i + 4; break; }
        }
        g_http_len = g_http_total - (g_http_hdr_end >= 0 ? g_http_hdr_end : 0);
        // Разбор заголовков: бинарь ли (Content-Type не text/*), и не обрезано ли.
        g_http_binary = 0; g_http_toobig = 0;
        int he = g_http_hdr_end;
        if (he > 0) {
            int ct_binary = 0, seen_ct = 0;
            long clen = -1;
            for (int i = 0; i + 13 < he; i++) {
                // Content-Type:
                if ((g_http_buf[i]=='C'||g_http_buf[i]=='c') &&
                    g_http_buf[i+1]=='o'&&g_http_buf[i+2]=='n'&&g_http_buf[i+3]=='t'&&
                    g_http_buf[i+4]=='e'&&g_http_buf[i+5]=='n'&&g_http_buf[i+6]=='t'&&
                    g_http_buf[i+7]=='-'&&g_http_buf[i+8]=='T'&&g_http_buf[i+9]=='y'&&
                    g_http_buf[i+10]=='p'&&g_http_buf[i+11]=='e') {
                    int j = i + 12; while (j < he && (g_http_buf[j]==':'||g_http_buf[j]==' ')) j++;
                    seen_ct = 1;
                    // text/* или пусто -> текст; иначе бинарь.
                    if (!(g_http_buf[j]=='t'&&g_http_buf[j+1]=='e'&&g_http_buf[j+2]=='x'&&g_http_buf[j+3]=='t')) ct_binary = 1;
                }
                // Content-Length:
                if ((g_http_buf[i]=='C'||g_http_buf[i]=='c') &&
                    g_http_buf[i+1]=='o'&&g_http_buf[i+2]=='n'&&g_http_buf[i+3]=='t'&&
                    g_http_buf[i+4]=='e'&&g_http_buf[i+5]=='n'&&g_http_buf[i+6]=='t'&&
                    g_http_buf[i+7]=='-'&&g_http_buf[i+8]=='L'&&g_http_buf[i+9]=='e'&&
                    g_http_buf[i+10]=='n') {
                    int j = i + 11; while (j < he && (g_http_buf[j]==':'||g_http_buf[j]==' ')) j++;
                    clen = 0; while (j < he && g_http_buf[j]>='0'&&g_http_buf[j]<='9'){ clen=clen*10+(g_http_buf[j]-'0'); j++; }
                }
            }
            if (seen_ct && ct_binary) g_http_binary = 1;
            if (clen >= 0 && clen > (long)(sizeof(g_http_buf) - he)) g_http_toobig = 1;
        }
        g_http_state = 2;
        return 1;
    }
    return 0;
}

// Скопировать тело ответа в буфер программы. Возвращает длину или -1.
static int api_http_result(char* out, unsigned int outmax) {
    if (g_http_state != 2 || !out || outmax == 0) return -1;
    // Не-text ответ (картинка/архив/файл): не отдаём бинарь в HTML-парсер.
    if (g_http_binary) {
        const char* m = "[binary file - not displayed]";
        int k = 0; while (m[k] && (unsigned int)(k+1) < outmax) { out[k] = m[k]; k++; }
        out[k] = 0; return k;
    }
    if (g_http_toobig) {
        const char* m = "[file too big - truncated]";
        int k = 0; while (m[k] && (unsigned int)(k+1) < outmax) { out[k] = m[k]; k++; }
        out[k] = 0; return k;
    }
    int blen = g_http_len;
    if ((unsigned int)blen > outmax - 1) blen = (int)(outmax - 1);
    const char* body = g_http_buf + (g_http_hdr_end >= 0 ? g_http_hdr_end : 0);
    for (int i = 0; i < blen; i++) out[i] = body[i];
    out[blen] = 0;
    return blen;
}

// СЫРОЙ результат: тело как есть (без text/binary-фильтра). Для картинок.
static int api_http_result_raw(char* out, unsigned int outmax) {
    if (g_http_state != 2 || !out || outmax == 0) return -1;
    if (g_http_toobig) return -2;   // не влезло в буфер ядра
    int blen = g_http_len;
    if ((unsigned int)blen > outmax) blen = (int)outmax;
    const char* body = g_http_buf + (g_http_hdr_end >= 0 ? g_http_hdr_end : 0);
    for (int i = 0; i < blen; i++) out[i] = body[i];
    return blen;   // без NUL — это бинарь
}

// Старый блокирующий вариант (оставлен для совместимости; не используется).
static int api_http_get(const char* url, char* out, unsigned int outmax) {
    if (api_http_start(url) != 0) return -1;
    for (int i = 0; i < 4000000; i++) {
        int st = api_http_poll();
        if (st == 1) return api_http_result(out, outmax);
        if (st < 0) return -1;
    }
    return -1;
}

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
    // stride фиксирован PWIN_MAXW (см. комментарий в api_window_pixel).
    unsigned char* dst = win->buf + ((unsigned int)y * (unsigned int)PWIN_MAXW + (unsigned int)x) * 3u;
    int rc = bmp_decode_scaled(src, (unsigned int)sz, dst, w, h);
    (void)cw; (void)ch;
    if (rc == BMP_OK) win->dirty = 1;
    return (rc == BMP_OK) ? 1 : 0;
}

// Картинка из ПАМЯТИ (BMP/GIF/PNG) — для веб-страниц. Формат по сигнатуре.
static int api_window_image_mem(int id, int x, int y, int w, int h, const void* ptr, unsigned int size) {
    int idx = win_by_id(id);
    if (idx < 0 || !ptr || size < 8) return 0;
    window_t* win = &wins[idx];
    if (!win->buf) return 0;
    const unsigned char* p = (const unsigned char*)ptr;
    // w<=0/h<=0 -> натуральный размер картинки (из заголовка). Считаем ДО клипа.
    if (w <= 0 || h <= 0) {
        if (p[0]=='G'&&p[1]=='I'&&p[2]=='F'&&p[3]=='8') {
            w = p[6] | (p[7] << 8); h = p[8] | (p[9] << 8);
        } else if (p[0]=='B'&&p[1]=='M' && size >= 26) {
            w = p[18] | (p[19]<<8) | (p[20]<<16) | (p[21]<<24);
            h = p[22] | (p[23]<<8) | (p[24]<<16) | (p[25]<<24);
            if (h < 0) h = -h;
        } else return 0;
        if (w <= 0 || h <= 0) return 0;
    }
    int cw = win->w, ch = win->h - 20;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > cw) w = cw - x;
    if (y + h > ch) h = ch - y;
    if (w <= 0 || h <= 0) return 0;
    serial_print("[img] mem sig=");
    if (size>=4) { serial_print_hex(p[0]); serial_print(" "); serial_print_hex(p[1]); serial_print(" "); serial_print_hex(p[2]); serial_print(" "); serial_print_hex(p[3]); }
    serial_print(" sz="); serial_print_int((int)size); serial_print(" wh="); serial_print_int(w); serial_print("x"); serial_print_int(h); serial_print("\n");
    unsigned char* dst = win->buf + ((unsigned int)y * PWIN_MAXW + (unsigned int)x) * 3u;
    // Буфер-приёмник должен иметь stride PWIN_MAXW — декодеры пишут плотно
    // w*3, поэтому рендерим во временный, затем копируем построчно.
    static unsigned char tmp[800 * 3 * 600];
    if (w * h * 3 > (int)sizeof(tmp)) return 0;
    int ok = 0;
    if (p[0]=='B' && p[1]=='M') {
        ok = (bmp_decode_scaled(p, size, tmp, w, h) == BMP_OK);
    } else if (p[0]=='G'&&p[1]=='I'&&p[2]=='F'&&p[3]=='8') {
        gif_info_t gi;
        if (gif_open(p, size, &gi) == GIF_OK && gi.nframes > 0) {
            ok = (gif_decode_frame(&gi, 0, tmp, w, h) >= 0);
        }
    } else if (p[0]==0x89&&p[1]=='P'&&p[2]=='N'&&p[3]=='G') {
        // PNG — пока не поддержан (декодер добавим отдельным шагом).
        ok = 0;
    }
    if (!ok) return 0;
    for (int r = 0; r < h; r++) {
        unsigned char* drow = dst + (unsigned int)r * PWIN_MAXW * 3u;
        const unsigned char* srow = tmp + (unsigned int)r * w * 3u;
        for (int c = 0; c < w * 3; c++) drow[c] = srow[c];
    }
    win->dirty = 1;
    return 1;
}

static int g_last_cmx = -100, g_last_cmy = -100;

// Последняя НАРИСОВАННАЯ позиция курсора. Обновляется в draw_cursor_bb ЛЮБЫМ
// кодом (главный цикл, yield программы, process_events). Область перерисовки
// обязана её покрывать — иначе старый курсор остаётся как "мусор" (особенно
// когда курсор рисовал не главный цикл и old_mx/old_my успели устареть).
static int g_cur_drawn_x = -1000, g_cur_drawn_y = -1000;
static unsigned int g_yield_frames = 0;
static unsigned int g_yield_fps = 60;
static unsigned int g_fps_tick = 0;
static unsigned int g_last_draw_tick = 0;
static unsigned int g_min_draw_ticks = 1;

// Отслеживание hover кнопок окна (Волна 2): при изменении наведения
// помечаем регион кнопок dirty, иначе подсветка не перерисовывается/
// не стирается при уходе мыши.
static int g_btn_hover[MAXWIN][3] = {{0}};
static void track_button_hover(void) {
    int mx = mouse_x(), my = mouse_y();
    for (int i = 0; i < MAXWIN; i++) {
        if (!wins[i].used || !wins[i].visible) {
            for (int b = 0; b < 3; b++) g_btn_hover[i][b] = 0;
            continue;
        }
        window_t* w = &wins[i];
        int by = w->y + 4;
        int cbx[3] = { w->x + w->w - 18, w->x + w->w - 34, w->x + w->w - 50 };
        for (int b = 0; b < 3; b++) {
            int hov = (mx >= cbx[b] - 2 && mx < cbx[b] + 14 &&
                       my >= by - 2 && my < by + 14) ? 1 : 0;
            if (hov != g_btn_hover[i][b]) {
                g_btn_hover[i][b] = hov;
                // пометить регион кнопки (с запасом под подсветку)
                mark_dirty(cbx[b] - 3, by - 3, 18, 18);
            }
        }
    }
}

// Отслеживание hover пунктов Start-меню (определена ниже, после sm_x/sm_y).
static void track_start_menu_hover(void);

// Расширить прямоугольник региона (rx,ry..rx2,ry2) так, чтобы он включал
// курсор во ВСЕХ известных позициях: текущей (mx,my), старой главного
// цикла (omx,omy) и последней НАРИСОВАННОЙ (g_cur_drawn). Курсор 12x19,
// запас CUR_PAD. Это единый источник правды — иначе два независимых цикла
// (main и api_yield) не знают позиций друг друга -> остаются следы.
static void include_cursor(int* prx, int* pry, int* prx2, int* pry2,
                           int mx, int my, int omx, int omy) {
    int xs[3], ys[3];
    xs[0] = mx;    ys[0] = my;
    xs[1] = omx;   ys[1] = omy;
    xs[2] = g_cur_drawn_x; ys[2] = g_cur_drawn_y;
    for (int i = 0; i < 3; i++) {
        if (xs[i] < -500) continue;   // «нет курсора» (скрыт в захвате)
        int x0 = xs[i] - CUR_PAD, y0 = ys[i] - CUR_PAD;
        int x1 = xs[i] + 12 + CUR_PAD, y1 = ys[i] + 19 + CUR_PAD;
        if (x0 < *prx) *prx = x0;
        if (y0 < *pry) *pry = y0;
        if (x1 > *prx2) *prx2 = x1;
        if (y1 > *pry2) *pry2 = y1;
    }
}

// Реализация вывода кадра. Обёрнута api_yield, которая в конце уступает CPU.
static void api_yield_impl(void) {
    track_button_hover();
    track_start_menu_hover();
    int changed = process_events(0);
    // Закрытие окна крестиком помечает на выход ИМЕННО задачу-владельца
    // (task_request_exit). Проверяем — не наша ли задача помечена.
    // Раньше был g_exit_request, который убивал ту задачу, что сейчас
    // в yield — то есть СЛУЧАЙНУЮ (не ту, чьё окно закрыли).
    {
        int code = 0;
        if (task_current_wants_exit(&code)) task_exit(code);
    }

    unsigned int now = timer_ticks();
    g_yield_frames++;
    if (now - g_fps_tick >= 100) {
        g_yield_fps = g_yield_frames;
        g_yield_frames = 0;
        g_fps_tick = now;
        if (g_yield_fps < 6) {
            g_min_draw_ticks += 1;
            if (g_min_draw_ticks > 30) g_min_draw_ticks = 30;   // было 50 — слишком жёстко
        } else if (g_yield_fps > 20) {
            if (g_min_draw_ticks > 1) g_min_draw_ticks--;
        } else {
            // Умеренный FPS (6..20): РАНЬШЕ отката не было вообще ->
            // троттлинг «залипал» навсегда после разового провала (баг).
            // Теперь плавно восстанавливаемся (быстрее, чем +1 за провал).
            if (g_min_draw_ticks > 1) g_min_draw_ticks -= 2;
            if (g_min_draw_ticks < 1) g_min_draw_ticks = 1;
        }
    }

    // Есть ли АКТИВНО анимирующееся окно программы (игра рисует кадры)?
    // Если да — троттлинг НЕ применяем: игра сама управляет частотой кадров.
    // Без этой проверки разовый провал FPS навсегда ограничивал отрисовку
    // (ratchet), и видео «лагало» даже при свободном CPU.
    int any_dirty = 0;
    for (int i = 0; i < MAXWIN; i++) {
        if (wins[i].used && wins[i].is_program && wins[i].visible && wins[i].dirty) {
            any_dirty = 1; break;
        }
    }

    // Троттлинг НЕ применяем, если что-то изменилось (клик, drag, свёрнуто/
    // развёрнуто окно) или активно рисуется программа-анимация (игра).
    if (drag_win < 0 && !changed && !any_dirty &&
        (now - g_last_draw_tick < g_min_draw_ticks)) return;
    g_last_draw_tick = now;

    int cmx = mouse_x(), cmy = mouse_y();
    // Что-то СЕРЬЁЗНОЕ (клик, open/close, смена z-порядка) — полная
    // перерисовка сцены. Только DRAG окна — узкая dirty-область
    // (bbox старой+новой позиции, уже накопленный в process_events).
    // Иначе каждый mouse-move тянул бы весь экран — QEMU не успевает.
    if (changed || (drag_win >= 0 && dirty_w == 0)) {
        render_region(0, 0, (int)vbe_width(), (int)vbe_height());
        draw_cursor_bb(cmx, cmy);
        fb_flip();
        g_last_cmx = cmx; g_last_cmy = cmy;
        dirty_w = 0; dirty_h = 0;
        for (int i = 0; i < MAXWIN; i++) wins[i].dirty = 0;
        return;
    }
    // Область перерисовки: СТАРЫЙ и НОВЫЙ курсор + dirty + ВСЕ видимые окна
    // программ (их содержимое — анимация — меняется каждый кадр). Рисуем
    // ВСЕГДА, даже если окон нет: иначе курсор замирает.
    // Единый расчёт региона курсора (Волна 2, v2): включаем текущую,
    // старую и последнюю нарисованную позиции с запасом CUR_PAD.
    int rx = cmx, ry = cmy, rx2 = cmx, ry2 = cmy;
    include_cursor(&rx, &ry, &rx2, &ry2, cmx, cmy, g_last_cmx, g_last_cmy);
    if (dirty_w > 0 && dirty_h > 0) {
        if (dirty_x < rx) rx = dirty_x;
        if (dirty_y < ry) ry = dirty_y;
        if (dirty_x + dirty_w > rx2) rx2 = dirty_x + dirty_w;
        if (dirty_y + dirty_h > ry2) ry2 = dirty_y + dirty_h;
    }
    // Включаем видимые окна программ — но ТОЛЬКО те, что помечены dirty
    // (программа что-то нарисовала с прошлого кадра). Это критично при
    // maximize: если окно на весь экран и НЕ менялось, не надо гонять
    // 1.44 МБ через backbuffer каждый кадр — иначе лаги.
    for (int i = 0; i < MAXWIN; i++) {
        if (!wins[i].used || !wins[i].is_program || !wins[i].visible) continue;
        if (!wins[i].dirty) continue;
        int wx = wins[i].x - WIN_SHADOW_PAD, wy = wins[i].y - WIN_SHADOW_PAD;
        int wx2 = wins[i].x + wins[i].w + WIN_SHADOW_PAD, wy2 = wins[i].y + wins[i].h + WIN_SHADOW_PAD;
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
    for (int i = 0; i < MAXWIN; i++) wins[i].dirty = 0;
}

// Программный yield: показать кадр и уступить CPU другим задачам.
static void api_yield(void) {
    api_yield_impl();
    task_yield();   // кооперативное переключение (если включено)
}

// ============================================================
// msdll: экспорты "mskernel.dll" (виртуальная DLL — не файл).
// Программы зовут msdll_resolve("mskernel", "...") и получают
// указатель на соответствующую функцию ядра. Заполняется до g_api,
// потому что все api_* уже объявлены выше (строки < 878).
// ============================================================
static const msdll_builtin_export_t g_mskernel_exports[] = {
    // Вывод
    { "put_char",           (unsigned int)&put_char           },
    { "print_string",       (unsigned int)&print_string       },
    { "print_int",          (unsigned int)&print_int          },
    { "print_hex",          (unsigned int)&print_hex          },
    // Окна
    { "window_create",      (unsigned int)&api_window_create      },
    { "window_create_ex",   (unsigned int)&api_window_create_ex   },
    { "window_pixel",       (unsigned int)&api_window_pixel       },
    { "window_rect",        (unsigned int)&api_window_rect        },
    { "window_fill",        (unsigned int)&api_window_fill        },
    { "window_text",        (unsigned int)&api_window_text        },
    { "window_clear",       (unsigned int)&api_window_clear       },
    { "window_close",       (unsigned int)&api_window_close       },
    { "window_flush",       (unsigned int)&api_window_flush       },
    { "window_image",       (unsigned int)&api_window_image       },
    { "window_static_text", (unsigned int)&api_window_static_text },
    { "window_static_clear",(unsigned int)&api_window_static_clear},
    // ФС
    { "fs_list",            (unsigned int)&api_fs_list       },
    { "fs_read",            (unsigned int)&api_fs_read       },
    { "fs_size",            (unsigned int)&api_fs_size       },
    { "fs_exists",          (unsigned int)&api_fs_exists     },
    { "fs_write",           (unsigned int)&api_fs_write      },
    { "format",             (unsigned int)&api_format       },
    // Система
    { "yield",              (unsigned int)&api_yield        },
    { "sleep",              (unsigned int)&api_sleep        },
    { "ticks",              (unsigned int)&timer_ticks      },
    { "screen_w",           (unsigned int)&api_screen_w     },
    { "screen_h",           (unsigned int)&api_screen_h     },
    { "set_wallpaper",      (unsigned int)&api_set_wallpaper},
    { "http_get",           (unsigned int)&api_http_get},
    { "get_arg",            (unsigned int)&api_get_arg      },
    { "run_program",        (unsigned int)&run_program      },
    { "run_program_arg",    (unsigned int)&api_run_program_arg },
    { 0, 0 }
};

// ============================================================
// SYSCALL DISPATCHER (int 0x80) — настоящий.
// ============================================================
// Раскладка стека isr128 (entry.asm), после pusha + push ds/es/fs/gs:
//   raw[0]=gs raw[1]=fs raw[2]=es raw[3]=ds
//   raw[4]=edi raw[5]=esi raw[6]=ebp raw[7]=esp
//   raw[8]=ebx raw[9]=edx raw[10]=ecx raw[11]=eax
//   raw[12]=eip raw[13]=cs raw[14]=eflags raw[15]=useresp raw[16]=ss
// Аргументы — ebx/ecx/edx/esi/edi. Возврат — raw[11] (eax).
#include "dll/msyscall.h"

// Шесть аргументов: ebx,ecx,edx,esi,edi,ebp.
// ebp (a5) — для 6-го аргумента функций (например, color у fill/rect).
// В cdecl-вызове int 0x80 ebp обычно «занят» как frame pointer, но
// программы в ring3 — freestanding, могут передавать в нём что угодно,
// если собственный код не использует ebp как fp (у нас -O2 с fp-omit).
static int msys_dispatch(unsigned int n,
                         unsigned int a0, unsigned int a1,
                         unsigned int a2, unsigned int a3,
                         unsigned int a4, unsigned int a5, unsigned int a6) {
    switch (n) {
    // ----- Базовые -----
    case SYS_PUTCHAR:       put_char((char)a0); return 0;
    case SYS_PRINT_STRING:  print_string((const char*)a0); return 0;
    case SYS_PRINT_INT:     print_int((int)a0); return 0;
    case SYS_PRINT_HEX:     print_hex((unsigned int)a0); return 0;
    case SYS_TICKS:         return (int)timer_ticks();
    case SYS_SLEEP:         api_sleep(a0); return 0;
    // Клавиатура — только активной (в фокусе) задаче. Иначе две
    // программы-«писателя» делили бы поток клавиш между собой.
    case SYS_KEY_AVAILABLE:
        if (focused_task_id() != task_current()) return 0;
        return kb_available();
    case SYS_KEY_GETCHAR:
        if (focused_task_id() != task_current()) return 0;
        return kb_getchar();
    case SYS_MOUSE_X:       return mouse_x();
    case SYS_MOUSE_Y:       return mouse_y();
    case SYS_MOUSE_LEFT:    return mouse_left();
    case SYS_SCREEN_W:      return (int)vbe_width();
    case SYS_SCREEN_H:      return (int)vbe_height();
    case SYS_YIELD:         api_yield(); return 0;
    // ----- Окна -----
    case SYS_WINDOW_CREATE:      return api_window_create((const char*)a0, (int)a1, (int)a2, (int)a3, (int)a4);
    case SYS_WINDOW_CREATE_EX:   return api_window_create_ex((const char*)a0, (int)a1, (int)a2, (int)a3, (int)a4, 0);
    case SYS_WINDOW_PIXEL:       api_window_pixel((int)a0, (int)a1, (int)a2, a3); return 0;
    case SYS_WINDOW_RECT:        api_window_rect((int)a0, (int)a1, (int)a2, (int)a3, (int)a4, a5); return 0;
    case SYS_WINDOW_FILL:        api_window_fill((int)a0, (int)a1, (int)a2, (int)a3, (int)a4, a5); return 0;
    case SYS_WINDOW_TEXT:        api_window_text((int)a0, (int)a1, (int)a2, (const char*)a3, a4, a5); return 0;
    case SYS_WINDOW_TEXT_SCALED: api_window_text_scaled((int)a0, (int)a1, (int)a2, (const char*)a3, a4, (int)a5); return 0;
    case SYS_WINDOW_CLEAR:       api_window_clear((int)a0, a1); return 0;
    case SYS_WINDOW_CLOSE:       api_window_close((int)a0); return 0;
    case SYS_WINDOW_FLUSH:       api_window_flush(); return 0;
    case SYS_WINDOW_IMAGE:       return api_window_image((int)a0, (int)a1, (int)a2, (int)a3, (int)a4, 0);
    case SYS_WINDOW_STATIC_TEXT: api_window_static_text((int)a0, (int)a1, (int)a2, (const char*)a3, a4); return 0;
    case SYS_WINDOW_STATIC_CLEAR:api_window_static_clear((int)a0); return 0;
    case SYS_WINDOW_FRAME:       api_window_frame((int)a0, (const unsigned int*)a1, (int)a2, (int)a3); return 0;
    case SYS_WINDOW_CLIENT: {
        int idx = win_by_id((int)a0);
        if (idx < 0) return 0;
        window_t* w = &wins[idx];
        int cw = w->w, chh = w->h - 20;   // клиентская область без заголовка
        if (cw < 0) cw = 0; if (chh < 0) chh = 0;
        return (cw << 16) | (chh & 0xFFFF);
    }
    case SYS_WINDOW_POS: {
        // Позиция КЛИЕНТСКОЙ области (x<<16)|y — для пересчёта координат
        // мыши в hover/кликах. РАНЬШЕ обработчика не было -> ms_window_pos
        // возвращал мусор, браузер считал hc/row со сдвигом, ссылки не
        // ловились. Требуем win_x/win_y и вычитаем высоту заголовка (20).
        int idx = win_by_id((int)a0);
        if (idx < 0) return 0;
        window_t* w = &wins[idx];
        int cx = w->x;
        int cy = w->y + 20;   // заголовок 20px
        return (cx << 16) | (cy & 0xFFFF);
    }
    case SYS_WINDOW_IMAGE_MEM:
        return api_window_image_mem((int)a0, (int)a1, (int)a2, (int)a3, (int)a4,
                                    (const void*)a5, (unsigned int)a6);
    case SYS_KEY_EVENT:    return kb_get_event();
    case SYS_SOUND_BEEP:  vss_beep(a0, a1); return 0;
    case SYS_SOUND_PCM:   vss_play_pcm((const unsigned char*)a0, a1, a2); return 0;
    case SYS_MUSIC_PCM:   return music_feed((const short*)a0, a1);
    case SYS_MUSIC_STOP:  music_stop(); return 0;
    case SYS_MOUSE_GRAB:   mouse_set_grab((int)a0); return 0;
    case SYS_MOUSE_EVENT:  return mouse_get_event();
    case SYS_MOUSE_REL: {
        int dx = 0, dy = 0;
        mouse_take_rel(&dx, &dy);
        if (a0) *(int*)a0 = dx;
        if (a1) *(int*)a1 = dy;
        return (dx || dy) ? 1 : 0;
    }
    case SYS_MEM_ALLOC: {
        // Программа просит непрерывный регион (из pmem, блоки 4 МБ).
        unsigned int bytes = a0;
        unsigned int nblocks = (bytes + PMEM_BLOCK - 1) / PMEM_BLOCK;
        unsigned int phys = pmem_alloc_contig(nblocks);
        return (int)phys;   // identity: phys == virt
    }
    case SYS_DEBUG: {
        // Отладка программы: строку в serial (COM1).
        const char* s = (const char*)a0;
        if (s) serial_print(s);
        return 0;
    }
    case SYS_MEM_FREE: {
        unsigned int bytes = a1;
        unsigned int nblocks = (bytes + PMEM_BLOCK - 1) / PMEM_BLOCK;
        pmem_free_contig(a0, nblocks);
        return 1;
    }
    // ----- ФС -----
    case SYS_FS_SIZE:   return api_fs_size((const char*)a0);
    case SYS_FS_EXISTS: return api_fs_exists((const char*)a0);
    case SYS_FS_READ:   return api_fs_read((const char*)a0, (unsigned char*)a1, a2);
    case SYS_FS_WRITE:  return api_fs_write((const char*)a0, (const char*)a1);
    case SYS_FS_WRITE_BIN: return vfs_write_bin((const char*)a0, (const unsigned char*)a1, a2);
    case SYS_FS_LIST:   return api_fs_list((const char*)a0, (char*)a1, (int*)a2, (unsigned int*)a3, (int)a4);
    case SYS_FORMAT:    return api_format((int)a0, (int)a1, (int)a2);
    // ----- Файловый дескриптор (стриминг) -----
    case SYS_FILE_OPEN:   return vfs_file_open((const char*)a0);
    case SYS_FILE_READ:   return vfs_file_read((int)a0, (unsigned char*)a1, a2);
    case SYS_FILE_SEEK:   return vfs_file_seek((int)a0, a1);
    case SYS_FILE_CLOSE:  vfs_file_close((int)a0); return 0;
    case SYS_FILE_SIZE:   return vfs_file_size((int)a0);
    // ----- Система -----
    case SYS_RUN_PROGRAM:   return run_program((const char*)a0);
    case SYS_RUN_PROGRAM_ARG: return api_run_program_arg((const char*)a0, (const char*)a1);
    case SYS_GET_ARG: {
        // Аргумент лежит в ЯДРОВОЙ памяти (g_prog_arg) — ring3 её не видит.
        // Копируем его в стек-регион ВЫЗЫВАЮЩЕЙ задачи по фикс-адресу
        // 0x00800100 (низ стека, не пересекается с растущим вниз стеком)
        // и возвращаем этот user-адрес.
        char* dst = (char*)0x00800100u;
        int i = 0;
        while (g_prog_arg[i] && i < 158) { dst[i] = g_prog_arg[i]; i++; }
        dst[i] = 0;
        return (int)0x00800100u;
    }
    case SYS_SET_WALLPAPER: return api_set_wallpaper((const char*)a0);
    case SYS_HTTP_GET:      return api_http_get((const char*)a0, (char*)a1, a2);
    case SYS_HTTP_START:    return api_http_start((const char*)a0);
    case SYS_HTTP_POLL:     return api_http_poll();
    case SYS_HTTP_RESULT:   return api_http_result((char*)a0, a1);
    case SYS_HTTP_RESULT_RAW: return api_http_result_raw((char*)a0, a1);
    // ----- DLL (v4) -----
    case SYS_DLL_RESOLVE:   return (int)msdll_resolve((const char*)a0, (const char*)a1);
    case SYS_DLL_LOAD:      return (int)msdll_load_file((const char*)a0, (const char*)a1);
    case SYS_DLL_COUNT:     return (int)msdll_dll_count();
    default: return 0;
    }
}

void syscall_handler_dispatch(unsigned int* raw) {
    unsigned int n   = raw[11];   // eax — номер
    unsigned int ptr = raw[8];    // ebx — указатель на массив аргументов

    // ABI: args[i] в памяти вызывающего (ring3). ring0 видит их
    // напрямую благодаря identity-маппингу (virt == phys).
    unsigned int a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0;
    if (ptr) {
        const unsigned int* a = (const unsigned int*)ptr;
        a0 = a[0]; a1 = a[1]; a2 = a[2]; a3 = a[3]; a4 = a[4]; a5 = a[5]; a6 = a[6];
    }

    if (n == SYS_EXIT) {
        task_request_exit(task_current(), (int)a0);
        raw[11] = 0;
        return;
    }

    raw[11] = (unsigned int)msys_dispatch(n, a0, a1, a2, a3, a4, a5, a6);
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
    api_format,
    api_window_static_text,   // в КОНЕЦ — ABI v2 сохранён
    api_window_static_clear,
    api_http_get   // в КОНЕЦ — ABI v3 сохранён
};

static int start_open = 0;
#define SM_W 160
#define SM_ITEM_H 26
#define SM_N 7
static const char* sm_items[SM_N] = { "Terminal", "Files", "About", "Music", "Wallpaper", "Theme", "Reboot" };
// Текущая тема: 0 = dark, 1 = light.
static int g_theme_light = 0;
static int sm_x(void) { return 4; }
static int sm_y(void) { return (int)vbe_height() - TASKBAR_H - SM_N * SM_ITEM_H - 4; }

// Отслеживание hover пунктов Start-меню: при смене наведения помечаем
// регион меню dirty (иначе подсветка не стирается — пункты/углы чернеют).
static int g_sm_hover = -1;
static void track_start_menu_hover(void) {
    int nh = -1;
    if (start_open) {
        int mx = mouse_x(), my = mouse_y();
        int x = sm_x(), y = sm_y();
        if (mx >= x && mx < x + SM_W && my >= y && my < y + SM_N * SM_ITEM_H + 4) {
            int idx = (my - y - 3) / SM_ITEM_H;
            if (idx >= 0 && idx < SM_N) nh = idx;
        }
    }
    if (nh != g_sm_hover) {
        g_sm_hover = nh;
        int x = sm_x(), y = sm_y();
        mark_dirty(x - 4, y - 4, SM_W + 8, SM_N * SM_ITEM_H + 12);
    }
}

static void bg_cache_blit_region(int rx, int ry, int rw, int rh);   // forward

static void draw_start_menu(int rx,int ry,int rw,int rh) {
    if (!start_open) return;
    int x = sm_x(), y = sm_y();
    int w = SM_W, h = SM_N * SM_ITEM_H + 4;
    if (!rects_overlap(rx,ry,rw,rh, x - WIN_SHADOW_PAD, y - WIN_SHADOW_PAD, w + 2*WIN_SHADOW_PAD, h + 2*WIN_SHADOW_PAD)) return;
    // Восстановить ФОН вокруг меню (под скруглёнными углами и тенью),
    // но НЕ выходить на таскбар снизу и не залезать на края экрана.
    int bx = x - WIN_SHADOW_PAD, by = y - WIN_SHADOW_PAD;
    int bw = w + 2*WIN_SHADOW_PAD, bh = h + 2*WIN_SHADOW_PAD;
    int tb_y = (int)vbe_height() - TASKBAR_H;   // верх таскбара
    if (by + bh > tb_y) bh = tb_y - by;          // не залезать на таскбар
    if (by < 0) { bh += by; by = 0; }
    if (bx < 0) { bw += bx; bx = 0; }
    if (bx + bw > (int)vbe_width()) bw = (int)vbe_width() - bx;
    if (bw > 0 && bh > 0) bg_cache_blit_region(bx, by, bw, bh);
    // Тень меню (мягкая, скруглённая).
    draw_shadow(x, y, w, h);
    // Фон меню — СКРУГЛЁННЫЙ. Сначала чуть более светлая кромка (радиус+1),
    // потом основной фон (радиус). Так получаем скруглённую обводку БЕЗ
    // прямоугольной рамки, которая затирала углы.
    int r = g_theme.corner_radius;
    fb_backbuffer_round_rect_alpha(x, y, w, h, r + 1, COL_WIN_BORDER, 255);
    fb_backbuffer_round_rect_alpha(x + 1, y + 1, w - 2, h - 2, r, COL_MENU_BG, 255);
    // Пункты с hover-подсветкой (скруглённой).
    int mx = mouse_x(), my = mouse_y();
    for (int i = 0; i < SM_N; i++) {
        int iy = y + 3 + i * SM_ITEM_H;
        int hi = (mx >= x + 2 && mx < x + w - 2 && my >= iy && my < iy + SM_ITEM_H);
        if (hi) fb_backbuffer_round_rect_alpha(x + 4, iy, w - 8, SM_ITEM_H, 6, COL_MENU_SEL, 255);
        // Текст БЕЗ фона (bb_text_t) — иначе плоский прямоугольник фона
        // виден на скруглённом меню.
        bb_text_t(x + 12, iy + 8, sm_items[i], COL_TEXT);
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
    if (!rects_overlap(rx,ry,rw,rh, x - WIN_SHADOW_PAD, y - WIN_SHADOW_PAD, w + 2*WIN_SHADOW_PAD, h + 2*WIN_SHADOW_PAD)) return;
    draw_shadow(x, y, w, h);
    // Фон меню — СКРУГЛЁННЫЙ (Волна 2).
    int r = g_theme.corner_radius;
    fb_backbuffer_round_rect_alpha(x, y, w, h, r, COL_MENU_BG, 255);
    fb_backbuffer_rect(x, y, w, h, COL_WIN_BORDER);
    int mx = mouse_x(), my = mouse_y();
    for (int i = 0; i < ctx_n; i++) {
        int iy = y + 2 + i * CTX_IH;
        int hi = (mx >= x + 2 && mx < x + w - 2 && my >= iy && my < iy + CTX_IH);
        if (hi) fb_backbuffer_round_rect_alpha(x + 3, iy, w - 6, CTX_IH, 6, COL_MENU_SEL, 255);
        bb_text(x + 10, iy + 7, ctx_items[i], COL_TEXT,
                hi ? COL_MENU_SEL : COL_MENU_BG);
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
        // ВАЖНО: этот блок — ТОЛЬКО если клик ПО МЕНЮ. Если мимо —
        // закрываем меню и НЕ выходим (иначе не обрабатываются иконки).
        int hit_menu = (mx >= x && mx < x + w && my >= y && my < y + h);
        if (hit_menu) {
            int idx = (my - y - 2) / CTX_IH;
            if (idx >= 0 && idx < ctx_n) {
                switch (ctx_action[idx]) {
                    case CTX_REFRESH: desktop_scan(); break;
                    case CTX_TERM:    run_program("/kernel/SYSPR/TERM.ELF"); break;
                    case CTX_WALL:    run_program("/kernel/SYSPR/WPICK.ELF"); break;
                    // ПРИЧИНА "улёта": fat/msfs write зовут print_string, а он при
                    // g_out_to_term=0 идёт в console -> пишет в LFB и СКРОЛЛИТ экран.
                    // На время записи направляем вывод в терминал (буфер, не LFB).
                    case CTX_NEWDIR: {
                        extern int g_out_to_term; g_out_to_term = 1;
                        vfs_mkdir("D1:/NEWDIR");
                        g_out_to_term = 0; desktop_scan(); break; }
                    case CTX_NEWFILE: {
                        extern int g_out_to_term; g_out_to_term = 1;
                        vfs_write("D1:/NEW.TXT", "x");
                        g_out_to_term = 0; desktop_scan(); break; }
                    default: break;
                }
            }
        }
        if (hit_menu) {
            ctx_open = 0;
            // После действий меню — ПОЛНАЯ перерисовка + flip.
            render_region(0, 0, (int)vbe_width(), (int)vbe_height());
            draw_cursor_bb(mx, my);
            fb_flip();
            dirty_w = 0; dirty_h = 0;
            changed = 1;
            prev_left = left;
            return changed;
        }
        // Клик МИМО меню: закрываем его и ПРОДОЛЖАЕМ (обрабатываем иконки).
        ctx_open = 0;
        mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
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
                    if (idx == 0 || idx == 1 || idx == 2 || idx == 3 || idx == 4) {
                        // 0=Terminal 1=Files 2=About 3=Music 4=Wallpaper
                        const char* ep = (idx == 0) ? "/kernel/SYSPR/TERM.ELF"
                                       : (idx == 1) ? "/kernel/SYSPR/FILES.ELF"
                                       : (idx == 2) ? "/kernel/SYSPR/ABOUT.ELF"
                                       : (idx == 3) ? "/kernel/SYSPR/MUSIC.ELF"
                                                    : "/kernel/SYSPR/WPICK.ELF";
                        run_program(ep);   // запуск ELF вместо встроенного окна
                    } else if (idx == 5) {
                        // Theme: переключить Dark <-> Light.
                        g_theme_light = !g_theme_light;
                        theme_set(g_theme_light);
                        bg_cache_invalidate();
                        mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                    } else {
                        // Reboot.
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
            // Пре-скан: если клик ВНУТРИ видимого окна — НЕ трогаем иконки
            // рабочего стола (иначе клик «просачивается» сквозь окно на
            // иконку под ним). Окна обрабатываются ниже, как обычно.
            int click_in_win = 0;
            for (int i = 0; i < MAXWIN; i++) {
                if (wins[i].used && wins[i].visible &&
                    mx >= wins[i].x && mx < wins[i].x + wins[i].w &&
                    my >= wins[i].y && my < wins[i].y + wins[i].h) {
                    click_in_win = 1; break;
                }
            }
            // Системные иконки (My PC, Terminal, Docs, Wallpaper).
            for (int i = 0; i < NICON_SYS && !handled && !click_in_win; i++) {
                if (icon_hit(i, mx, my)) {
                    unsigned int now = timer_ticks();
                    if (i == last_icon && (now - last_icon_tick) < 80)
                        run_program(dicons[i].elf);
                    last_icon = i;
                    last_icon_tick = now;
                    handled = 1;
                }
            }

            // Иконки дисков: двойной клик — Files с аргументом "D<n>:/".
            for (int i = 0; i < disk_count && !handled; i++) {
                int ix = disk_icons[i].x, iy = disk_icons[i].y;
                if (mx >= ix && mx < ix + 32 && my >= iy && my < iy + 32) {
                    unsigned int now = timer_ticks();
                    int code = 100 + i;
                    if (code == last_icon && (now - last_icon_tick) < 80) {
                        // Формат Dx:// (двойной слеш), как принято в ОС.
                        char arg[8]; arg[0]='D'; arg[1]=(char)('0'+disk_drive[i]);
                        arg[2]=':'; arg[3]='/'; arg[4]='/'; arg[5]=0;
                        api_run_program_arg(disk_icons[i].elf, arg);
                    }
                    last_icon = code;
                    last_icon_tick = now;
                    handled = 1;
                }
            }

            // Динамические иконки из корня: двойной клик.
            for (int i = 0; i < dyn_count && !handled && !click_in_win; i++) {
                int ix = dyn_icons[i].x, iy = dyn_icons[i].y;
                if (mx >= ix && mx < ix + 32 && my >= iy && my < iy + 32) {
                    unsigned int now = timer_ticks();
                    int code = 200 + i;
                    if (code == last_icon && (now - last_icon_tick) < 80) {
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
                    last_icon = code;
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
                    mark_dirty(w->x - WIN_SHADOW_PAD, w->y - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD);
                    if (w->is_program) {
                        // Освобождаем ОБА буфера (раньше text_buf утекал).
                        buf_free(w->buf_idx);
                        buf_free(w->text_buf_idx);
                        w->used = 0; w->buf = 0; w->buf_idx = -1;
                        w->text_buf = 0; w->text_buf_idx = -1;
                        // Помечаем на выход ИМЕННО задачу-владельца окна.
                        // Раньше был глобальный g_exit_request, который убивал
                        // ту задачу, что сейчас в api_yield — то есть ЧУЖУЮ.
                        if (!handle_kbd && w->task_id > 0)
                            task_request_exit(w->task_id, 0);
                    } else {
                        w->visible = 0;
                    }
                    drag_win = -1; handled = 1; break;
                }
                if (hit_btn && mx >= cbx - 16 && mx < cbx - 16 + 12) {
                    mark_dirty(w->x - WIN_SHADOW_PAD, w->y - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD);
                    if (!w->maximized) {
                        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
                        w->x = 0; w->y = 0;
                        // Растягиваем ЛЮБОЕ окно (буфер теперь 800x600).
                        // Содержимое программ останется в верхнем-левом углу,
                        // остальное — WIN_TRANSPARENT -> фон окна.
                        w->w = (int)vbe_width(); w->h = (int)vbe_height() - TASKBAR_H;
                        w->maximized = 1;
                    } else {
                        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
                        w->maximized = 0;
                    }
                    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                    drag_win = -1; handled = 1; break;
                }
                if (hit_btn && mx >= cbx - 32 && mx < cbx - 32 + 12) {
                    mark_dirty(w->x - WIN_SHADOW_PAD, w->y - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD);
                    w->minimized = 1; w->visible = 0; drag_win = -1;
                    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
                    handled = 1; break;
                }
                // Клик в ЛЮБОЕ место окна (кроме кнопок выше) → активировать.
                // Заголовок — заодно начать drag. Тело — просто фокус.
                if (mx >= w->x && mx < w->x + w->w &&
                    my >= w->y && my < w->y + w->h) {
                    bring_to_front(i);
                    // bring_to_front сдвинул массив — окно теперь в MAXWIN-1.
                    // Обновляем i, иначе последующий код (Files) использует старый индекс.
                    i = MAXWIN - 1;
                    for (int j = 0; j < MAXWIN; j++) wins[j].active = (j == i) ? 1 : 0;
                    if (my < wins[i].y + 20) {
                        // клик в заголовок — тащим
                        drag_win = i;
                        wins[i].grab_dx = mx - wins[i].x;
                        wins[i].grab_dy = my - wins[i].y;
                    } else {
                        // клик в тело — не тащим
                        drag_win = -1;
                    }
                    for (int j = 0; j < MAXWIN; j++)
                        if (wins[j].used && wins[j].visible)
                            mark_dirty(wins[j].x - WIN_SHADOW_PAD, wins[j].y - WIN_SHADOW_PAD, wins[j].w + 2*WIN_SHADOW_PAD, wins[j].h + 2*WIN_SHADOW_PAD);
                    handled = 1;
                    // НЕ break: ниже — обработка кликов по файлам в окне Files.
                    // Но она проверяет w->title, а w указывает на старый индекс.
                    // w теперь указывает на то же окно, но по новому индексу (i).
                    // Никакой Files-специфики ниже нет — вложенные клики
                    // обрабатывает сама программа FILES.ELF.
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
            // НЕ ставим changed = 1: при drag достаточно dirty-области
            // (bbox старой+новой позиции). changed = 1 означало бы
            // «полная перерисовка всего экрана» — тормозит QEMU и даёт
            // мелькание. Раньше это было скрытой причиной того, что
            // оптимизации drag не работали.
            mark_dirty(ox - WIN_SHADOW_PAD, oy - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD);
            mark_dirty(nx - WIN_SHADOW_PAD, ny - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD);
        }
    }
    prev_left = left;
    return changed;
}

// Тень окна: МЯГКАЯ и СКРУГЛЁННАЯ (Волна 1, исправлено).
// Раньше были прямые прямоугольники -> торчали углы. Теперь каждый слой —
// СКРУГЛЁННЫЙ прямоугольник (fb_backbuffer_round_rect_alpha), расширяется
// наружу и бледнеет. Радиус слоя = corner_radius + расширение, чтобы
// тень огибала скруглённые углы окна без «мусора».
static void draw_shadow(int x, int y, int w, int h) {
    unsigned int sc = COL_SHADOW;
    unsigned int base_a = g_theme.shadow_alpha;
    int r0 = g_theme.corner_radius;
    // 5 слоёв: расширение растёт, альфа падает.
    static const int grow[5] = { 1, 2, 4, 6, 8 };
    static const int frac[5] = { 55, 40, 28, 18, 10 };
    for (int i = 4; i >= 0; i--) {
        unsigned int a = (base_a * (unsigned int)frac[i]) / 100u;
        if (a == 0) continue;
        int g = grow[i];
        int ox = x - g + 1, oy = y - g + 1;
        int ow = w + 2 * g, oh = h + 2 * g;
        if (ow <= 0 || oh <= 0) continue;
        fb_backbuffer_round_rect_alpha(ox, oy, ow, oh, r0 + g, sc, a);
    }
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

// ============================================================
// СКРУГЛЕНИЕ УГЛОВ ОКНА (Волна 1, v2).
// ============================================================
// Раньше затирали углы цветом из bg_cache -> выглядело как «наклейка»
// фона, а тень в углах пропадала. Теперь: СОХРАНЯЕМ угловые пиксели
// (там уже нарисована тень) ДО рисования окна, рисуем окно, затем
// ВОССТАНАВЛИВАЕМ сохранённое по маске-кругу. Сквозь скруглённые углы
// видна тень, а не рабочий стол.
#define CORNER_MAX_R 20
static unsigned int g_corner_save[CORNER_MAX_R * CORNER_MAX_R * 4];

static int corner_r(int w, int h, int r) {
    if (r <= 0) return 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r > CORNER_MAX_R) r = CORNER_MAX_R;
    return r;
}

// Сохранить угловые пиксели (4 угла x r x r).
static void corners_save(int x, int y, int w, int h, int r) {
    r = corner_r(w, h, r);
    if (r <= 0) return;
    for (int cy = 0; cy < r; cy++)
        for (int cx = 0; cx < r; cx++) {
            int idx = (cy * r + cx) * 4;
            g_corner_save[idx + 0] = fb_backbuffer_read_pixel(x + cx,           y + cy);
            g_corner_save[idx + 1] = fb_backbuffer_read_pixel(x + w - 1 - cx,   y + cy);
            g_corner_save[idx + 2] = fb_backbuffer_read_pixel(x + cx,           y + h - 1 - cy);
            g_corner_save[idx + 3] = fb_backbuffer_read_pixel(x + w - 1 - cx,   y + h - 1 - cy);
        }
}

// Восстановить: снаружи круга — сохранённый пиксель (тень),
// внутри — оставить нарисованное окно.
static void corners_round(int x, int y, int w, int h, int r) {
    r = corner_r(w, h, r);
    if (r <= 0) return;
    for (int cy = 0; cy < r; cy++) {
        for (int cx = 0; cx < r; cx++) {
            int ddx = r - cx;
            int ddy = r - cy;
            int dist2 = (ddx*2-1)*(ddx*2-1) + (ddy*2-1)*(ddy*2-1);
            int rr = r * 2;
            int rr2 = rr * rr;
            int inside;
            if (dist2 <= rr2) inside = 255;
            else {
                int rr_out = (rr + 2) * (rr + 2);
                if (dist2 >= rr_out) inside = 0;
                else inside = 255 - ((dist2 - rr2) * 255) / (rr_out - rr2);
            }
            int outside = 255 - inside;
            if (outside <= 0) continue;
            int idx = (cy * r + cx) * 4;
            int PX[4], PY[4];
            PX[0]=x + cx;          PY[0]=y + cy;
            PX[1]=x + w - 1 - cx;  PY[1]=y + cy;
            PX[2]=x + cx;          PY[2]=y + h - 1 - cy;
            PX[3]=x + w - 1 - cx;  PY[3]=y + h - 1 - cy;
            for (int k = 0; k < 4; k++) {
                unsigned int sav = g_corner_save[idx + k];
                fb_backbuffer_blend_pixel(PX[k], PY[k], sav, (unsigned int)outside);
            }
        }
    }
}

static void draw_window(window_t* w, int rx, int ry, int rw, int rh) {
    if (!w->used || !w->visible) return;
    if (!rects_overlap(rx, ry, rw, rh, w->x - WIN_SHADOW_PAD, w->y - WIN_SHADOW_PAD, w->w + 2*WIN_SHADOW_PAD, w->h + 2*WIN_SHADOW_PAD)) return;

    // Тень.
    draw_shadow(w->x, w->y, w->w, w->h);

    // СОХРАНИТЬ угловые пиксели (там уже тень) ДО рисования тела окна.
    // Потом восстановим их по маске-кругу -> сквозь скруглённые углы
    // будет видна тень, а не «наклейка» фона.
    corners_save(w->x, w->y, w->w, w->h, g_theme.corner_radius);

    // Тело окна.
    fb_backbuffer_fill(w->x, w->y, w->w, w->h, COL_WIN_BG);

    // Заголовок: градиент (активный — синий, неактивный — серый).
    unsigned int t1 = w->active ? COL_TITLE_ACT : COL_TITLE_INA;
    unsigned int t2 = w->active ? g_theme.title_act2 : g_theme.title_ina2;
    fill_grad_v(w->x, w->y, w->w, 20, t1, t2);

    // Кнопки окна (Волна 2): hover-подсветка + иконка.
    // Max/Min — тёмные в светлой теме, светлые в тёмной. Close — красный.
    const icon_t* ic_max = g_theme_light ? &ICON_BTN_MAX_DARK : &ICON_BTN_MAX;
    const icon_t* ic_min = g_theme_light ? &ICON_BTN_MIN_DARK : &ICON_BTN_MIN;
    {
        int mxs = mouse_x(), mys = mouse_y();
        int by = w->y + 4;
        // подложки: close — красная, остальные — нейтральные.
        // Зона клика в process_events — 12x12 от cbx. Подложку рисуем чуть
        // больше (16x16), чтобы hover выглядел мягко.
        struct { int cbx; const icon_t* ic; unsigned int hov_col; } btns[3] = {
            { w->x + w->w - 18, &ICON_BTN_CLOSE,      FB_RGB(0xE8, 0x3B, 0x3B) },
            { w->x + w->w - 34, ic_max,               0 },
            { w->x + w->w - 50, ic_min,               0 },
        };
        for (int bi = 0; bi < 3; bi++) {
            int cbx = btns[bi].cbx;
            int hov = (mxs >= cbx - 2 && mxs < cbx + 14 && mys >= by - 2 && mys < by + 14);
            if (hov) {
                unsigned int hc = btns[bi].hov_col ? btns[bi].hov_col : COL_TASKBAR_HI;
                fb_backbuffer_round_rect_alpha(cbx - 2, by - 2, 16, 16, 4, hc, 200);
            }
            draw_icon(cbx, by, btns[bi].ic);
        }
    }

    // Заголовок (текст) — ЦЕНТРИРУЕМ (Волна 2).
    // bb_text_t: только буквы, БЕЗ плоского фона — иначе на градиенте
    // заголовка виден прямоугольник другого оттенка.
    {
        int tl = 0; while (w->title[tl]) tl++;
        int tw = tl * 8;
        int tx = w->x + (w->w - tw) / 2;
        int min_tx = w->x + 8;
        int max_tx = w->x + w->w - 62 - tw;   // не залезать на кнопки
        if (max_tx < min_tx) max_tx = min_tx;
        if (tx < min_tx) tx = min_tx;
        if (tx > max_tx) tx = max_tx;
        bb_text_t(tx, w->y + 6, w->title, COL_TEXT);
    }

    // Тонкий разделитель под заголовком (Волна 2) — «стеклянная» полоса.
    fb_backbuffer_line(w->x + 1, w->y + 19, w->x + w->w - 2, w->y + 19, COL_SEP);

    // Тонкая рамка.
    fb_backbuffer_rect(w->x, w->y, w->w, w->h, COL_WIN_BORDER);

    // Содержимое: СНАЧАЛА статичный текст (нижний слой), ПОТОМ buf (анимация).
    // buf блитится С ПРОЗРАЧНОСТЬЮ по WIN_TRANSPARENT: где программа залила
    // фон "прозрачным" цветом, виден текст из text_buf; где нарисован объект —
    // он текст накрывает.
    // ВАЖНО: stride буфера — PWIN_MAXW * 3 (фиксированный), а не w->w * 3.
    // При maximize w->w = 800, но пиксели лежат с шагом PWIN_MAXW. Если
    // передать w->w * 3 — прочитаем со сдвигом и увидим "смазывание".
    if (w->is_program && w->text_buf) {
        int ch = w->h - 20;
        fb_backbuffer_blit(w->x, w->y + 20, w->text_buf, w->w, ch, PWIN_MAXW * 3);
    }
    if (w->is_program && w->buf) {
        int ch = w->h - 20;
        // blit_key_alpha: WIN_TRANSPARENT не рисуется, остальные пиксели
        // смешиваются с backbuffer по w->alpha (255 = старый быстрый путь).
        fb_backbuffer_blit_key_alpha(w->x, w->y + 20, w->buf, w->w, ch,
                                     PWIN_MAXW * 3, WIN_TRANSPARENT, w->alpha);
    } else if (w->draw_content) {
        w->draw_content(w);
    }

    // Скругление углов (Волна 1, v2): восстанавливаем сохранённые угловые
    // пиксели (тень) по маске-кругу. Делаем В САМОМ КОНЦЕ, после содержимого.
    corners_round(w->x, w->y, w->w, w->h, g_theme.corner_radius);
}

static void draw_taskbar_region(int rx,int ry,int rw,int rh) {
    int y = (int)vbe_height() - TASKBAR_H;
    int sw = (int)vbe_width();
    if (!rects_overlap(rx,ry,rw,rh, 0,y,sw,TASKBAR_H)) return;
    // Градиент фона таскбара (заметный: светлее сверху).
    fill_grad_v(0, y, sw, TASKBAR_H, COL_TASKBAR, g_theme.taskbar2);
    fb_backbuffer_line(0, y, sw, y, COL_SEP);

    // Кнопка Start с hover — СКРУГЛЁННАЯ (Волна 2).
    int mx = mouse_x(), my = mouse_y();
    int start_hi = (mx >= 4 && mx < 74 && my >= y + 3 && my < y + TASKBAR_H - 3) || start_open;
    unsigned int sc = start_hi ? COL_ACCENT_HI : COL_ACCENT;
    fb_backbuffer_round_rect_alpha(4, y + 4, 70, TASKBAR_H - 8, 6, sc, 255);
    bb_text(12, y + 10, "Start", COL_TEXT, sc);

    // Кнопки свёрнутых окон — скруглённые (Волна 2).
    int bx = 80;
    for (int i = 0; i < MAXWIN; i++) {
        if (!wins[i].used || !wins[i].minimized) continue;
        int hi = (mx >= bx && mx < bx + 100 && my >= y + 3 && my < y + TASKBAR_H - 3);
        unsigned int bc = hi ? COL_TASKBAR_HI : g_theme.taskbar_btn;
        fb_backbuffer_round_rect_alpha(bx, y + 4, 100, TASKBAR_H - 8, 6, bc, 255);
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

// ============================================================
// BG_CACHE — кэш "нижнего слоя" (обои + иконки).
// ============================================================
// Один раз (и при изменениях) рисуем сюда обои + иконки. При drag окон
// render_region просто КОПИРУЕТ этот прямоугольник, не перерисовывая
// обои заново. Экономит ~70% работы при drag и открытии окон.
// g_bg_cache_valid объявлен в начале файла (нужен round_corners).

static void bg_cache_rebuild(void) {
    // Рисуем в BG_CACHE_ADDR, временно подменив backbuffer.
    unsigned char* saved = fb_backbuffer_set_target((unsigned char*)BG_CACHE_ADDR);
    wall_blit_region(0, 0, (int)vbe_width(), (int)vbe_height());
    draw_icons_region(0, 0, (int)vbe_width(), (int)vbe_height());
    fb_backbuffer_set_target(saved);
    g_bg_cache_valid = 1;
}

// Инвалидировать кэш (вызывать при смене обоев / пересборке иконок).
static void bg_cache_invalidate(void) { g_bg_cache_valid = 0; }

static void bg_cache_blit_region(int rx, int ry, int rw, int rh) {
    if (!g_bg_cache_valid) bg_cache_rebuild();
    int sw = (int)vbe_width();
    const unsigned char* cache = (const unsigned char*)BG_CACHE_ADDR;
    for (int y = ry; y < ry + rh; y++) {
        const unsigned char* srow = cache + ((unsigned int)y * (unsigned int)sw + (unsigned int)rx) * 3u;
        fb_backbuffer_blit(rx, y, srow, rw, 1, sw * 3);
    }
}

static void render_region(int rx,int ry,int rw,int rh) {
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > (int)vbe_width())  rw = (int)vbe_width() - rx;
    if (ry + rh > (int)vbe_height()) rh = (int)vbe_height() - ry;
    if (rw <= 0 || rh <= 0) return;
    bg_cache_blit_region(rx, ry, rw, rh);   // обои+иконки из кэша
    for (int i = 0; i < MAXWIN; i++) draw_window(&wins[i], rx, ry, rw, rh);
    draw_taskbar_region(rx, ry, rw, rh);
    draw_start_menu(rx, ry, rw, rh);
    draw_ctx_menu(rx, ry, rw, rh);
}

// Слой курсора: фон под курсором сохраняем и восстанавливаем, чтобы
// быстрый мах мышью не "смазывал" фон. Курсор НЕ впекается в кадр.
#define CUR_W 12
#define CUR_H 19
static unsigned int g_cur_bg[CUR_H][CUR_W];
static int g_cur_saved = 0;      // 1 = g_cur_bg содержит фон под курсором
static int g_cur_sx = -1000, g_cur_sy = -1000;

// Прочитать пиксель backbuffer (0xRRGGBB).
static unsigned int bb_get(int x, int y) {
    if (x < 0 || y < 0 || x >= (int)vbe_width() || y >= (int)vbe_height()) return 0;
    const unsigned char* p = (const unsigned char*)0x01000000u
        + ((unsigned int)y * (unsigned int)vbe_width() + (unsigned int)x) * 3u;
    return ((unsigned int)p[2] << 16) | ((unsigned int)p[1] << 8) | p[0];
}

// Восстановить фон под ранее нарисованным курсором.
static void cursor_bb_restore(void) {
    if (!g_cur_saved) return;
    for (int row = 0; row < CUR_H; row++)
        for (int col = 0; col < CUR_W; col++)
            fb_backbuffer_pixel(g_cur_sx + col, g_cur_sy + row, g_cur_bg[row][col]);
    g_cur_saved = 0;
}

static void draw_cursor_bb(int mx, int my) {
    // В ЗАХВАТЕ (игра) курсор ОС скрыт - игра рисует свой прицел.
    if (mouse_grabbed()) { g_cur_drawn_x = -1000; g_cur_drawn_y = -1000; return; }
    // render_region() ДО этого уже нарисовал чистый фон в регионе.
    // Просто рисуем курсор поверх свежего фона.
    for (int row = 0; row < 19; row++)
        for (int col = 0; col < 12; col++) {
            unsigned char m = CUR[row][col];
            if (m == 1) fb_backbuffer_pixel(mx + col, my + row, 0xFFFFFF);
            else if (m == 2) fb_backbuffer_pixel(mx + col, my + row, 0x000000);
        }
    // Запомнить, куда курсор нарисован: любой отрисовщик обязан быть учтён
    // при вычислении области перерисовки (иначе остаётся "мусор").
    g_cur_drawn_x = mx; g_cur_drawn_y = my;
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
// prog_buf вынесен из .bss в верхнюю RAM: 256 КБ в .bss приближали её конец
// к стеку ядра (0x90000) — риск коллизии при глубоких вызовах ("улёт").
#define PROG_BUF_ADDR 0x02A00000u
static unsigned char* prog_buf = (unsigned char*)PROG_BUF_ADDR;

// Защита от реентрантности: пока программа работает, повторный run_program
// (например, из рекурсивного process_events по всё ещё нажатой мыши) запрещён.
// Иначе WINAPP -> yield -> process_events -> WINAPP -> ... перетирает стек.


// ============================================================
// АВТО-ЗАГРУЗКА PE-DLL (для импортов PE-программ)
// ============================================================
// Вызывается из pe_load_imports, когда программа импортирует DLL.
// Ищет файл "/kernel/SYS/<NAME>.DLL", грузит в DLL-регион и регистрирует
// экспорты. Возвращает базу или 0.
// Буфер под PE-DLL — в верхней RAM, НЕ в .bss (256 КБ в .bss переполняли
// его за стек ядра 0x90000 -> стек и .bss пересекались -> порча всего).
#define DLL_BUF_ADDR 0x02B00000u
static unsigned char* dll_buf = (unsigned char*)DLL_BUF_ADDR;

unsigned int pe_load_dll_by_name(const char* name) {
    if (!name || !name[0]) return 0;
    // Уже загружена? (msdll знает про встроенные + ранее загруженные)
    msdll_entry_t* e = msdll_find(name);
    if (e && e->base) return e->base;

    // Собрать путь /kernel/SYS/NAME.DLL (8.3, имя в верхнем регистре).
    char path[64]; int p = 0;
    const char* pre = "D0://kernel/SYS/";
    for (int i = 0; pre[i]; i++) path[p++] = pre[i];
    for (int i = 0; name[i] && p < 56; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        path[p++] = c;
    }
    const char* ext = ".DLL";
    for (int i = 0; ext[i] && p < 62; i++) path[p++] = ext[i];
    path[p] = 0;

    ata_select(0);
    int sz = vfs_read_file(path, dll_buf, 256 * 1024);
    if (sz <= 0) { serial_print("[pe] DLL not found: "); serial_print(path); serial_print("\n"); return 0; }

    // Выделить регион под образ и разложить.
    unsigned int base = msdll_region_alloc(0x10000u);   // 64 КБ на DLL
    if (!base) return 0;
    int r = pe_load_dll(dll_buf, (unsigned int)sz, base, name);
    if (r <= 0) return 0;
    serial_print("[pe] DLL loaded: "); serial_print(name); serial_print("\n");
    return base;
}

static int run_program(const char* name) {
    // Обычный запуск БЕЗ аргумента: сбросить аргумент, чтобы новая программа
    // не получила путь от предыдущей (баг: FILES открывался по чужому пути).
    g_prog_arg[0] = 0;
    // Ограничение "одна программа" снято: каждая задача изолирована (своя PD).
    // prog_buf используется только на время загрузки — это безопасно.
    ata_select(0);
    // prog_buf: 0x02A00000..0x02B00000 (1 МБ) - до DLL-буфера.
    // Раньше было 256 КБ - мало для Doom (524 КБ).
    int sz = vfs_read_file(name, prog_buf, 1024 * 1024);
    serial_print("[run] "); serial_print(name);
    serial_print(" sz="); serial_print_int(sz); serial_print("\n");
    if (sz <= 0) return -1;
    while (kb_available()) kb_getchar();
    // Мультизагрузчик: формат по магии.
    //   'MZ'      -> PE (.exe)
    //   0x7F'ELF' -> ELF
    // Создаём ЗАДАЧУ (своё адресное пространство) вместо блокирующего exec_run.
    int id;
    if (sz >= 2 && prog_buf[0] == 'M' && prog_buf[1] == 'Z')
        id = task_create_pe(prog_buf, (unsigned int)sz, &g_api);
    else
        id = task_create_elf(prog_buf, (unsigned int)sz, &g_api);
    serial_print("[run] task id="); serial_print_int(id); serial_print("\n");
    if (id < 0) return -1;
    mark_dirty(0, 0, (int)vbe_width(), (int)vbe_height());
    return 0;
}

// --- ДИАГНОСТИКА для реального железа: запись символа прямо в VGA-текст
// (0xB8000). Работает ВСЕГДА в текстовом режиме, не требует VBE. Увидим,
// докуда доходит ядро, даже если графика не завелась. Убрать потом.
static void diag_mark(int pos, char c) {
    volatile unsigned char* vga = (volatile unsigned char*)0xB8000;
    vga[pos * 2] = (unsigned char)c;
    vga[pos * 2 + 1] = 0x0F;   // белый на чёрном
}

static int g_dhcp_started = 0;

void kernel_main(void) {
    serial_init();       // COM1 — отладочный вывод в -serial stdio
    serial_print("=== MysticOS 2 kernel_main ===\n");
    // Проверка RDRAND (аппаратный ГПСЧ). Нужен для HTTPS/TLS.
    {
        unsigned int eax, ebx, ecx, edx;
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        int has = (ecx >> 30) & 1;
        serial_print("[cpu] RDRAND: ");
        serial_print(has ? "YES\n" : "NO\n");
    }
    // Self-test SHA-256 (FIPS 180-4). Тест-вектор NIST: SHA256("abc") =
    // ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    {
        extern void sha256(const void*, unsigned int, unsigned char[32]);
        unsigned char h[32];
        sha256("abc", 3, h);
        static const unsigned char expect[32] = {
            0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
            0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
        };
        int ok = 1;
        for (int i = 0; i < 32; i++) if (h[i] != expect[i]) { ok = 0; break; }
        serial_print("[sha256] test: ");
        serial_print(ok ? "OK\n" : "FAIL\n");
    }
    // Self-test HMAC-SHA256 (RFC 4231, Test Case 1):
    // key = 0x0b x 20, data = "Hi There" ->
    // b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7
    {
        extern void hmac_sha256(const void*, unsigned int, const void*, unsigned int, unsigned char[32]);
        unsigned char key[20]; for (int i = 0; i < 20; i++) key[i] = 0x0b;
        unsigned char out[32];
        hmac_sha256(key, 20, "Hi There", 8, out);
        static const unsigned char expect[32] = {
            0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
            0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
        };
        int ok = 1;
        for (int i = 0; i < 32; i++) if (out[i] != expect[i]) { ok = 0; break; }
        serial_print("[hmac] test: ");
        serial_print(ok ? "OK\n" : "FAIL\n");
    }
    // Self-test rand: 16 байт дважды — должны различаться (не все нули,
    // не совпадать). Настоящая проверка — что RDRAND выдаёт разные данные.
    {
        extern void rand_init(void);
        extern int rand_bytes(unsigned char*, unsigned int);
        extern int rand_has_rdrand(void);
        rand_init();
        unsigned char a[16], b[16];
        rand_bytes(a, 16);
        rand_bytes(b, 16);
        int diff = 0, nonzero = 0;
        for (int i = 0; i < 16; i++) {
            if (a[i] != b[i]) diff = 1;
            if (a[i] != 0) nonzero = 1;
        }
        serial_print("[rand] test: ");
        serial_print((diff && nonzero) ? "OK" : "FAIL");
        serial_print(rand_has_rdrand() ? " (rdrand)\n" : " (fallback)\n");
    }
    // Self-test AES-128 (NIST SP 800-38A, F.1.1):
    // key=000102...0f pt=00112233...ff -> ct=69c4e0d86a7b0430d8cdb78070b4c55a
    {
        extern void aes128_init(void*, const unsigned char[16]);
        extern void aes128_encrypt_block(const void*, const unsigned char[16], unsigned char[16]);
        unsigned char k[16], pt[16];
        for (int i = 0; i < 16; i++) { k[i] = (unsigned char)i; pt[i] = (unsigned char)(i*0x11); }
        static const unsigned char expect[16] = {
            0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a
        };
        unsigned int ctx[44];
        aes128_init(ctx, k);
        unsigned char out[16];
        aes128_encrypt_block(ctx, pt, out);
        int ok = 1;
        for (int i = 0; i < 16; i++) if (out[i] != expect[i]) { ok = 0; break; }
        serial_print("[aes128] test: ");
        serial_print(ok ? "OK\n" : "FAIL\n");
    }
    // Self-test BigInt: 2^10 mod 1000 = 24, 3^100 mod 101 = 1 (Ферма).
    {
        extern void bn_from_u32(void*, unsigned int);
        extern void bn_modpow(void*, const void*, const void*, const void*);
        unsigned int a[64], b[64], m[64], r[64];
        bn_from_u32(a, 2); bn_from_u32(b, 10); bn_from_u32(m, 1000);
        bn_modpow(r, a, b, m);
        int t1 = (r[0] == 24);
        bn_from_u32(a, 3); bn_from_u32(b, 100); bn_from_u32(m, 101);
        bn_modpow(r, a, b, m);
        int t2 = (r[0] == 1);
        serial_print("[bigint] test: ");
        serial_print((t1 && t2) ? "OK\n" : "FAIL\n");
    }
    diag_mark(0, 'A');   // ядро стартовало (до vbe_init)
    if (!vbe_init()) {
        serial_print("[FATAL] vbe_init failed\n");
        for (;;) __asm__ volatile ("hlt");
    }
    diag_mark(1, 'B');   // vbe_init вернул успех
    fb_backbuffer_init();
    theme_init();   // палитра (Dark по умолчанию, см. theme.c)
    // console_init ОБЯЗАТЕЛЕН: без него console.cols/rows = 0, и первый же
    // console_putc сделает while(cy>=0) scroll() — сдвинет ВЕСЬ LFB вверх
    // на 8 строк. Так у нас «улетал рабочий стол» при put_char/print_hex.
    console_init();
    // Буфер обоев: фиксированный адрес в верхней RAM (см. WALL_BUF_ADDR).
    g_wall = (unsigned char*)WALL_BUF_ADDR;
    g_wall_on = 0;   // загрузим дефолт ниже, после инициализации ATA
    // Включить страничную адресацию (identity, PSE 4 МБ). Если система уйдёт
    // в перезагрузку/зависание сразу после старта — закомментируй эту строку.
    paging_init();
    diag_mark(2, 'C');   // paging_init() прошёл (если видим C — paging не убил систему)
    pmem_init();

    // VirtIO-GPU: если есть — вывод пойдёт через GPU (быстрее).
    // init находит устройство 0x1050 и настраивает controlq; setup_framebuffer
    // создаёт экранный ресурс и привязывает backbuffer (0x1000000).
    if (gpu_init()) {
        if (gpu_setup_framebuffer(vbe_width(), vbe_height())) {
            serial_print("[gpu] framebuffer via GPU ready\n");
            gpu_cursor_init();   // аппаратный курсор (64x64)
        } else {
            serial_print("[gpu] setup_framebuffer failed, fallback to LFB\n");
        }
    } else {
        serial_print("[gpu] no virtio-gpu, using LFB\n");
    }

    // MMDCM инициализируется ПОСЛЕ idt_init/pic_init (ниже), т.к. ставит свои
    // IDT-гейты для DOS-прерываний, которые иначе перезапишутся IRQ.

    // VDS: инициализация реестра драйверов + сканирование PCI.
    // Пока ни один драйвер не зарегистрирован — pci_dump() выведет
    // все устройства в консоль (видно на панели QEMU в текстовом режиме
    // или в VGA-память, если посмотреть). Позже сюда добавим virtio-net,
    // rtl8723de, virtio-gpu.
    driver_init();
    vss_init();   // Virtual Sound System: таблица звуковых драйверов
    serial_print("=== MysticOS 2 boot ===\n");
    pci_dump_serial();

    // Регистрируем драйверы в VDS. При наличии устройства VDS сам
    // позовёт match/probe в driver_scan().
    virtio_net_init();
    ac97_driver_register();  // звук: AC'97 (VDS match по PCI class 0x0401)
    hda_driver_register();   // звук: Intel HDA (VDS match по PCI class 0x0403)
    amdgpu_driver_register(); // GPU: AMD (VDS match по vendor 0x1002 + class 0x03)
    int probes = driver_scan();
    serial_print("[vds] successful probes: ");
    serial_print_int(probes);
    serial_print("\n");
    if (virtio_net_ready()) {
        net_init();
        // НИКАКИХ блокирующих poll'ов до старта UI! net_init отправит ARP,
        // а ответы будут обработаны в главном цикле через net_poll.
        serial_print("[net] init done, polling in main loop\n");
    }

    // Звук: VSS перебирает драйверы и берёт первый рабочий (сейчас HDA).
    if (vss_probe_all()) {
        serial_print("[vss] sound ready: "); serial_print(vss_name()); serial_print("\n");
    } else {
        serial_print("[vss] no sound device\n");
    }

    icons_init();
    icons_layout();
    windows_init();
    task_init();   // ядро = задача 0; задачи ELF получают своё адресное пространство
    gdt_init_all();   // GDT с ring3 + TSS (этап ring3)
    idt_init();
    pic_init();
    // MMDCM: после IDT/PIC, чтобы гейты DOS-прерываний не перезаписались.
    mmdcm_init();   // MMDCM: V86 + IVT-ловушки
    mmdcm_test_dos();   // тест: DOS-программа "Hello from DOS!" через int 21h
    pit_init(100);
    // APIC Timer (для WHPX, где PIC IRQ0 не доставляется). Если APIC есть —
    // он шлёт прерывание на вектор 0x20 (как PIC IRQ0), а EOI идёт в LAPIC.
    // Если APIC недоступен — остаётся классический PIT/PIC.
    if (!apic_init(100)) {
        serial_print("[timer] APIC unavailable, using PIT/PIC\n");
    } else {
        // LAPIC жив -> внешние IRQ идут через IO-APIC (PIC под WHPX мёртв).
        ioapic_init();
        // Маскируем ВСЕ IRQ в PIC, чтобы он не мешал IO-APIC.
        outb(0x21, 0xFF);
        outb(0xA1, 0xFF);
    }
    mouse_init();
    interrupts_enable();
    task_enable(); // включить кооперативное переключение (api_yield -> task_yield)

    // Тестовый бип убран: блокировал загрузку и спамил лог. Звук проверяется
    // через MUSIC/системный SYS_SOUND_BEEP по запросу.

    // msdll: инициализация подсистемы DLL + регистрация виртуальной
    // "mskernel.dll" (не файл — таблица в ядре). Программы смогут
    // звать msdll_resolve("mskernel", "window_create") и т.п.
    msdll_init();
    msdll_region_init();   // обнулить bump-аллокатор DLL-региона (0x64000000)
    msdll_register_builtin("mskernel", g_mskernel_exports);

    ata_select(0);
    vfs_init();   // определить ФС и выбрать активный драйвер
    disks_layout();   // иконки подключённых дисков (D0..D3)
    desktop_scan();   // иконки файлов из корня диска (кроме /kernel)

    // Загрузить дефолтные обои (BMP) из образа. Если файла нет/не BMP —
    // останется fallback-заливка (wall_blit_region).
    wall_load("/kernel/BG/BG.BMP");   // FAT16 8.3: каталог 'BG' (не BACKGROUND)

    // Окон при старте нет — все создаются по требованию (ELF-программы
    // через api_window_create). Приветствие терминала печатается самой
    // программой TERM.ELF при первом запуске.

    diag_mark(3, 'D');   // дошли до первой отрисовки (все init прошли)
    render_region(0, 0, (int)vbe_width(), (int)vbe_height());
    int mx = mouse_x(), my = mouse_y();
    draw_cursor_bb(mx, my);
    fb_flip();
    diag_mark(4, 'E');   // первый fb_flip() выполнен (запись в LFB не упала)
    int old_mx = mx, old_my = my;

    for (;;) {
        // Освобождаем память завершённых программ.
        task_reap();
        // Клавиатуру забирает ядро (терминал) ТОЛЬКО когда программа не работает.
        int prog_alive = task_any_alive();
        int changed = process_events(prog_alive ? 0 : 1);
        update_icon_hover();   // hover-подсветка иконок рабочего стола
        mx = mouse_x(); my = mouse_y();

        // Единый расчёт региона: курсор (все позиции + запас) и dirty.
        int rx = mx, ry = my, rx2 = mx, ry2 = my;
        include_cursor(&rx, &ry, &rx2, &ry2, mx, my, old_mx, old_my);

        if (dirty_w > 0 && dirty_h > 0) {
            if (dirty_x < rx) rx = dirty_x;
            if (dirty_y < ry) ry = dirty_y;
            if (dirty_x + dirty_w > rx2) rx2 = dirty_x + dirty_w;
            if (dirty_y + dirty_h > ry2) ry2 = dirty_y + dirty_h;
        }
        int rw = rx2 - rx, rh = ry2 - ry;

        // Постоянное мигание курсора терминала (пока он активен), независимо
        // от событий. Фаза меняется по таймеру, область окна метится dirty.
        int twin = find_win("Terminal");
        if (twin >= 0 && wins[twin].visible && wins[twin].active && drag_win < 0) {
            unsigned int nowt = timer_ticks();
            if (nowt - g_term_cursor_tick >= 50) {
                g_term_cursor_tick = nowt;
                g_term_cursor_on = !g_term_cursor_on;
                mark_dirty(wins[twin].x - WIN_SHADOW_PAD, wins[twin].y - WIN_SHADOW_PAD, wins[twin].w + 2*WIN_SHADOW_PAD, wins[twin].h + 2*WIN_SHADOW_PAD);
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
            unsigned int _t0 = timer_ticks();
            render_region(rx, ry, rw, rh);
            unsigned int _t1 = timer_ticks();
            draw_cursor_bb(mx, my);
            // Профилировка: время рендера региона в тиках PIT (10мс/тик).
            g_prof_render += (_t1 - _t0);
            g_prof_frames++;
            // Аппаратный курсор (plane) не должен попадать в backbuffer/flush.
            // Если курсор аппаратный - регион курсора в flush НЕ добавляем.
            if (!(gpu_is_ready() && gpu_cursor_hw_enabled())) {
                int cxr = mx + 12, cyr = my + 19;
                if (mx < rx) { rw += rx - mx; rx = mx; }
                if (my < ry) { rh += ry - my; ry = my; }
                if (cxr > rx + rw) rw = cxr - rx;
                if (cyr > ry + rh) rh = cyr - ry;
            }
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

        // Сетевой опрос (неблокирующий): принять и обработать пакеты.
        // Много раз: если в RX-очереди есть пачка — обработаем за кадр.
        for (int _n = 0; _n < 8; _n++) {
            if (!net_poll()) break;
        }

        // Профилировка рендера: раз в секунду — в serial.
        {
            static unsigned int last_prof_t = 0;
            unsigned int nt = timer_ticks();
            if (nt - last_prof_t >= 100) {
                last_prof_t = nt;
                serial_print("[prof] render_frames="); serial_print_int((int)g_prof_frames);
                serial_print(" render_ticks="); serial_print_int((int)g_prof_render);
                serial_print(" frame_calls="); serial_print_int((int)g_prof_frame_calls);
                serial_print(" frame_ticks="); serial_print_int((int)g_prof_frame_ticks);
                extern unsigned int g_music_underrun;
                serial_print(" mus_underrun="); serial_print_int((int)g_music_underrun);
                g_music_underrun = 0;
                serial_print("\n");
                g_prof_frames = 0; g_prof_render = 0;
                g_prof_frame_calls = 0; g_prof_frame_ticks = 0;
            }
        }

        // Звук: дописать микс голосов в кольцевой буфер HDA.
        vss_poll();

        // DHCP: один раз через 1 секунду после старта.
        if (virtio_net_ready() && !g_dhcp_started && timer_ticks() > 100) {
            g_dhcp_started = 1;
            serial_print("[net] starting DHCP\n");
            net_dhcp_discover();
        }

        // (Тестовые DNS/HTTP/ping-задачи убраны: они блокировали ядро
        //  на старте. Сеть теперь используется по требованию — браузером.)

        // Уступить CPU задачам-программам (кооперативно). Вернёмся сюда,
        // когда программа вызовет api->yield() или завершится.
        task_yield();

        __asm__ volatile ("hlt");
    }
}
