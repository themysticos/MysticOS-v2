// browser.c - SYSPR: браузер (HTTP + минимальный HTML + ссылки). Новый API.
// Адресная строка: наберите URL, Enter — загрузить. Клик по ссылке — переход.
// Esc — выход. Стрелки вверх/вниз — прокрутка.
#include "../include/mysticos.h"
#include "../include/ms/ui.h"
#include "minijs.h"

// ms_debug в ELF не линкуется (libc/posix.c не входит). Для отладки —
// определи BR_DEBUG и собери с libc; иначе no-op.
// Отладка в serial через SYS_DEBUG (есть в ядре). Включается BR_DEBUG.
#ifndef BR_DEBUG
#define brlog(s) ((void)0)
#else
static inline void brlog(const char* s) { __ms_sc1(SYS_DEBUG, (unsigned int)s); }
#endif

#define MAXL 4000
#define MAXC 100
#define MAXLINKS 256
#define URLMAX 240

static char lines[MAXL][MAXC + 1];
static unsigned char line_align[MAXL];   // 0=left 1=center 2=right (из align=)
static unsigned char line_scale[MAXL];   // 1=обычный текст, 2/3=заголовки h2/h1
static int cur_scale = 1;                // текущий масштаб (h1..h3)
static int line_h[MAXL];                 // высота строки в пикселях (8*scale)
static int total_h = 0;                  // суммарная высота всех строк
static int scroll_y = 0;                 // вертикальная прокрутка в ПИКСЕЛЯХ

static int  nlines = 0;
static int  cur_align = 0;               // текущее выравнивание (обновляется тегами)
static int  top = 0;

static int row_px(int lr) { int s = line_scale[lr]; if (s < 1) s = 1; return 8 * s; }

// Экранный Y (в пикселях контента, от content_y) -> номер строки (-1).
static int line_at_sy(int sy) {
    int py = 0;
    for (int lr = 0; lr < nlines; lr++) {
        int rh = row_px(lr);
        if (sy >= py && sy < py + rh) return lr;
        py += rh;
        if (py > sy) return lr;
    }
    return -1;
}
static int cum_h_before(int idx) {
    int y = 0; for (int i = 0; i < idx && i < nlines; i++) y += row_px(i); return y;
}

static char url[URLMAX + 1];
static int  url_len = 0;

static struct {
    int line;
    int col;
    int len;
    char url[URLMAX + 1];
} links[MAXLINKS];
static int nlinks = 0;

// Картинки: <img src>. Храним URL + строку, где показывать, + размеры.
#define MAXIMGS 64
static struct {
    int line;          // номер строки, где нарисовать
    int w, h;          // желаемые размеры (из width/height или 0)
    char url[URLMAX + 1];
} imgs[MAXIMGS];
static int nimgs = 0;

// Буфер одной картинки (грузим по одной) + флаг готовности.
#define IMG_BUF_SZ (256 * 1024)
static unsigned char img_data[IMG_BUF_SZ];   // временный буфер загрузки
// Кэш картинок в СТАТИЧЕСКОМ пуле программы (ms_mem_alloc в ring3 не мапится
// в page directory задачи -> #PF; поэтому только .bss).
#define IMG_POOL_SZ (1024 * 1024)
static unsigned char img_pool[IMG_POOL_SZ];
static int img_pool_used = 0;
static unsigned int img_off[MAXIMGS];   // смещение в img_pool
static int          img_sz[MAXIMGS];
static int          img_ok[MAXIMGS];

