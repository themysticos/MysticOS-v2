// browser.c - SYSPR: браузер (HTTP + минимальный HTML + ссылки). Новый API.
// Адресная строка: наберите URL, Enter — загрузить. Клик по ссылке — переход.
// Esc — выход. Стрелки вверх/вниз — прокрутка.
#include "../include/mysticos.h"

#define MAXL 4000
#define MAXC 100
#define MAXLINKS 256
#define URLMAX 240

static char lines[MAXL][MAXC + 1];
static int  nlines = 0;
static int  top = 0;

static char url[URLMAX + 1];
static int  url_len = 0;

static struct {
    int line;
    int col;
    int len;
    char url[URLMAX + 1];
} links[MAXLINKS];
static int nlinks = 0;

static int win_id = -1, win_w = 0, win_h = 0;

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static void add_line(void) {
    if (nlines < MAXL) { lines[nlines][0] = 0; nlines++; }
}

static int is_block_tag(const char* t, int len) {
    static const char* blk[] = {
        "br","p","div","li","tr","h1","h2","h3","h4","h5","h6",
        "table","ul","ol","hr","title","body","head","meta","html"
    };
    for (unsigned i = 0; i < sizeof(blk)/sizeof(blk[0]); i++) {
        const char* n = blk[i];
        int nl = slen(n);
        if (nl != len) continue;
        int ok = 1;
        for (int k = 0; k < len; k++) { char c = t[k]; if (c>='A'&&c<='Z') c+=32; if (c!=n[k]) { ok=0; break; } }
        if (ok) return 1;
    }
    return 0;
}

static int ci_eq(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        char x = a[i]; if (x>='A'&&x<='Z') x+=32;
        char y = b[i]; if (y>='A'&&y<='Z') y+=32;
        if (x != y) return 0;
    }
    return 1;
}

static void resolve_url(const char* base, const char* rel, char* out) {
    int o = 0;
    const char* h = "http://";
    if (ci_eq(rel, "http://", 7)) {
        for (int i = 0; rel[i] && o < URLMAX; i++) out[o++] = rel[i];
        out[o] = 0; return;
    }
    if (ci_eq(rel, "mailto:", 7) || ci_eq(rel, "javascript:", 11) || rel[0] == '#') { out[0] = 0; return; }
    for (int i = 0; h[i] && o < URLMAX; i++) out[o++] = h[i];
    int b = 0; if (ci_eq(base, "http://", 7)) b = 7;
    while (base[b] && base[b] != '/') { if (o < URLMAX) out[o++] = base[b]; b++; }
    if (rel[0] == '/') {
        for (int i = 0; rel[i] && o < URLMAX; i++) out[o++] = rel[i];
    } else {
        int last_slash = b;
        for (int i = b; base[i]; i++) if (base[i] == '/') last_slash = i;
        int start = (last_slash > b) ? b : last_slash;
        for (int i = start; i < last_slash; i++) { if (o < URLMAX) out[o++] = base[i]; }
        if (o == 0 || out[o-1] != '/') { if (o < URLMAX) out[o++] = '/'; }
        for (int i = 0; rel[i] && o < URLMAX; i++) out[o++] = rel[i];
    }
    out[o] = 0;
}

