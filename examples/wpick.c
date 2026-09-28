// wpick.c - SYSPR: выбор обоев рабочего стола (BMP) как ELF-программа (API v2).
// Навигация по папкам как в FILES.ELF; двойной клик по файлу -> set_wallpaper.
#include "../api.h"

#define FMAX 64
#define ROW_H 16

static const mystic_api_t* g;
static char names[FMAX * 13];
static int  isdir[FMAX];
static unsigned int sizes[FMAX];
static int  count = 0;

static int win_id = -1, win_w = 0, win_h = 0, win_x = 0, win_y = 0;
static char path[128] = "/";

static int at_root(void) { return path[0] == '/' && path[1] == 0; }

static void reload(void) {
    count = g->fs_list(path, names, isdir, sizes, FMAX);
    if (count < 0) count = 0;
}

static void enter(const char* nm) {
    int n = 0; while (path[n]) n++;
    if (n > 0 && path[n-1] != '/') { if (n < 120) path[n++] = '/'; }
    for (int k = 0; nm[k] && n < 120; k++) path[n++] = nm[k];
    path[n] = 0;
    reload();
}

static void go_up(void) {
    int n = 0; while (path[n]) n++;
    if (n <= 1) { path[0] = '/'; path[1] = 0; reload(); return; }
    if (path[n-1] == '/') n--;
    while (n > 1 && path[n-1] != '/') n--;
    if (n <= 1) { path[0] = '/'; path[1] = 0; } else path[n-1] = 0;
    reload();
}

static void full_path(const char* nm, char* out) {
    int n = 0; while (path[n] && n < 150) { out[n] = path[n]; n++; }
    if (n > 0 && out[n-1] != '/') out[n++] = '/';
    for (int k = 0; nm[k] && n < 158; k++) out[n++] = nm[k];
    out[n] = 0;
}

static void render(void) {
    unsigned int bg = 0x181822;
    g->window_fill(win_id, 0, 0, win_w, win_h, bg);
    g->window_text(win_id, 4, 2, "Wallpaper", 0x80FF80, bg);
    g->window_text(win_id, 80, 2, path, 0x80C0FF, bg);   // текущий путь
    int special = at_root() ? 0 : 1;
    int y = 18;
    if (special) {
        g->window_text(win_id, 6, y, "..", 0xFFC060, bg);
        y += ROW_H;
    }
    if (count == 0 && !special) { g->window_text(win_id, 6, y, "(empty)", 0xA0A0B0, bg); return; }
    int maxrows = (win_h - y) / ROW_H;
    for (int i = 0; i < count && i < maxrows; i++) {
        int ry = y + i * ROW_H;
        unsigned int fg = isdir[i] ? 0xFFE060 : 0xE0E0F0;
        g->window_text(win_id, 6, ry, names + i * 13, fg, bg);
        if (!isdir[i]) {
            char sz[16]; int v = (int)sizes[i], p = 0;
            if (v == 0) sz[p++] = '0';
            char tmp[12]; int t = 0;
            while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
            while (t) sz[p++] = tmp[--t];
            sz[p] = 0;
            g->window_text(win_id, 6 + 13 * 8, ry, sz, 0x808090, bg);
        }
    }
}

// Проверить, что имя файла имеет расширение .BMP (регистр не важен).
static int is_bmp(const char* nm) {
    for (int k = 0; nm[k]; k++)
        if (nm[k] == '.' && (nm[k+1]|0x20)=='b' && (nm[k+2]|0x20)=='m' && (nm[k+3]|0x20)=='p')
            return 1;
    return 0;
}

int _start(const mystic_api_t* api) {
    g = api;
    win_w = 320; win_h = 220; win_x = 240; win_y = 120;
    win_id = api->window_create("Wallpaper", win_x, win_y, win_w, win_h);
    if (win_id < 0) return 1;

    reload();
    render();

    char msg[64];
    int msg_n = 0;

    int prev_left = 0, last_idx = -2;
    unsigned int last_tick = 0;
    while (!api->key_available()) {
        int ml = api->mouse_left();
        if (ml && !prev_left) {
            int mx = api->mouse_x(), my = api->mouse_y();
            int lx = mx - win_x, ly = my - (win_y + 20);
            if (lx >= 0 && lx < win_w && ly >= 0 && ly < win_h) {
                int special = at_root() ? 0 : 1;
                int row = (ly - 18) / ROW_H;
                int idx = -2;
                if (special && row == 0) idx = -1;
                else { int fi = row - special; if (fi >= 0 && fi < count) idx = fi; }
                if (idx != -2) {
                    unsigned int now = api->ticks();
                    if (idx == last_idx && (now - last_tick) < 40) {
                        if (idx == -1) { go_up(); }
                        else if (isdir[idx]) { enter(names + idx * 13); }
                        else {
                            // Выбран файл — ставим обоями, если он .BMP.
                            char full[160]; full_path(names + idx * 13, full);
                            int ok = 0;
                            if (is_bmp(names + idx * 13) && api->set_wallpaper) ok = api->set_wallpaper(full);
                            msg_n = 0;
                            const char* r = ok ? "Wallpaper set!" : "Not a BMP / failed";
                            for (int k = 0; r[k] && msg_n < 60; k++) msg[msg_n++] = r[k];
                            msg[msg_n] = 0;
                        }
                        last_idx = -2;
                        render();
                        if (msg_n > 0) g->window_text(win_id, 6, win_h - 14, msg, 0x80FF80, 0x181822);
                    } else { last_idx = idx; last_tick = now; }
                }
            }
        }
        prev_left = ml;
        api->yield();
        api->sleep(20);
    }
    api->key_getchar();
    api->window_close(win_id);
    return 0;
}
