// term.c - SYSPR: терминал как ELF-программа (API v2).
// Своя текстовая сетка, ввод с эхом, команды: help, ls, cat, run, ver, clear.
#include "../api.h"

#define CW 8
#define CH 8
#define MAX_COLS 128
#define MAX_ROWS 64
#define ROW_H 16

static const mystic_api_t* g;

static int win_id = -1, win_w = 0, win_h = 0, win_x = 0, win_y = 0;
static int cols = 0, rows = 0;
static int cx = 0, cy = 0;
static char buf[MAX_ROWS][MAX_COLS];

static unsigned int FG = 0xC0FFC0, BG = 0x101018;

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
    unsigned int bg = BG;
    g->window_fill(win_id, 0, 0, win_w, win_h, bg);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            char s[2]; s[0] = buf[r][c]; s[1] = 0;
            g->window_text(win_id, c * CW, r * CH, s, FG, bg);
        }
}

static void draw_cursor(int on) {
    // Блок-курсор на позиции ввода.
    if (on) g->window_fill(win_id, cx * CW, cy * CH, CW, CH, FG);
    else {
        char s[2]; s[0] = (cy < rows && cx < cols) ? buf[cy][cx] : ' '; s[1] = 0;
        g->window_text(win_id, cx * CW, cy * CH, s, FG, BG);
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
        char names[64 * 13]; int isd[64]; unsigned int sz[64];
        int n = g->fs_list("/", names, isd, sz, 64);
        for (int i = 0; i < n; i++) { prints(names + i * 13); prints("\n"); }
    } else if (cmd[0]=='c' && cmd[1]=='a' && cmd[2]=='t' && cmd[3]==' ') {
        static unsigned char fb[8192];
        int n = g->fs_read(cmd + 4, fb, sizeof(fb));
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
        if ((is_msfs || is_fat) && g->format) {
            prints("formatting D"); printi(drv); prints("...\n");
            int ok = g->format(drv, is_msfs ? 1 : 0, ver);
            prints(ok ? "done\n" : "FAILED\n");
        } else {
            prints("usage: mofsf.msfs -D1 | mofsf.fat -16 -D1\n");
        }
    } else if (cmd[0]=='r' && cmd[1]=='u' && cmd[2]=='n' && cmd[3]==' ') {
        prints("running...\n");
        int rc = g->run_program(cmd + 4);
        prints("[exit "); printi(rc); prints("]\n");
    } else if (streq(cmd, "ver")) {
        prints("MysticOS 2 graphics (SYSPR term)\n");
    } else if (streq(cmd, "clear")) {
        clear_buf();
    } else if (cmd_n > 0) {
        prints("unknown command: "); prints(cmd); prints("\n");
    }
    prompt();
}

int _start(const mystic_api_t* api) {
    g = api;
    win_w = 320; win_h = 200; win_x = 80; win_y = 80;
    win_id = api->window_create("Terminal", win_x, win_y, win_w, win_h);
    if (win_id < 0) return 1;

    cols = win_w / CW; if (cols > MAX_COLS) cols = MAX_COLS;
    rows = win_h / CH; if (rows > MAX_ROWS) rows = MAX_ROWS;
    clear_buf();
    prints("MysticOS 2 terminal (SYSPR ELF)\nType 'help' for commands.\n\n");
    prompt();

    int cur_on = 0; unsigned int cur_tick = api->ticks();
    render(); draw_cursor(cur_on);

    while (1) {
        // Мигание курсора независимо от ввода.
        unsigned int now = api->ticks();
        if (now - cur_tick >= 50) { cur_tick = now; cur_on = !cur_on; }

        int did = 0;
        while (api->key_available()) {
            char c = (char)api->key_getchar();
            if (c == '\n' || c == '\r') { cmd_exec(); }
            else if (c == '\b') { if (cmd_n > 0) { cmd_n--; putc_('\b'); } }
            else if (c >= ' ' && cmd_n < 120) { cmd[cmd_n++] = c; cmd[cmd_n] = 0; putc_(c); }
            did = 1;
        }

        render();
        draw_cursor(cur_on);
        api->yield();
        (void)did;
        api->sleep(30);
    }
    return 0;
}