static void parse_html(const char* html) {
    nlines = 0; top = 0; nlinks = 0; add_line();
    int col = 0;
    const char* t = html;
    int in_a = 0; int a_line = 0, a_col = 0; char a_href[URLMAX + 1]; a_href[0] = 0;

    while (*t) {
        if (*t == '<') {
            const char* p = t + 1;
            int closing = 0;
            if (*p == '/') { closing = 1; p++; }
            const char* name = p;
            int nl = 0;
            while (p[nl] && p[nl] != '>' && p[nl] != ' ' && p[nl] != '/' && p[nl] != '\r' && p[nl] != '\n') nl++;

            if (!closing && nl == 1 && (name[0]=='a'||name[0]=='A')) {
                const char* q = t;
                a_href[0] = 0;
                while (*q && *q != '>') {
                    if ((q[0]=='h'||q[0]=='H') && (q[1]=='r'||q[1]=='R') &&
                        (q[2]=='e'||q[2]=='E') && (q[3]=='f'||q[3]=='F') && q[4]=='=') {
                        q += 5;
                        char qc = 0;
                        if (*q=='"'||*q=='\'') { qc = *q; q++; }
                        int hi = 0;
                        while (*q && hi < URLMAX) {
                            if (qc) { if (*q == qc) break; }
                            else { if (*q==' '||*q=='>') break; }
                            a_href[hi++] = *q++;
                        }
                        a_href[hi] = 0;
                        break;
                    }
                    q++;
                }
                in_a = 1; a_line = nlines - 1; a_col = col;
            }
            else if (closing && nl == 1 && (name[0]=='a'||name[0]=='A')) {
                if (in_a && a_href[0] && nlinks < MAXLINKS) {
                    resolve_url(url, a_href, links[nlinks].url);
                    if (links[nlinks].url[0]) {
                        links[nlinks].line = a_line;
                        links[nlinks].col = a_col;
                        links[nlinks].len = col - a_col;
                        if (links[nlinks].len < 0) links[nlinks].len = 0;
                        nlinks++;
                    }
                }
                in_a = 0;
            }
            else if (is_block_tag(name, nl)) { add_line(); col = 0; if (in_a) { a_line = nlines - 1; a_col = col; } }

            while (*t && *t != '>') t++;
            if (*t == '>') t++;
            continue;
        }
        char c = *t;
        if (c == '\r') { t++; continue; }
        if (c == '\n') { add_line(); col = 0; if (in_a) { a_line = nlines - 1; a_col = col; } t++; continue; }
        if (c == '&') {
            if (t[1]=='a'&&t[2]=='m'&&t[3]=='p'&&t[4]==';') { c='&'; t+=5; }
            else if (t[1]=='l'&&t[2]=='t'&&t[3]==';') { c='<'; t+=4; }
            else if (t[1]=='g'&&t[2]=='t'&&t[3]==';') { c='>'; t+=4; }
            else if (t[1]=='q'&&t[2]=='u'&&t[3]=='o'&&t[4]=='t'&&t[5]==';') { c='"'; t+=6; }
            else if (t[1]=='n'&&t[2]=='b'&&t[3]=='s'&&t[4]=='p'&&t[5]==';') { c=' '; t+=6; }
            else if (t[1]=='#'&&t[2]=='3'&&t[3]=='9'&&t[4]==';') { c='\''; t+=5; }
            else { t++; }
        } else { t++; }
        if (nlines == 0) add_line();
        if (col < MAXC) { lines[nlines-1][col++] = c; lines[nlines-1][col] = 0; }
        else { add_line(); col = 0; if (in_a) { a_line = nlines - 1; a_col = col; } lines[nlines-1][col++] = c; lines[nlines-1][col] = 0; }
    }
    if (nlines == 0) add_line();
}

static int link_at(int line, int col) {
    for (int i = 0; i < nlinks; i++) {
        if (links[i].line != line) continue;
        if (col >= links[i].col && col < links[i].col + links[i].len) return i;
    }
    return -1;
}

static void render(void) {
    ms_clear(win_id, MS_WHITE);
    ms_fill(win_id, 0, 0, win_w, 16, MS_LIGHT_GRAY);
    ms_text(win_id, 4, 4, url[0] ? url : "http://", MS_BLUE);
    int ty = 20;
    int rows = (win_h - ty) / 8;
    if (top > nlines - rows) top = nlines - rows;
    if (top < 0) top = 0;
    for (int r = 0; r < rows; r++) {
        int lr = top + r;
        if (lr >= nlines) break;
        int y = ty + r * 8;
        ms_text(win_id, 2, y, lines[lr], MS_BLACK);
        for (int i = 0; i < nlinks; i++) {
            if (links[i].line != lr) continue;
            char sub[MAXC + 1]; int k = 0;
            for (int c = links[i].col; c < links[i].col + links[i].len && c < MAXC && k < MAXC; c++) sub[k++] = lines[lr][c];
            sub[k] = 0;
            ms_text(win_id, 2 + links[i].col * 8, y, sub, MS_BLUE);
        }
    }
}

