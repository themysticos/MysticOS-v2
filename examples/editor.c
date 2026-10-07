// editor.c - SYSPR: простой текстовый редактор как ELF-программа. ring3 API.
// Открывает файл через ms_arg(). F2 (0x02) или Ctrl+S — сохранить.
#include "../include/mysticos.h"

#define MAXL 200
#define MAXC 78

static char lines[MAXL][MAXC + 2];
static int  nlines = 0;

static int win_id = -1, win_w = 0, win_h = 0;
static int cx = 0, cy = 0;
static int top = 0;
static char fpath[160];
static char status[80];

static void set_status(const char* s) {
    int i = 0; while (s[i] && i < 78) { status[i] = s[i]; i++; } status[i] = 0;
}

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static void load_text(const char* data, int len) {
    nlines = 0; cx = cy = 0; top = 0;
    int c = 0;
    lines[0][0] = 0;
    for (int i = 0; i < len; i++) {
        char ch = data[i];
        if (ch == '\r') continue;
        if (ch == '\n') {
            lines[nlines][c] = 0;
            nlines++;
            c = 0;
            if (nlines >= MAXL) break;
            lines[nlines][0] = 0;
        } else {
            if (c < MAXC) { lines[nlines][c++] = ch; }
        }
    }
    lines[nlines][c] = 0;
    nlines++;
    if (nlines == 0) { nlines = 1; lines[0][0] = 0; }
}

static int build_text(char* out, int max) {
    int p = 0;
    for (int r = 0; r < nlines; r++) {
        int n = slen(lines[r]);
        for (int k = 0; k < n; k++) { if (p < max - 1) out[p++] = lines[r][k]; }
        if (r < nlines - 1) { if (p < max - 1) out[p++] = '\n'; }
    }
    out[p] = 0;
    return p;
}

static void render(void) {
    unsigned int bg = 0x101018, fg = 0xC8E0C8;
    ms_fill(win_id, 0, 0, win_w, win_h, bg);
    ms_text_bg(win_id, 4, 2, fpath, 0x80C0FF, bg);
    int ty = 18;
    int rows = (win_h - ty - 14) / 8;
    if (rows < 1) rows = 1;
    if (cy < top) top = cy;
    if (cy >= top + rows) top = cy - rows + 1;
    for (int r = 0; r < rows; r++) {
        int lr = top + r;
        if (lr >= nlines) break;
        ms_text_bg(win_id, 2, ty + r * 8, lines[lr], fg, bg);
    }
    int scr = cy - top;
    if (scr >= 0 && scr < rows) {
        int px = 2 + cx * 8;
        int py = ty + scr * 8;
        ms_rect(win_id, px, py, 8, 8, 0x80FF80);
    }
    ms_text_bg(win_id, 2, win_h - 12, status, 0xFFE060, bg);
}

static void insert_char(char ch) {
    int n = slen(lines[cy]);
    if (n >= MAXC) return;
    if (cx > n) cx = n;
    for (int k = n; k >= cx; k--) lines[cy][k + 1] = lines[cy][k];
    lines[cy][cx] = ch;
    cx++;
}

static void backspace(void) {
    if (cx > 0) {
        int n = slen(lines[cy]);
        for (int k = cx - 1; k < n; k++) lines[cy][k] = lines[cy][k + 1];
        cx--;
    } else if (cy > 0) {
        int pn = slen(lines[cy - 1]);
        int cn = slen(lines[cy]);
        for (int k = 0; k < cn && pn + k < MAXC; k++) lines[cy - 1][pn + k] = lines[cy][k];
        lines[cy - 1][pn + cn < MAXC ? pn + cn : MAXC] = 0;
        for (int r = cy; r < nlines - 1; r++) {
            for (int k = 0; k <= MAXC; k++) lines[r][k] = lines[r + 1][k];
        }
        nlines--;
        cy--;
        cx = pn;
    }
}

static void newline(void) {
    if (nlines >= MAXL) return;
    int n = slen(lines[cy]);
    if (cx > n) cx = n;
    for (int r = nlines; r > cy + 1; r--) {
        for (int k = 0; k <= MAXC; k++) lines[r][k] = lines[r - 1][k];
    }
    nlines++;
    int j = 0;
    for (int k = cx; k <= n; k++) lines[cy + 1][j++] = lines[cy][k];
    lines[cy][cx] = 0;
    cy++;
    cx = 0;
}

int _start(void) {
    const char* a = ms_arg();
    int i = 0; while (a[i] && i < 158) { fpath[i] = a[i]; i++; } fpath[i] = 0;
    if (fpath[0] == 0) { fpath[0] = '?'; fpath[1] = 0; }

    win_w = 460; win_h = 320;
    win_id = ms_window("Text Editor", 120, 80, win_w, win_h);
    if (win_id < 0) ms_exit(1);

    static char filebuf[8000];
    nlines = 1; lines[0][0] = 0;
    int loaded = 0;
    if (a[0]) {
        // НЕ полагаемся на ms_fs_size: для MSFS он возвращает -1.
        // Просто пробуем прочитать.
        int n = ms_fs_read(a, (unsigned char*)filebuf, sizeof(filebuf) - 1);
        if (n > 0) { filebuf[n] = 0; load_text(filebuf, n); loaded = 1; }
    }
    if (loaded) set_status("F2 / Ctrl+S = save");
    else set_status("New file.  F2 / Ctrl+S = save");

    render();
    ms_yield();

    while (!ms_key_avail()) {
        ms_yield();
        ms_sleep(20);
    }
    int running = 1;
    int dirty = 1;
    while (running) {
        if (ms_key_avail()) {
            int ch = ms_key_get();
            if (ch == 27) { running = 0; }
            else if (ch == 0x02) {
                static char out[8000];
                int n = build_text(out, sizeof(out));
                if (n > 0) {
                    int ok = ms_fs_write(a, out);
                    set_status(ok ? "Saved!" : "Save FAILED");
                } else set_status("Nothing to save");
                dirty = 1;
            }
            else if (ch == 0x01) {
                if (ms_key_avail()) {
                    int c2 = ms_key_get();
                    if (c2 == 's' || c2 == 'S') {
                        static char out[8000];
                        int n = build_text(out, sizeof(out));
                        if (n > 0) {
                            int ok = ms_fs_write(a, out);
                            set_status(ok ? "Saved!" : "Save FAILED");
                        } else set_status("Nothing to save");
                    }
                }
                dirty = 1;
            }
            else if (ch == '\b') { backspace(); dirty = 1; }
            else if (ch == '\n' || ch == '\r') { newline(); dirty = 1; }
            else if ((ch & 0xFF) == 0xC8) { if (cy > 0) cy--; if (cx > slen(lines[cy])) cx = slen(lines[cy]); dirty = 1; }
            else if ((ch & 0xFF) == 0xD0) { if (cy < nlines - 1) cy++; if (cx > slen(lines[cy])) cx = slen(lines[cy]); dirty = 1; }
            else if ((ch & 0xFF) == 0xCB) { if (cx > 0) cx--; dirty = 1; }
            else if ((ch & 0xFF) == 0xCD) { if (cx < slen(lines[cy])) cx++; dirty = 1; }
            else if (ch >= 32 && ch < 127) { insert_char((char)ch); dirty = 1; }
        }
        if (dirty) { render(); dirty = 0; }
        ms_yield();
        ms_sleep(20);
    }
    ms_window_close(win_id);
    ms_exit(0);
}
