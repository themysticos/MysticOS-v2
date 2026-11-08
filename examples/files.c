// files.c - SYSPR: файловый менеджер (UI-кит). ring3 API.
// Тулбар с навигацией, иконки типов, переключатель дисков, хлебные крошки,
// пустое состояние. Стиль — единый (ms/ui.h).
#include "../include/mysticos.h"
#include "../include/ms/ui.h"

#define FMAX 64
#define ROW_H 20
#define TB_H 24          // высота тулбара
#define PATHBAR_H 20

static char names[FMAX * 13];
static int  isdir[FMAX];
static unsigned int sizes[FMAX];
static int  count = 0;

static int win_id = -1, win_w = 0, win_h = 0, win_x = 0, win_y = 0;
static char path[128] = "/";       // путь БЕЗ префикса диска
static int  g_drive = 0;            // текущий диск
static int  sel = -2;              // выделенная строка

// История навигации (для кнопки "назад").
static char hist[32][128];
static int  hist_n = 0;

static int at_root(void) { return path[0] == '/' && path[1] == 0; }

static void mkpath(char* out) {
    int p = 0;
    out[p++] = 'D'; out[p++] = (char)('0' + (g_drive % 10)); out[p++] = ':';
    for (int k = 0; path[k] && p < 120; k++) out[p++] = path[k];
    if (p == 3) out[p++] = '/';
    out[p] = 0;
}

static void hist_push(const char* p) {
    if (hist_n >= 32) return;
    ui_strcpy(hist[hist_n], p, 128);
    hist_n++;
}

static void reload(void) {
    char fp[128]; mkpath(fp);
    count = ms_fs_list(fp, names, isdir, sizes, FMAX);
    if (count < 0) count = 0;
    sel = -2;
}

static void enter(const char* nm) {
    hist_push(path);
    int n = 0; while (path[n]) n++;
    if (n > 0 && path[n-1] != '/') { if (n < 120) path[n++] = '/'; }
    for (int k = 0; nm[k] && n < 120; k++) path[n++] = nm[k];
    path[n] = 0;
    reload();
}

static void go_up(void) {
    int n = 0; while (path[n]) n++;
    if (n <= 1) { path[0] = '/'; path[1] = 0; reload(); return; }
    hist_push(path);
    if (path[n-1] == '/') n--;
    while (n > 1 && path[n-1] != '/') n--;
    if (n <= 1) { path[0] = '/'; path[1] = 0; } else path[n-1] = 0;
    reload();
}

static void go_back(void) {
    if (hist_n <= 0) return;
    hist_n--;
    ui_strcpy(path, hist[hist_n], 128);
    reload();
}

static void full_path(const char* nm, char* out) {
    int n = 0;
    out[n++] = 'D'; out[n++] = (char)('0' + (g_drive % 10)); out[n++] = ':';
    for (int k = 0; path[k] && n < 150; k++) out[n++] = path[k];
    if (n > 3 && out[n-1] != '/') out[n++] = '/';
    for (int k = 0; nm[k] && n < 158; k++) out[n++] = nm[k];
    out[n] = 0;
}

// Иконка/цвет по типу файла.
static void type_of(const char* nm, int is_d, int* ic, unsigned int* col) {
    if (is_d) { *ic = UI_IC_FOLDER; *col = UI_FOLDER; return; }
    for (int k = 0; nm[k]; k++) {
        if (nm[k] == '.') {
            char a = nm[k+1]|0x20, b = nm[k+2]|0x20, c = nm[k+3]|0x20;
            if (a=='e'&&b=='l'&&c=='f') { *ic=UI_IC_ELF; *col=UI_EXE; return; }
            if (a=='t'&&b=='x'&&c=='t') { *ic=UI_IC_TXT; *col=UI_TXT; return; }
            if (a=='b'&&b=='m'&&c=='p') { *ic=UI_IC_IMG; *col=UI_IMG; return; }
        }
    }
    *ic = UI_IC_FILE; *col = 0xC0C0D0;
}