static void set_url(const char* u) {
    int i = 0; while (u[i] && i < URLMAX) { url[i] = u[i]; i++; } url[i] = 0; url_len = i;
}

static void load_url(void) {
    if (url_len == 0) return;
    if (ms_http_start(url) != 0) {
        nlines = 0; nlinks = 0; add_line();
        const char* e = "[error] could not start (only http://)";
        for (int i = 0; e[i] && i < MAXC; i++) lines[0][i] = e[i];
        lines[0][slen(e) < MAXC ? slen(e) : MAXC] = 0;
        render();
        return;
    }
    // Показываем "Loading...", но НЕ блокируем ОС: крутим свой цикл.
    for (;;) {
        ms_fill(win_id, 0, 16, win_w, 10, MS_LIGHT_GRAY);
        ms_text(win_id, 4, 17, "Loading...", MS_DARK_GRAY);
        ms_yield();
        int st = ms_http_poll();
        if (st == 1) break;
        if (st < 0) {
            nlines = 0; nlinks = 0; add_line();
            const char* e = "[error] could not load (only http://)";
            for (int i = 0; e[i] && i < MAXC; i++) lines[0][i] = e[i];
            lines[0][slen(e) < MAXC ? slen(e) : MAXC] = 0;
            render();
            return;
        }
        ms_sleep(5);
    }
    static char page[32768];
    int n = ms_http_result(page, sizeof(page));
    if (n < 0) {
        nlines = 0; nlinks = 0; add_line();
        const char* e = "[error] no data";
        for (int i = 0; e[i] && i < MAXC; i++) lines[0][i] = e[i];
        lines[0][slen(e) < MAXC ? slen(e) : MAXC] = 0;
        render();
        return;
    }
    parse_html(page);
    render();
}

int _start(void) {
    win_w = 560; win_h = 400;
    win_id = ms_window("Browser", 100, 60, win_w, win_h);
    if (win_id < 0) ms_exit(1);
    url[0] = 0; url_len = 0;
    nlines = 0; add_line();
    render();
    ms_yield();

    int prev_left = 0;

    while (1) {
        if (ms_key_avail()) {
            int ch = ms_key_get();
            if (ch == 27) break;
            else if (ch == '\n' || ch == '\r') load_url();
            else if (ch == '\b') { if (url_len > 0) { url_len--; url[url_len] = 0; render(); } }
            else if (ch >= 32 && ch < 127) { if (url_len < URLMAX - 1) { url[url_len++] = (char)ch; url[url_len] = 0; render(); } }
            else if ((ch & 0xFF) == 0xC8) { if (top > 0) { top--; render(); } }
            else if ((ch & 0xFF) == 0xD0) { top++; render(); }
        }
        int left = ms_mouse_left();
        if (left && !prev_left) {
            int mx = ms_mouse_x(), my = ms_mouse_y();
            int rx = mx - 100 - 1;
            int ry = my - 60 - 22;
            if (rx >= 0 && ry >= 20 && rx < win_w) {
                int row = (ry - 20) / 8;
                int ccol = (rx - 2) / 8;
                int line = top + row;
                int li = link_at(line, ccol);
                if (li >= 0 && links[li].url[0]) {
                    set_url(links[li].url);
                    load_url();
                }
            }
        }
        prev_left = left;
        ms_yield();
        ms_sleep(20);
    }
    ms_window_close(win_id);
    ms_exit(0);
}