static int win_id = -1, win_w = 0, win_h = 0;
static int win_cx = 0, win_cy = 0;   // позиция клиентской области в экране

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static void add_line(void) {
    if (nlines < MAXL) {
        lines[nlines][0] = 0;
        line_align[nlines] = (unsigned char)cur_align;
        line_scale[nlines] = (unsigned char)cur_scale;
        line_h[nlines] = 8 * (cur_scale < 1 ? 1 : cur_scale);
        nlines++;
    }
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
    // Схему берём из БАЗОВОГО url (http или https), а не хардкодим http.
    int base_https = ci_eq(base, "https://", 8);
    const char* h = base_https ? "https://" : "http://";
    int hlen = base_https ? 8 : 7;
    if (ci_eq(rel, "http://", 7) || ci_eq(rel, "https://", 8)) {
        for (int i = 0; rel[i] && o < URLMAX; i++) out[o++] = rel[i];
        out[o] = 0; return;
    }
    if (ci_eq(rel, "mailto:", 7) || ci_eq(rel, "javascript:", 11) || rel[0] == '#') { out[0] = 0; return; }
    for (int i = 0; h[i] && o < URLMAX; i++) out[o++] = h[i];
    int b = hlen;
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
    nlines = 0; top = 0; nlinks = 0; nimgs = 0; cur_align = 0; cur_scale = 1; add_line();
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

            // <script>...</script>: выполнить мини-JS, вывод — в текст.
            if (!closing && nl == 6 && ci_eq(name,"script",6)) {
                const char* sp = t; while (*sp && *sp != '>') sp++; if (*sp=='>') sp++;
                const char* se = sp;
                while (*se && !(se[0]=='<' && se[1]=='/')) se++;
                static char jsbuf[8192]; int jl = (int)(se - sp);
                if (jl > 8191) jl = 8191;
                for (int i = 0; i < jl; i++) jsbuf[i] = sp[i];
                jsbuf[jl] = 0;
                static char jsout[4096];
                minijs_run(jsbuf, jsout, sizeof(jsout));
                // Вывод скрипта — с разбором простых тегов:
                // <br>/<p> -> новая строка, прочие <...> пропускаем,
                // &amp;/&lt;/&gt;/&nbsp; раскрываем. Иначе <br> виден как текст.
                const char* o = jsout;
                add_line(); col = 0;
                while (*o) {
                    if (*o == '<') {
                        const char* e = o + 1;
                        int br = 0, blk = 0;
                        if ((e[0]=='b'||e[0]=='B') && (e[1]=='r'||e[1]=='R')) br = 1;
                        else if (e[0]=='p'||e[0]=='P'||e[0]=='d'||e[0]=='D') blk = 1;
                        while (*o && *o != '>') o++;
                        if (*o == '>') o++;
                        if (br || blk) { add_line(); col = 0; }
                        continue;
                    }
                    char c = *o;
                    if (c == '&') {
                        if (o[1]=='a'&&o[2]=='m'&&o[3]=='p'&&o[4]==';') { c='&'; o+=5; }
                        else if (o[1]=='l'&&o[2]=='t'&&o[3]==';') { c='<'; o+=4; }
                        else if (o[1]=='g'&&o[2]=='t'&&o[3]==';') { c='>'; o+=4; }
                        else if (o[1]=='n'&&o[2]=='b'&&o[3]=='s'&&o[4]=='p'&&o[5]==';') { c=' '; o+=6; }
                        else { o++; continue; }
                    } else o++;
                    if (c == '\n') { add_line(); col = 0; continue; }
                    if (c == '\r') continue;
                    if (nlines == 0) add_line();
                    if (col < MAXC) { lines[nlines-1][col++] = c; lines[nlines-1][col] = 0; }
                    else { add_line(); col = 0; lines[nlines-1][col++] = c; lines[nlines-1][col] = 0; }
                }
                // пропустить до </script>
                t = se;
                while (*t && *t != '>') t++;
                if (*t == '>') t++;
                continue;
            }
            // <img src="...">: запомнить URL картинки и строку для отрисовки.
            if (!closing && nl == 3 && ci_eq(name,"img",3)) {
                const char* q = t; char isrc[URLMAX + 1]; isrc[0] = 0;
                while (*q && *q != '>') {
                    if ((q[0]=='s'||q[0]=='S') && (q[1]=='r'||q[1]=='R') &&
                        (q[2]=='c'||q[2]=='C') && q[3]=='=') {
                        q += 4; char qc = 0;
                        if (*q=='"'||*q=='\'') { qc = *q; q++; }
                        int hi = 0;
                        while (*q && hi < URLMAX) {
                            if (qc) { if (*q == qc) break; }
                            else { if (*q==' '||*q=='>') break; }
                            isrc[hi++] = *q++;
                        }
                        isrc[hi] = 0; break;
                    }
                    q++;
                }
                brlog("[br] found <img>\n");
                if (isrc[0] && nimgs < MAXIMGS) {
                    resolve_url(url, isrc, imgs[nimgs].url);
                    if (imgs[nimgs].url[0]) {
                        imgs[nimgs].line = nlines;   // на текущей строке
                        imgs[nimgs].w = 0; imgs[nimgs].h = 0;
                        nimgs++;
                        add_line();                  // картинка занимает свою строку
                    }
                }
            }
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
            else if (nl == 6 && ci_eq(name,"center",6)) {
                // <center>...</center>: выравнивание по центру на блок.
                cur_align = closing ? 0 : 1;
                add_line(); col = 0; if (in_a) { a_line = nlines - 1; a_col = col; }
            }
            else if (nl == 2 && (name[0]=='h'||name[0]=='H') && name[1]>='1' && name[1]<='3') {
                // <h1>/<h2>/<h3> — крупный заголовок. h1=3x, h2=2x, h3=2x.
                if (closing) cur_scale = 1;
                else cur_scale = (name[1]=='1') ? 3 : 2;
                add_line(); col = 0; if (in_a) { a_line = nlines - 1; a_col = col; }
            }
            else if (is_block_tag(name, nl)) {
                // Разбор align=left|center|right у div/p/h*.
                if (!closing) {
                    const char* q = t;
                    while (*q && *q != '>') {
                        if ((q[0]=='a'||q[0]=='A')&&(q[1]=='l'||q[1]=='L')&&(q[2]=='i'||q[2]=='I')&&(q[3]=='g'||q[3]=='G')&&(q[4]=='n'||q[4]=='N')&&q[5]=='=') {
                            q += 6; char qc = 0;
                            if (*q=='"'||*q=='\'') { qc = *q; q++; }
                            if ((q[0]=='c'||q[0]=='C')&&(q[1]=='e'||q[1]=='E')) cur_align = 1;
                            else if ((q[0]=='r'||q[0]=='R')&&(q[1]=='i'||q[1]=='I')) cur_align = 2;
                            else cur_align = 0;
                            break;
                        }
                        q++;
                    }
                } else {
                    cur_align = 0;   // закрытие блока -> сброс
                }
                add_line(); col = 0; if (in_a) { a_line = nlines - 1; a_col = col; }
            }

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
    // Отладка: перечислить найденные ссылки (line/col/len/url).
    for (int i = 0; i < nlinks && i < 30; i++) {
        char dbg[128]; int dp = 0;
        const char* p1 = "[br] L";
        for (int k = 0; p1[k]; k++) dbg[dp++] = p1[k];
        char nb[12]; ui_int_str(i, nb);
        for (int k = 0; nb[k]; k++) dbg[dp++] = nb[k];
        const char* p2 = " ln=";
        for (int k = 0; p2[k]; k++) dbg[dp++] = p2[k];
        ui_int_str(links[i].line, nb);
        for (int k = 0; nb[k]; k++) dbg[dp++] = nb[k];
        const char* p3 = " col=";
        for (int k = 0; p3[k]; k++) dbg[dp++] = p3[k];
        ui_int_str(links[i].col, nb);
        for (int k = 0; nb[k]; k++) dbg[dp++] = nb[k];
        const char* p4 = " len=";
        for (int k = 0; p4[k]; k++) dbg[dp++] = p4[k];
        ui_int_str(links[i].len, nb);
        for (int k = 0; nb[k]; k++) dbg[dp++] = nb[k];
        dbg[dp++] = ' ';
        for (int k = 0; links[i].url[k] && dp < 120; k++) dbg[dp++] = links[i].url[k];
        dbg[dp++] = '\n'; dbg[dp] = 0;
        brlog(dbg);
    }
    // Пересчитать суммарную высоту и сбросить прокрутку.
    total_h = 0;
    for (int i = 0; i < nlines; i++) total_h += row_px(i);
    scroll_y = 0;
}

