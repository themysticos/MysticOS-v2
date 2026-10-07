// files.c - SYSPR: файловый менеджер (в стиле Windows Explorer). ring3 API.
// Адресная строка с буквой диска, иконки типов, выделение, двойной клик.
#include "../include/mysticos.h"

#define FMAX 64
#define ROW_H 14

static char names[FMAX * 13];
static int  isdir[FMAX];
static unsigned int sizes[FMAX];
static int  count = 0;

static int win_id = -1, win_w = 0, win_h = 0, win_x = 0, win_y = 0;
static char path[128] = "/";       // путь БЕЗ префикса диска
static int  g_drive = 0;            // текущий диск (D0..D3)
static int sel = -2;              // выделенная строка: -2 ничего, -1 "..", >=0 файл

#define C_BG      0x181820
#define C_ADDR    0x203860
#define C_SEL     0x2A4A8A
#define C_DIR     0xFFD060
#define C_EXE     0x70E070
#define C_TXT     0xE0E0F0
#define C_IMG     0x80D0FF
#define C_FILE    0xC0C0D0
#define C_DIM     0x808090
#define C_STATUS  0x101018

static int strlen8(const char* s);

static int at_root(void) { return path[0] == '/' && path[1] == 0; }

static void mkpath(char* out) {
    int p = 0;
    out[p++] = 'D';
    out[p++] = (char)('0' + (g_drive % 10));
    out[p++] = ':';
    for (int k = 0; path[k] && p < 120; k++) out[p++] = path[k];
    if (p == 3) out[p++] = '/';
    out[p] = 0;
}

static void reload(void) {
    char fp[128]; mkpath(fp);
    count = ms_fs_list(fp, names, isdir, sizes, FMAX);
    if (count < 0) count = 0;
    sel = -2;
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
    int n = 0;
    out[n++] = 'D'; out[n++] = (char)('0' + (g_drive % 10)); out[n++] = ':';
    for (int k = 0; path[k] && n < 150; k++) out[n++] = path[k];
    if (n > 3 && out[n-1] != '/') out[n++] = '/';
    for (int k = 0; nm[k] && n < 158; k++) out[n++] = nm[k];
    out[n] = 0;
}

static void addr_string(char* out) {
    int p = 0;
    out[p++] = 'D';
    out[p++] = (char)('0' + (g_drive % 10));
    out[p++] = ':';
    out[p++] = '\\';
    int k = (path[0] == '/') ? 1 : 0;
    for (; path[k] && p < 120; k++) out[p++] = (path[k] == '/') ? '\\' : path[k];
    out[p] = 0;
}

static void type_of(const char* nm, int is_d, int* kind, char* icon, unsigned int* col) {
    if (is_d) { *kind = 0; icon[0]='<'; icon[1]='D'; icon[2]='>'; icon[3]=0; *col = C_DIR; return; }
    for (int k = 0; nm[k]; k++) {
        if (nm[k] == '.') {
            char a = nm[k+1]|0x20, b = nm[k+2]|0x20, c = nm[k+3]|0x20;
            if (a=='e'&&b=='l'&&c=='f') { *kind=1; icon[0]='['; icon[1]='E'; icon[2]=']'; icon[3]=0; *col=C_EXE; return; }
            if (a=='t'&&b=='x'&&c=='t') { *kind=2; icon[0]='['; icon[1]='T'; icon[2]=']'; icon[3]=0; *col=C_TXT; return; }
            if (a=='b'&&b=='m'&&c=='p') { *kind=3; icon[0]='['; icon[1]='I'; icon[2]=']'; icon[3]=0; *col=C_IMG; return; }
        }
    }
    *kind = 4; icon[0]='['; icon[1]=' '; icon[2]=']'; icon[3]=0; *col = C_FILE;
}

static int is_elf(const char* nm) {
    for (int k = 0; nm[k]; k++)
        if (nm[k] == '.' && (nm[k+1]|0x20)=='e' && (nm[k+2]|0x20)=='l' && (nm[k+3]|0x20)=='f')
            return 1;
    return 0;
}

static int has_ext3(const char* nm, char a, char b, char c) {
    for (int k = 0; nm[k]; k++)
        if (nm[k] == '.' && (nm[k+1]|0x20)==a && (nm[k+2]|0x20)==b && (nm[k+3]|0x20)==c)
            return 1;
    return 0;
}

static void num_to_str(int v, char* out) {
    int p = 0;
    if (v == 0) { out[p++]='0'; out[p]=0; return; }
    char tmp[12]; int t = 0;
    while (v) { tmp[t++] = '0' + (v % 10); v /= 10; }
    while (t) out[p++] = tmp[--t];
    out[p] = 0;
}