static int has_ext3(const char* nm, char a, char b, char c) {
    for (int k = 0; nm[k]; k++)
        if (nm[k] == '.' && (nm[k+1]|0x20)==a && (nm[k+2]|0x20)==b && (nm[k+3]|0x20)==c)
            return 1;
    return 0;
}

static void render(int mx, int my, int left, int pl) {
    ms_fill(win_id, 0, 0, win_w, win_h, UI_BG);

    // --- Тулбар ---
    ui_panel(win_id, 0, 0, win_w, TB_H, UI_PANEL);
    int bx = 2;
    ui_icon_button(win_id, bx, 1, UI_IC_BACK, mx, my, left, pl); bx += 24;
    ui_icon_button(win_id, bx, 1, UI_IC_UP,   mx, my, left, pl); bx += 24;
    ui_icon_button(win_id, bx, 1, UI_IC_HOME, mx, my, left, pl); bx += 24;
    ui_icon_button(win_id, bx, 1, UI_IC_REFRESH, mx, my, left, pl); bx += 30;

    // --- Переключатель дисков D0..D3 (только реально существующие) ---
    for (int d = 0; d < 2; d++) {
        int bw = 30;
        int hov = (mx >= bx && mx < bx + bw && my >= 2 && my < TB_H - 2);
        unsigned int bg = (d == g_drive) ? UI_ACCENT : (hov ? UI_PANEL2 : UI_PANEL);
        ms_fill(win_id, bx, 2, bw, TB_H - 4, bg);
        ms_rect(win_id, bx, 2, bw, TB_H - 4, UI_BORDER);
        char dl[3]; dl[0] = 'D'; dl[1] = '0' + d; dl[2] = 0;
        ms_text(win_id, bx + 6, 8, dl, (d == g_drive) ? 0xFFFFFF : UI_TEXT);
        bx += bw + 2;
    }

    // --- Панель пути (хлебные крошки) ---
    ui_panel(win_id, 0, TB_H, win_w, PATHBAR_H, UI_PANEL2);
    int px = 6;
    char dl[4]; dl[0]='D'; dl[1]='0'+g_drive; dl[2]=':'; dl[3]=0;
    ms_text(win_id, px, TB_H + 6, dl, UI_ACCENT); px += 24;   // "D0:" = 3 символа x 8px
    ms_text(win_id, px, TB_H + 6, path, UI_TEXT);

    // --- Список ---
    int y0 = TB_H + PATHBAR_H;
    int maxrows = (win_h - y0 - 16) / ROW_H;
    if (maxrows < 1) maxrows = 1;

    if (count == 0) {
        ui_empty(win_id, win_w / 2, y0 + (win_h - y0) / 2 - 10, "This folder is empty");
    }

    for (int i = 0; i < count && i < maxrows; i++) {
        int ry = y0 + i * ROW_H;
        int ic; unsigned int col;
        type_of(names + i * 13, isdir[i], &ic, &col);
        char sz[16]; sz[0] = 0;
        if (!isdir[i]) ui_size_str(sizes[i], sz);
        ui_list_row(win_id, 0, ry, win_w, ROW_H, ic, col, names + i * 13, sz, sel == i);
    }

    // --- Статусбар ---
    char st[96]; int p = 0;
    char num[12]; ui_int_str(count, num);
    const char* a = "items: ";
    for (int k = 0; a[k]; k++) st[p++] = a[k];
    for (int k = 0; num[k]; k++) st[p++] = num[k];
    const char* b = "   double-click to open";
    for (int k = 0; b[k] && p < 94; k++) st[p++] = b[k];
    st[p] = 0;
    ui_statusbar(win_id, win_w, win_h, st);
}

static int pick(int mx, int my) {
    int lx = mx - win_x, ly = my - win_y;
    if (lx < 0 || lx >= win_w || ly < 0 || ly >= win_h) return -2;
    int y0 = TB_H + PATHBAR_H;
    if (ly < y0) return -2;
    int i = (ly - y0) / ROW_H;
    if (i >= 0 && i < count) return i;
    return -2;
}