static int link_at(int line, int col) {
    for (int i = 0; i < nlinks; i++) {
        if (links[i].line != line) continue;
        if (col >= links[i].col && col < links[i].col + links[i].len) return i;
    }
    return -1;
}

// ---- layout-константы ----
#define TOOLBAR_H   28
#define STATUSBAR_H 14
#define PAD_X       6

// Геометрия: актуализировать win_w/win_h из ядра (maximize/resize).
static void sync_geom(void) {
    int vc = ms_window_client(win_id);
    int w = (vc >> 16) & 0xFFFF;
    int h = vc & 0xFFFF;
    if (w > 0) win_w = w;
    if (h > 0) win_h = h;
    int vp = ms_window_pos(win_id);
    win_cx = (vp >> 16) & 0xFFFF;
    win_cy = vp & 0xFFFF;
}

// Внутренние кнопки тулбара (рисуются и обрабатываются в render/click).
#define BTN_BACK_X  PAD_X
#define BTN_REFR_X  (PAD_X + 24)
#define BTN_Y       3
#define BTN_W       22
#define BTN_H       22
#define URLBAR_X    (PAD_X + 52)

static void render(void) {
    sync_geom();
    int content_y = TOOLBAR_H;
    int content_h = win_h - TOOLBAR_H - STATUSBAR_H;
    // Прокрутка в ПИКСЕЛЯХ (строки разной высоты: h1/h2/p).
    if (scroll_y > total_h - content_h) scroll_y = total_h - content_h;
    if (scroll_y < 0) scroll_y = 0;
    if (total_h <= content_h) scroll_y = 0;

    // Фон.
    ms_clear(win_id, UI_BG);

    // ---- Тулбар ----
    ms_fill(win_id, 0, 0, win_w, TOOLBAR_H, UI_PANEL);
    ms_fill(win_id, 0, TOOLBAR_H - 1, win_w, 1, UI_BORDER);
    // Кнопки (иконки), состояние hover не знаем здесь — нейтрально.
    ms_fill(win_id, BTN_BACK_X, BTN_Y, BTN_W, BTN_H, UI_PANEL2);
    ms_rect(win_id, BTN_BACK_X, BTN_Y, BTN_W, BTN_H, UI_BORDER);
    ui_icon(win_id, BTN_BACK_X + 3, BTN_Y + 3, UI_IC_BACK, UI_TEXT);
    ms_fill(win_id, BTN_REFR_X, BTN_Y, BTN_W, BTN_H, UI_PANEL2);
    ms_rect(win_id, BTN_REFR_X, BTN_Y, BTN_W, BTN_H, UI_BORDER);
    ui_icon(win_id, BTN_REFR_X + 3, BTN_Y + 3, UI_IC_REFRESH, UI_TEXT);
    // Поле URL.
    int ux = URLBAR_X, uw = win_w - URLBAR_X - PAD_X;
    if (uw > 0) {
        ms_fill(win_id, ux, 4, uw, TOOLBAR_H - 8, UI_BG);
        ms_rect(win_id, ux, 4, uw, TOOLBAR_H - 8, UI_ACCENT);
        const char* show = url[0] ? url : "http://";
        ms_text(win_id, ux + 5, 4 + (TOOLBAR_H - 8 - 8) / 2, show,
                url[0] ? UI_TEXT : UI_DIM);
    }

    // ---- Контент ----
    // Позиция мыши в клиентских координатах (для hover).
    int hmx = ms_mouse_x() - win_cx;
    int hmy = ms_mouse_y() - win_cy;
    int hlink = -1;
    if (hmx >= PAD_X && hmy >= content_y && hmx < win_w && hmy < win_h - STATUSBAR_H) {
        int hlr = line_at_sy(hmy - content_y + scroll_y);
        if (hlr >= 0) hlink = link_at(hlr, (hmx - PAD_X) / 8);
    }
    // Пиксельная прокрутка: проходим строки, считая их Y, рисуем видимые.
    int py = 0;               // Y строки в «документе» (пиксели)
    int first_vis = -1;
    for (int lr = 0; lr < nlines; lr++) {
        int rh = row_px(lr);
        int sy = py - scroll_y;             // Y на экране
        py += rh;
        if (sy + rh <= content_y) continue;
        if (sy >= content_y + content_h) break;
        if (first_vis < 0) first_vis = lr;
        int sc = line_scale[lr]; if (sc < 1) sc = 1;
        int y = content_y + sy;
        int lw = slen(lines[lr]) * 8 * sc;
        int avail = win_w - PAD_X * 2 - 8;
        int tx = PAD_X;
        if (line_align[lr] == 1) tx = PAD_X + (avail - lw) / 2;
        else if (line_align[lr] == 2) tx = PAD_X + (avail - lw);
        if (tx < PAD_X) tx = PAD_X;
        if (sc > 1) ms_text_scaled(win_id, tx, y, lines[lr], UI_TEXT, sc);
        else ms_text(win_id, tx, y, lines[lr], UI_TEXT);
        // Ссылки этой строки (только для обычного масштаба — h1-ссылки редки).
        if (sc == 1) {
            for (int i = 0; i < nlinks; i++) {
                if (links[i].line != lr) continue;
                char sub[MAXC + 1]; int k = 0;
                for (int c = links[i].col; c < links[i].col + links[i].len && c < MAXC && k < MAXC; c++) sub[k++] = lines[lr][c];
                sub[k] = 0;
                int hovered = (i == hlink);
                int uw = links[i].len * 8;
                int ux = PAD_X + links[i].col * 8;
                if (hovered) ms_fill(win_id, ux, y, uw, 8, ui_blend(UI_BG, UI_ACCENT, 60));
                unsigned int lc = hovered ? 0xFFFFFF : UI_ACCENT_HI;
                ms_text(win_id, ux, y, sub, lc);
                ms_fill(win_id, ux, y + 8, uw, 1, hovered ? 0xFFFFFF : UI_ACCENT);
                if (hovered) ms_fill(win_id, ux, y + 9, uw, 1, 0xFFFFFF);
            }
        }
    }
    (void)first_vis;

    // ---- Картинки (<img>): пока рамка-плейсхолдер с URL ----
    // (полную загрузку/декод добавим отдельным шагом).
    for (int i = 0; i < nimgs; i++) {
        int docy = cum_h_before(imgs[i].line);   // Y в документе
        int y = content_y + docy - scroll_y;
        if (y + 8 < content_y || y > content_y + content_h) continue;
        int bx = PAD_X, bw = win_w - PAD_X * 2 - 8, bh = 48;
        if (i < MAXIMGS && img_ok[i]) {
            // Реальная картинка из памяти (BMP/GIF/PNG), на всю ширину.
            int r = ms_image_mem(win_id, bx, y, 0, 0, (const void*)(img_pool + img_off[i]), (unsigned int)img_sz[i]);
            brlog(r ? "[br] img drawn\n" : "[br] img decode FAIL\n");
            continue;
        }
        ms_fill(win_id, bx, y, bw, bh, UI_PANEL2);
        ms_rect(win_id, bx, y, bw, bh, UI_ACCENT);
        char lbl[MAXC + 1]; int k = 0;
        const char* pfx = "[img] ";
        for (int j = 0; pfx[j] && k < MAXC; j++) lbl[k++] = pfx[j];
        for (int j = 0; imgs[i].url[j] && k < MAXC; j++) lbl[k++] = imgs[i].url[j];
        lbl[k] = 0;
        ms_text(win_id, bx + 4, y + 4, lbl, UI_ACCENT_HI);
    }

    // ---- Скроллбар (если есть что скроллить) ----
    if (total_h > content_h) {
        int sb_x = win_w - 6, sb_y = content_y, sb_h = content_h;
        int sb_rows = total_h / 8; if (sb_rows < 1) sb_rows = 1;
        int sb_top  = scroll_y / 8;
        int sb_view = content_h / 8; if (sb_view < 1) sb_view = 1;
        ui_scrollbar(win_id, sb_x, sb_y, 6, sb_h, sb_top, sb_rows, sb_view, -1, -1, 0, 0);
    }

    // ---- Статусбар ----
    int sy = win_h - STATUSBAR_H;
    ms_fill(win_id, 0, sy, win_w, STATUSBAR_H, UI_PANEL);
    ms_fill(win_id, 0, sy, win_w, 1, UI_BORDER);
    char info[64]; int ip = 0;
    const char* pfx = "lines ";
    for (int i = 0; pfx[i]; i++) info[ip++] = pfx[i];
    char nb[12]; ui_int_str(nlines, nb);
    for (int i = 0; nb[i]; i++) info[ip++] = nb[i];
    const char* mid = "  links ";
    for (int i = 0; mid[i]; i++) info[ip++] = mid[i];
    char lb[12]; ui_int_str(nlinks, lb);
    for (int i = 0; lb[i]; i++) info[ip++] = lb[i];
    info[ip] = 0;
    ms_text(win_id, PAD_X, sy + 3, info, UI_DIM);
}