static void render(void) {
    ms_fill(win_id, 0, 0, win_w, win_h, C_BG);

    ms_fill(win_id, 0, 0, win_w, 16, C_ADDR);
    ms_text_bg(win_id, 6, 4, "Up", 0xFFFFFF, C_ADDR);
    char addr[130]; addr_string(addr);
    ms_text_bg(win_id, 40, 4, addr, 0xD0E0FF, C_ADDR);

    int y = 20;
    int special = at_root() ? 0 : 1;
    int maxrows = (win_h - y - 14) / ROW_H;
    if (maxrows < 1) maxrows = 1;

    if (special) {
        unsigned int rbg = (sel == -1) ? C_SEL : C_BG;
        ms_text_bg(win_id, 6, y, "<U> ..", 0xFFC060, rbg);
        y += ROW_H;
    }

    if (count == 0 && !special) {
        ms_text_bg(win_id, 6, y + 4, "(empty folder)", C_DIM, C_BG);
    }

    for (int i = 0; i < count && i < maxrows; i++) {
        int ry = y + i * ROW_H;
        unsigned int rbg = (sel == i) ? C_SEL : C_BG;
        int kind; char icon[4]; unsigned int col;
        type_of(names + i * 13, isdir[i], &kind, icon, &col);
        ms_text_bg(win_id, 6, ry, icon, col, rbg);
        ms_text_bg(win_id, 6 + 4 * 8, ry, names + i * 13, col, rbg);
        if (!isdir[i]) {
            char sz[16]; num_to_str((int)sizes[i], sz);
            ms_text_bg(win_id, win_w - 6 - (int)strlen8(sz) * 8, ry, sz, C_DIM, rbg);
        }
    }

    ms_fill(win_id, 0, win_h - 12, win_w, 12, C_STATUS);
    char st[80]; int p = 0;
    const char* pr = "items: ";
    for (int k = 0; pr[k]; k++) st[p++] = pr[k];
    char num[12]; num_to_str(count, num);
    for (int k = 0; num[k]; k++) st[p++] = num[k];
    const char* hint = "   dbl-click = open";
    for (int k = 0; hint[k] && p < 78; k++) st[p++] = hint[k];
    st[p] = 0;
    ms_text_bg(win_id, 4, win_h - 10, st, C_DIM, C_STATUS);
}

static int strlen8(const char* s) { int n = 0; while (s[n]) n++; return n; }

static int pick(int mx, int my) {
    int lx = mx - win_x, ly = my - (win_y + 20);
    if (lx < 0 || lx >= win_w || ly < 0 || ly >= win_h) return -2;
    int special = at_root() ? 0 : 1;
    if (special && ly >= 20 && ly < 20 + ROW_H) return -1;
    int base = 20 + special * ROW_H;
    if (ly < base) return -2;
    int i = (ly - base) / ROW_H;
    if (i >= 0 && i < count) return i;
    return -2;
}

int _start(void) {
    // Стартовый путь: из аргумента (ms_arg) или корень D0.
    const char* a = ms_arg();
    g_drive = 0;
    path[0] = '/'; path[1] = 0;
    if (a && a[0]) {
        const char* rest = a;
        if (a[0] == 'D' && a[1] >= '0' && a[1] <= '9' && a[2] == ':') {
            g_drive = a[1] - '0';
            rest = a + 3;
        }
        int rlen = 0; while (rest[rlen]) rlen++;
        int is_dir_like = (rlen == 0) || rest[rlen-1] == '/';
        if (is_dir_like) {
            int k = 0;
            if (rest[0] == '/') { path[k++] = '/'; rest++; }
            while (*rest && k < 120) { path[k++] = *rest++; }
            if (k == 0) path[k++] = '/';
            path[k] = 0;
        }
    }
    win_w = 380; win_h = 240; win_x = 380; win_y = 120;
    win_id = ms_window("Files", win_x, win_y, win_w, win_h);
    if (win_id < 0) ms_exit(1);

    reload();
    render();

    int prev_left = 0, last_idx = -3;
    unsigned int last_tick = 0;
    while (!ms_key_avail()) {
        int mx = ms_mouse_x(), my = ms_mouse_y();
        int ml = ms_mouse_left();
        if (ml && !prev_left) {
            int lx = mx - win_x, ly = my - (win_y + 20);
            if (lx >= 0 && lx < 36 && ly >= 0 && ly < 16) {
                go_up(); render();
            } else {
                int idx = pick(mx, my);
                if (idx != -2) {
                    unsigned int now = ms_ticks();
                    if (idx == last_idx && (now - last_tick) < 40) {
                        if (idx == -1) go_up();
                        else if (isdir[idx]) enter(names + idx * 13);
                        else {
                            const char* nm = names + idx * 13;
                            char full[160]; full_path(nm, full);
                            if (is_elf(nm)) {
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
                        sel = idx;
                        last_idx = idx; last_tick = now;
                    }
                    render();
                }
            }
        }
        prev_left = ml;
        ms_yield();
        ms_sleep(20);
    }
    ms_key_get();
    ms_window_close(win_id);
    ms_exit(0);
}
