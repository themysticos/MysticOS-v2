// editor.c - SYSPR: простой текстовый редактор как ELF-программа (API v2).
// Открывает файл через get_arg(). F2 (0x02) или Ctrl+S — сохранить.
//
// Модель: массив строк фиксированной длины. Вставка/удаление — сдвигом.
#include "../api.h"

#define MAXL 200      // строк
#define MAXC 78       // символов в строке (видимых)

static char lines[MAXL][MAXC + 2];
static int  nlines = 0;

static int win_id = -1, win_w = 0, win_h = 0;
static int cx = 0, cy = 0;        // курсор (колонка, строка)
static int top = 0;               // первая видимая строка
static char fpath[160];
static char status[80];

static const mystic_api_t* g;

static void set_status(const char* s) {
    int i = 0; while (s[i] && i < 78) { status[i] = s[i]; i++; } status[i] = 0;
}

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

// Разобрать содержимое файла на строки.
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

// Собрать все строки в один буфер (для сохранения). Возвращает длину.
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
    g->window_fill(win_id, 0, 0, win_w, win_h, bg);
    g->window_text(win_id, 4, 2, fpath, 0x80C0FF, bg);
    // Область текста: ниже шапки, выше статуса.
    int ty = 18;
    int rows = (win_h - ty - 14) / 8;
    if (rows < 1) rows = 1;
    // Прокрутка: держим курсор в видимой области.
    if (cy < top) top = cy;
    if (cy >= top + rows) top = cy - rows + 1;
    for (int r = 0; r < rows; r++) {
        int lr = top + r;
        if (lr >= nlines) break;
        g->window_text(win_id, 2, ty + r * 8, lines[lr], fg, bg);
    }
    // Курсор — блок (инверсия): рисуем поверх, заливая знакоместо.
    int scr = cy - top;
    if (scr >= 0 && scr < rows) {
        int px = 2 + cx * 8;
        int py = ty + scr * 8;
        g->window_rect(win_id, px, py, 8, 8, 0x80FF80);
    }
    // Строка статуса внизу.
    g->window_text(win_id, 2, win_h - 12, status, 0xFFE060, bg);
}

// Вставить символ в позицию курсора (со сдвигом вправо).
static void insert_char(char ch) {
    int n = slen(lines[cy]);
    if (n >= MAXC) return;
    if (cx > n) cx = n;
    for (int k = n; k >= cx; k--) lines[cy][k + 1] = lines[cy][k];
    lines[cy][cx] = ch;
    cx++;
}

// Backspace: удалить символ слева (или склеить со предыдущей строкой).
static void backspace(void) {
    if (cx > 0) {
        int n = slen(lines[cy]);
        for (int k = cx - 1; k < n; k++) lines[cy][k] = lines[cy][k + 1];
        cx--;
    } else if (cy > 0) {
        int pn = slen(lines[cy - 1]);
        int cn = slen(lines[cy]);
        // Склеиваем: к строке cy-1 дописываем строку cy.
        for (int k = 0; k < cn && pn + k < MAXC; k++) lines[cy - 1][pn + k] = lines[cy][k];
        lines[cy - 1][pn + cn < MAXC ? pn + cn : MAXC] = 0;
        // Удаляем строку cy (сдвиг вверх).
        for (int r = cy; r < nlines - 1; r++) {
            for (int k = 0; k <= MAXC; k++) lines[r][k] = lines[r + 1][k];
        }
        nlines--;
        cy--;
        cx = pn;
    }
}

// Enter: разбить строку в позиции курсора.
static void newline(void) {
    if (nlines >= MAXL) return;
    int n = slen(lines[cy]);
    if (cx > n) cx = n;
    // Сдвинуть строки вниз, освободив место после cy.
    for (int r = nlines; r > cy + 1; r--) {
        for (int k = 0; k <= MAXC; k++) lines[r][k] = lines[r - 1][k];
    }
    nlines++;
    // Хвост строки cy -> в новую строку cy+1.
    int j = 0;
    for (int k = cx; k <= n; k++) lines[cy + 1][j++] = lines[cy][k];
    lines[cy][cx] = 0;
    cy++;
    cx = 0;
}

int _start(const mystic_api_t* api) {
    g = api;
    const char* a = (api->get_arg ? api->get_arg() : "");
    int i = 0; while (a[i] && i < 158) { fpath[i] = a[i]; i++; } fpath[i] = 0;
    if (fpath[0] == 0) { fpath[0] = '?'; fpath[1] = 0; }

    win_w = 460; win_h = 320;
    win_id = api->window_create("Text Editor", 120, 80, win_w, win_h);
    if (win_id < 0) return 1;

    // Загрузить файл (если есть).
    static char filebuf[8000];
    nlines = 1; lines[0][0] = 0;
    if (a[0] && api->fs_size(a) > 0) {
        int n = api->fs_read(a, (unsigned char*)filebuf, sizeof(filebuf) - 1);
        if (n > 0) { filebuf[n] = 0; load_text(filebuf, n); }
        set_status("F2 / Ctrl+S = save");
    } else {
        set_status("New file.  F2 / Ctrl+S = save");
    }

    render();
    api->yield();

    while (!api->key_available()) {
        api->yield();
        api->sleep(20);
    }
    // Основной цикл ввода.
    int running = 1;
    int dirty = 1;
    while (running) {
        if (api->key_available()) {
            int ch = api->key_getchar();
            if (ch == 27) { running = 0; }                  // Esc — выход
            else if (ch == 0x02) {                          // F2 — сохранить
                static char out[8000];
                int n = build_text(out, sizeof(out));
                if (api->fs_write && n > 0) {
                    int ok = api->fs_write(a, out);
                    set_status(ok ? "Saved!" : "Save FAILED");
                } else {
                    set_status("Nothing to save");
                }
                dirty = 1;
            }
            else if (ch == 0x01) {                          // Ctrl — ждём 's'
                if (api->key_available()) {
                    int c2 = api->key_getchar();
                    if (c2 == 's' || c2 == 'S') {
                        static char out[8000];
                        int n = build_text(out, sizeof(out));
                        if (api->fs_write && n > 0) {
                            int ok = api->fs_write(a, out);
                            set_status(ok ? "Saved!" : "Save FAILED");
                        } else set_status("Nothing to save");
                    }
                }
                dirty = 1;
            }
            else if (ch == '\b') { backspace(); dirty = 1; }
            else if (ch == '\n' || ch == '\r') { newline(); dirty = 1; }
            else if ((ch & 0xFF) == 0xC8) { if (cy > 0) cy--; if (cx > slen(lines[cy])) cx = slen(lines[cy]); dirty = 1; }  // up
            else if ((ch & 0xFF) == 0xD0) { if (cy < nlines - 1) cy++; if (cx > slen(lines[cy])) cx = slen(lines[cy]); dirty = 1; }  // down
            else if ((ch & 0xFF) == 0xCB) { if (cx > 0) cx--; dirty = 1; }   // left
            else if ((ch & 0xFF) == 0xCD) { if (cx < slen(lines[cy])) cx++; dirty = 1; }  // right
            else if (ch >= 32 && ch < 127) { insert_char((char)ch); dirty = 1; }
        }
        if (dirty) { render(); dirty = 0; }
        api->yield();
        api->sleep(20);
    }
    api->window_close(win_id);
    return 0;
}