// Загрузить картинку по URL в img_data (асинхронно, как страница).
// Возвращает 1 при успехе, 0 при ошибке. Данные — сырые (BMP/GIF/PNG).
static int load_image(const char* iurl) {
    if (ms_http_start(iurl) != 0) return 0;
    int done = 0;
    for (int spin = 0; spin < 800; spin++) {
        ms_yield();
        int st = ms_http_poll();
        if (st == 1) { done = 1; break; }
        if (st < 0) { ms_sleep(10); continue; }
        ms_sleep(10);
    }
    return done;   // успех ТОЛЬКО если ответ полностью принят (state==2)
}

static void set_url(const char* u) {
    int i = 0; while (u[i] && i < URLMAX) { url[i] = u[i]; i++; } url[i] = 0; url_len = i;
}

static void show_error(const char* msg) {
    nlines = 0; nlinks = 0; add_line();
    for (int i = 0; msg[i] && i < MAXC; i++) lines[0][i] = msg[i];
    lines[0][slen(msg) < MAXC ? slen(msg) : MAXC] = 0;
    render();
}

// Одна попытка загрузки с ТАЙМАУТОМ (а не «ошибка при первом -1»).
// Возвращает 1 при успехе, 0 при неудаче (можно ретраить).
static int load_once(void) {
    if (ms_http_start(url) != 0) return 0;
    // Ждём до ~8 сек (800 итераций * 10 мс). Показываем «Loading...».
    for (int spin = 0; spin < 800; spin++) {
        sync_geom();
        int ly = TOOLBAR_H + 8;
        ms_fill(win_id, PAD_X, ly, 120, 14, UI_PANEL2);
        ms_rect(win_id, PAD_X, ly, 120, 14, UI_ACCENT);
        ms_text(win_id, PAD_X + 6, ly + 3, "Loading...", UI_ACCENT_HI);
        ms_yield();
        int st = ms_http_poll();
        if (st == 1) return 1;
        if (st < 0) {
            // Сервер ещё может ответить — подождём и попробуем poll снова.
            ms_sleep(10);
            continue;
        }
        ms_sleep(10);
    }
    return 0;
}

