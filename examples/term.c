// term.c - SYSPR: терминал как ELF-программа (ring3 API).
// Своя текстовая сетка, ввод с эхом, команды: help, ls, cat, run, ver, clear.
//
// API без api->: ms_window(), ms_fill(), ms_text(), ms_yield(), ms_key_avail(),
// ms_run(), ms_exit(). Внутри — int 0x80 (см. include/ms/*.h).
#include "../include/mysticos.h"

#define CW 8
#define CH 8
#define MAX_COLS 128
#define MAX_ROWS 64

static int win_id = -1, win_w = 0, win_h = 0;
static int cols = 0, rows = 0;
static int cx = 0, cy = 0;
static char buf[MAX_ROWS][MAX_COLS];

static const unsigned int FG = 0xC0FFC0, BG = 0x101018;

static void clear_buf(void) {
    for (int r = 0; r < MAX_ROWS; r++) for (int c = 0; c < MAX_COLS; c++) buf[r][c] = ' ';
    cx = cy = 0;
}

static void putc_(char c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\r') { cx = 0; }
    else if (c == '\b') { if (cx > 0) { cx--; buf[cy][cx] = ' '; } }
    else { if (cx < cols) buf[cy][cx] = c; cx++; }
    if (cx >= cols) { cx = 0; cy++; }
    if (cy >= rows) {
        for (int r = 1; r < rows; r++) for (int c = 0; c < cols; c++) buf[r-1][c] = buf[r][c];
        for (int c = 0; c < cols; c++) buf[rows-1][c] = ' ';
        cy = rows - 1;
    }
}
static void prints(const char* s) { while (*s) putc_(*s++); }
static void printi(int v) {
    if (v == 0) { putc_('0'); return; }
    char b[12]; int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    if (neg) b[i++] = '-';
    while (i) putc_(b[--i]);
}

static void render(void) {
    ms_fill(win_id, 0, 0, win_w, win_h, BG);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            char s[2]; s[0] = buf[r][c]; s[1] = 0;
            ms_text_bg(win_id, c * CW, r * CH, s, FG, BG);
        }
}

static void draw_cursor(int on) {
    // Блок-курсор на позиции ввода.
    if (on) ms_fill(win_id, cx * CW, cy * CH, CW, CH, FG);
    else {
        char s[2]; s[0] = (cy < rows && cx < cols) ? buf[cy][cx] : ' '; s[1] = 0;
        ms_text_bg(win_id, cx * CW, cy * CH, s, FG, BG);
    }
}

// --- команды ---
static char cmd[128];
static int cmd_n = 0;

static int streq(const char* a, const char* b) { while (*a && *b && *a == *b) { a++; b++; } return *a == 0 && *b == 0; }

static void prompt(void) { prints("[/]> "); cmd_n = 0; cmd[0] = 0; }

static void cmd_exec(void) {
    putc_('\n');
    if (streq(cmd, "help")) {
        prints("commands: help, ls, cat <f>, run <f>, ver, clear\n");
        prints("  mofsf.msfs -D1   - format D1 as MSFS\n");
        prints("  mofsf.fat -16 -D1 - format D1 as FAT16\n");
    } else if (streq(cmd, "ls")) {
        static char names[64 * 13]; static int isd[64]; static unsigned int sz[64];
        int n = ms_fs_list("/", names, isd, sz, 64);
        for (int i = 0; i < n; i++) { prints(names + i * 13); prints("\n"); }
    } else if (cmd[0]=='c' && cmd[1]=='a' && cmd[2]=='t' && cmd[3]==' ') {
        static unsigned char fb[8192];
        int n = ms_fs_read(cmd + 4, fb, sizeof(fb));
        if (n > 0) { for (int i = 0; i < n; i++) putc_(fb[i]); prints("\n"); }
        else prints("cat: cannot read\n");
    } else if (cmd[0]=='m'&&cmd[1]=='o'&&cmd[2]=='f'&&cmd[3]=='s'&&cmd[4]=='f'&&cmd[5]=='.') {
        // mofsf.<fs> [-<ver>] -D<n>
        char fsname[8]; int fp = 0, i = 6;
        while (cmd[i] && cmd[i] != ' ' && fp < 7) fsname[fp++] = cmd[i++];
        fsname[fp] = 0;
        int drv = 0, ver = 16;
        for (int k = i; cmd[k]; k++) {
            if (cmd[k]=='-' && (cmd[k+1]=='D'||cmd[k+1]=='d') && cmd[k+2]>='0' && cmd[k+2]<='9') drv = cmd[k+2]-'0';
            else if (cmd[k]=='-' && cmd[k+1]>='0' && cmd[k+1]<='9') { ver = 0; int k2 = k+1; while (cmd[k2]>='0'&&cmd[k2]<='9') { ver = ver*10 + (cmd[k2]-'0'); k2++; } }
        }
        int is_msfs = (fsname[0]=='m'&&fsname[1]=='s'&&fsname[2]=='f'&&fsname[3]=='s'&&fsname[4]==0);
        int is_fat  = (fsname[0]=='f'&&fsname[1]=='a'&&fsname[2]=='t'&&fsname[3]==0);
        if (is_msfs || is_fat) {
            prints("formatting D"); printi(drv); prints("...\n");
            int ok = ms_format(drv, is_msfs ? 1 : 0, ver);
            prints(ok ? "done\n" : "FAILED\n");
        } else {
            prints("usage: mofsf.msfs -D1 | mofsf.fat -16 -D1\n");
        }
    } else if (cmd[0]=='r' && cmd[1]=='u' && cmd[2]=='n' && cmd[3]==' ') {
        prints("running...\n");
        int rc = ms_run(cmd + 4);
        prints("[exit "); printi(rc); prints("]\n");
    } else if (streq(cmd, "ver")) {
        prints("MysticOS 2 graphics (SYSPR term, ring3)\n");
    } else if (streq(cmd, "clear")) {
        clear_buf();
    } else if (cmd_n > 0) {
        prints("unknown command: "); prints(cmd); prints("\n");
    }
    prompt();
}

int _start(void) {
    win_w = 320; win_h = 200;
    win_id = ms_window("Terminal", 80, 80, win_w, win_h);
    if (win_id < 0) ms_exit(1);

    cols = win_w / CW; if (cols > MAX_COLS) cols = MAX_COLS;
    rows = win_h / CH; if (rows > MAX_ROWS) rows = MAX_ROWS;
    clear_buf();
    prints("MysticOS 2 terminal (SYSPR, ring3)\nType 'help' for commands.\n\n");
    prompt();

    int cur_on = 0; unsigned int cur_tick = ms_ticks();
    render(); draw_cursor(cur_on);

    while (1) {
        // Мигание курсора независимо от ввода.
        unsigned int now = ms_ticks();
        if (now - cur_tick >= 50) { cur_tick = now; cur_on = !cur_on; }

        while (ms_key_avail()) {
            char c = (char)ms_key_get();
            if (c == '\n' || c == '\r') { cmd_exec(); }
            else if (c == '\b') { if (cmd_n > 0) { cmd_n--; putc_('\b'); } }
            else if (c >= ' ' && cmd_n < 120) { cmd[cmd_n++] = c; cmd[cmd_n] = 0; putc_(c); }
        }

        render();
        draw_cursor(cur_on);
        ms_yield();
        ms_sleep(30);
    }
    ms_exit(0);
}