// Обработка клика по тулбару. Возвращает 1 если что-то сделано.
static int toolbar_click(int mx, int my, int pl) {
    int lx = mx - win_x, ly = my - win_y;
    if (ly < 0 || ly >= TB_H) return 0;
    int bx = 2;
    if (lx >= bx && lx < bx + 22) { go_back(); return 1; } bx += 24;
    if (lx >= bx && lx < bx + 22) { go_up(); return 1; } bx += 24;
    if (lx >= bx && lx < bx + 22) { hist_push(path); path[0]='/'; path[1]=0; reload(); return 1; } bx += 24;
    if (lx >= bx && lx < bx + 22) { reload(); return 1; } bx += 30;
    for (int d = 0; d < 2; d++) {
        if (lx >= bx && lx < bx + 30) { hist_push(path); g_drive = d; path[0]='/'; path[1]=0; reload(); return 1; }
        bx += 32;
    }
    return 0;
}

int _start(void) {
    const char* a = ms_arg();
    g_drive = 0;
    path[0] = '/'; path[1] = 0;
    if (a && a[0]) {
        const char* rest = a;
        if (a[0] == 'D' && a[1] >= '0' && a[1] <= '9' && a[2] == ':') {
            g_drive = a[1] - '0'; rest = a + 3;
        }
        int rlen = 0; while (rest[rlen]) rlen++;
        if (rlen == 0 || rest[rlen-1] == '/') {
            int k = 0;
            if (rest[0] == '/') { path[k++] = '/'; rest++; }
            while (*rest && k < 120) { path[k++] = *rest++; }
            if (k == 0) path[k++] = '/';
            path[k] = 0;
        }
    }
    win_w = 400; win_h = 260; win_x = 360; win_y = 110;
    win_id = ms_window("Files", win_x, win_y, win_w, win_h);
    if (win_id < 0) ms_exit(1);
    // win_x/win_y теперь = origin КЛИЕНТСКОЙ области (см. ui_client_geom).
    { int vp = ms_window_pos(win_id); win_x = (vp >> 16) & 0xFFFF; win_y = vp & 0xFFFF; }

    reload();
    int prev_left = 0, last_idx = -3;
    unsigned int last_tick = 0;
    int mx = 0, my = 0, ml = 0;
    int frame = 0;
    render(mx, my, ml, prev_left);

    while (!ms_key_avail()) {
        // Реакция на maximize/перемещение: опрашиваем геометрию раз в 4 кадра
        // (на глаз незаметно, но снимает нагрузку с каждого кадра).
        if ((frame++ & 3) == 0)
            ui_client_geom(win_id, &win_x, &win_y, &win_w, &win_h);
        mx = ms_mouse_x(); my = ms_mouse_y(); ml = ms_mouse_left();
        // Перед обработкой клика — свежая геометрия (вдруг maximize/сдвиг).
        if (ml && !prev_left)
            ui_client_geom(win_id, &win_x, &win_y, &win_w, &win_h);
        int lx = mx - win_x, ly = my - win_y;
        if (ml && !prev_left) {
            if (ly >= 0 && ly < TB_H) {
                toolbar_click(mx, my, prev_left);
            } else {
                int idx = pick(mx, my);
                if (idx != -2) {
                    unsigned int now = ms_ticks();
                    if (idx == last_idx && (now - last_tick) < 40) {
                        if (isdir[idx]) enter(names + idx * 13);
                        else {
                            const char* nm = names + idx * 13;
                            char full[160]; full_path(nm, full);
                            if (has_ext3(nm, 'e', 'l', 'f')) {
                                ms_run(full);
                                ms_window_close(win_id);
                                ms_exit(0);
                            } else if (has_ext3(nm, 't', 'x', 't')) {
                                ms_run_arg("/kernel/SYSPR/EDITOR.ELF", full);
                            } else if (has_ext3(nm, 'b', 'm', 'p')) {
                                ms_run_arg("/kernel/SYSPR/IMGVIEW.ELF", full);
                            }
                        }
                        last_idx = -3;
                    } else {
                        sel = idx; last_idx = idx; last_tick = now;
                    }
                }
            }
        }
        prev_left = ml;
        render(mx, my, ml, prev_left);
        ms_yield();
        ms_sleep(20);
    }
    ms_key_get();
    ms_window_close(win_id);
    ms_exit(0);
}