static void load_url(void) {
    if (url_len == 0) return;
    // До 3 попыток: сеть может лагать, сервер — тормозить.
    int ok = 0;
    for (int attempt = 0; attempt < 3 && !ok; attempt++) {
        if (attempt > 0) { ms_sleep(300); render(); }
        ok = load_once();
    }
    if (!ok) { show_error("[error] could not load (timeout/network)"); return; }
    static char page[32768];
    int n = ms_http_result(page, sizeof(page));
    if (n <= 0) { show_error("[error] no data"); return; }
    parse_html(page);
    brlog("[br] parse done\n");
    img_pool_used = 0;   // новая страница — сброс пула картинок
    // Подгрузить картинки страницы (по одной, в отдельные буферы).
    for (int i = 0; i < nimgs; i++) {
        if (i >= MAXIMGS) break;
        img_off[i] = 0; img_sz[i] = 0; img_ok[i] = 0;
        // Показываем, что грузим.
        render(); ms_yield();
        if (!load_image(imgs[i].url)) { brlog("[br] img load fail\n"); continue; }
        int sz = ms_http_result_raw(img_data, IMG_BUF_SZ);
        brlog("[br] img got\n");
        if (sz <= 0) { brlog("[br] img empty\n"); continue; }
        if (img_pool_used + sz > IMG_POOL_SZ) break;   // пул кончился
        for (int k = 0; k < sz; k++) img_pool[img_pool_used + k] = img_data[k];
        img_off[i] = (unsigned int)img_pool_used;
        img_sz[i] = sz; img_ok[i] = 1;
        img_pool_used += sz;
    }
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

    while (1) {
        // Забрать события мыши и позицию окна (event-driven API).
        ui_begin(win_id);

        if (ms_key_avail()) {
            int ch = ms_key_get();
            if (ch == 27) break;
            else if (ch == '\n' || ch == '\r') load_url();
            else if (ch == '\b') { if (url_len > 0) { url_len--; url[url_len] = 0; render(); } }
            else if (ch >= 32 && ch < 127) { if (url_len < URLMAX - 1) { url[url_len++] = (char)ch; url[url_len] = 0; render(); } }
            else if ((ch & 0xFF) == 0xC8) { scroll_y -= 8; if (scroll_y < 0) scroll_y = 0; render(); }
            else if ((ch & 0xFF) == 0xD0) { scroll_y += 8; render(); }
        }

        // Обработка кликов через ui_clicked (координаты уже клиентские).
        if (ui_event_count() > 0) {
            sync_geom();
            int rx = 0, ry = 0;
            // Берём первое DOWN-событие для определения клика.
            const ui_event_t* ev0 = ui_event_at(0);
            if (ev0) { rx = ev0->x; ry = ev0->y; }
            int had_click = 0;
            for (int i = 0; i < ui_event_count(); i++) {
                const ui_event_t* e = ui_event_at(i);
                if (e && e->type == UI_EV_DOWN && e->button == 1) { had_click = 1; rx = e->x; ry = e->y; break; }
            }
            if (had_click) {
                { char dbg[64]; int dp = 0;
                  const char* p1 = "[br] click rx=";
                  for (int i = 0; p1[i]; i++) dbg[dp++] = p1[i];
                  char nb[12]; ui_int_str(rx, nb);
                  for (int i = 0; nb[i]; i++) dbg[dp++] = nb[i];
                  const char* p2 = " ry=";
                  for (int i = 0; p2[i]; i++) dbg[dp++] = p2[i];
                  ui_int_str(ry, nb);
                  for (int i = 0; nb[i]; i++) dbg[dp++] = nb[i];
                  dbg[dp++] = '\n'; dbg[dp] = 0;
                  brlog(dbg);
                }
                // Кнопки тулбара.
                if (rx >= BTN_BACK_X && rx < BTN_BACK_X + BTN_W && ry >= BTN_Y && ry < BTN_Y + BTN_H) {
                    if (url_len > 0) load_url();
                } else if (rx >= BTN_REFR_X && rx < BTN_REFR_X + BTN_W && ry >= BTN_Y && ry < BTN_Y + BTN_H) {
                    if (url_len > 0) load_url();
                }
                // Скроллбар.
                else if (nlines > (win_h - TOOLBAR_H - STATUSBAR_H) / 8 &&
                         rx >= win_w - 8 && rx < win_w && ry >= TOOLBAR_H &&
                         ry < win_h - STATUSBAR_H) {
                    int sb_rows = total_h / 8; if (sb_rows < 1) sb_rows = 1;
                    int sb_view = (win_h - TOOLBAR_H - STATUSBAR_H) / 8; if (sb_view < 1) sb_view = 1;
                    int nv = ui_scrollbar(win_id, win_w - 6, TOOLBAR_H,
                                          6, win_h - TOOLBAR_H - STATUSBAR_H,
                                          scroll_y / 8, sb_rows, sb_view, rx, ry, 1, 0);
                    scroll_y = nv * 8;
                    render();
                }
                // Ссылки в контенте.
                else if (rx >= PAD_X && ry >= TOOLBAR_H && rx < win_w &&
                         ry < win_h - STATUSBAR_H) {
                    int ccol = (rx - PAD_X) / 8;
                    int line = line_at_sy(ry - TOOLBAR_H + scroll_y);
                    int li = (line >= 0) ? link_at(line, ccol) : -1;
                    { char dbg[96]; int dp = 0;
                      const char* p1 = "[br] link line=";
                      for (int i = 0; p1[i]; i++) dbg[dp++] = p1[i];
                      char nb[12]; ui_int_str(line, nb);
                      for (int i = 0; nb[i]; i++) dbg[dp++] = nb[i];
                      const char* p2 = " col=";
                      for (int i = 0; p2[i]; i++) dbg[dp++] = p2[i];
                      ui_int_str(ccol, nb);
                      for (int i = 0; nb[i]; i++) dbg[dp++] = nb[i];
                      const char* p3 = " -> li=";
                      for (int i = 0; p3[i]; i++) dbg[dp++] = p3[i];
                      ui_int_str(li, nb);
                      for (int i = 0; nb[i]; i++) dbg[dp++] = nb[i];
                      if (li >= 0) {
                        const char* p4 = " url=";
                        for (int i = 0; p4[i]; i++) dbg[dp++] = p4[i];
                        for (int i = 0; links[li].url[i] && dp < 90; i++) dbg[dp++] = links[li].url[i];
                      }
                      dbg[dp++] = '\n'; dbg[dp] = 0;
                      brlog(dbg);
                    }
                    if (li >= 0 && links[li].url[0]) {
                        set_url(links[li].url);
                        load_url();
                    }
                }
            }
        }
        // Старый блок (не используется — оставлен для совместимости).
        if (0) {
            // Абсолютные координаты мыши -> координаты клиента окна.
            int vp = ms_window_pos(win_id);
            int cx = (vp >> 16) & 0xFFFF;
            int cy = vp & 0xFFFF;
            int rx = ms_mouse_x() - cx;
            int ry = ms_mouse_y() - cy;
            sync_geom();

            // Кнопки тулбара.
            if (rx >= BTN_BACK_X && rx < BTN_BACK_X + BTN_W &&
                ry >= BTN_Y && ry < BTN_Y + BTN_H) {
                // Назад — пока просто перезагрузить (история не ведётся).
                if (url_len > 0) load_url();
            } else if (rx >= BTN_REFR_X && rx < BTN_REFR_X + BTN_W &&
                       ry >= BTN_Y && ry < BTN_Y + BTN_H) {
                if (url_len > 0) load_url();
            }
            // Скроллбар.
            else if (nlines > (win_h - TOOLBAR_H - STATUSBAR_H) / 8 &&
                     rx >= win_w - 8 && rx < win_w && ry >= TOOLBAR_H &&
                     ry < win_h - STATUSBAR_H) {
                int rows = (win_h - TOOLBAR_H - STATUSBAR_H) / 8;
                int nv = ui_scrollbar(win_id, win_w - 6, TOOLBAR_H,
                                      6, win_h - TOOLBAR_H - STATUSBAR_H,
                                      top, nlines, rows, rx, ry, 1, 0);
                top = nv;
                render();
            }
            // Ссылки в контенте.
            else if (rx >= PAD_X && ry >= TOOLBAR_H && rx < win_w &&
                     ry < win_h - STATUSBAR_H) {
                int row = (ry - TOOLBAR_H) / 8;
                int ccol = (rx - PAD_X) / 8;
                int line = top + row;
                int li = link_at(line, ccol);
                if (li >= 0 && links[li].url[0]) {
                    set_url(links[li].url);
                    load_url();
                }
            }
        }
        ms_yield();
        ms_sleep(20);
    }
    ms_window_close(win_id);
    ms_exit(0);
}
