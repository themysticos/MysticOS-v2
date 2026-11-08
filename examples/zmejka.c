// zmejka.c - "Змейка" для MysticOS 2. Финальная версия.
// Управление: WASD / стрелки, SPACE - пауза, R - рестарт, ESC - выход.
//
// Факты из ядра (pic.c, kernel.c):
//   ms_key_event() = (pressed<<8) | scancode_set1; extended = 0x80|sc
//   ms_ticks()     = тики по 10 мс (PIT 100 Гц)
//   ms_sleep()     = блокирующий hlt-цикл -> в играх НЕ использовать
#include "../include/mysticos.h"
#include "../include/ms/ui.h"

#define COLS      24
#define ROWS      18
#define CELL      16
#define PAD        6
#define HUD_H     18
#define WIN_W     (COLS * CELL + PAD * 2)
#define WIN_H     (ROWS * CELL + PAD * 2 + HUD_H)

// scancode set 1
#define SC_ESC    0x01
#define SC_W      0x11
#define SC_R      0x13
#define SC_A      0x1E
#define SC_S      0x1F
#define SC_D      0x20
#define SC_SPACE  0x39
// extended (pic.c: kb_push_event(pressed, 0x80|code))
#define SC_UP     (0x80 | 0x48)
#define SC_DOWN   (0x80 | 0x50)
#define SC_LEFT   (0x80 | 0x4B)
#define SC_RIGHT  (0x80 | 0x4D)

// ---- LCG ----
static unsigned int rng_state = 0x12345678u;
static unsigned int rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

// ---- змейка ----
#define MAXLEN (COLS * ROWS)
static signed char sx[MAXLEN], sy[MAXLEN];
static int head, tail, len;
static int dir, next_dir;          // 0=up 1=right 2=down 3=left
static int food_x, food_y;
static int score, best;
static int alive, paused;
static unsigned int tick_ticks, acc_ticks;

// ---- безопасный спавн еды: не у стены, не на змейке, не под носом ----
static void spawn_food(void) {
    int xmin = 1, xmax = COLS - 2;
    int ymin = 1, ymax = ROWS - 2;
    int span_x = xmax - xmin + 1;
    int span_y = ymax - ymin + 1;
    if (span_x <= 0 || span_y <= 0) { food_x = -1; food_y = -1; return; }

    // клетка "под носом" (2 шага вперёд от головы)
    int tx = sx[head], ty = sy[head];
    if (dir == 0) ty -= 2;
    if (dir == 1) tx += 2;
    if (dir == 2) ty += 2;
    if (dir == 3) tx -= 2;

    for (int tries = 0; tries < 800; tries++) {
        int fx = xmin + (int)(rnd() % (unsigned)span_x);
        int fy = ymin + (int)(rnd() % (unsigned)span_y);

        int busy = 0;
        for (int i = 0; i < len; i++) {
            int idx = (tail + i) % MAXLEN;
            if (sx[idx] == fx && sy[idx] == fy) { busy = 1; break; }
        }
        if (busy) continue;
        if (fx == tx && fy == ty) continue;

        food_x = fx; food_y = fy;
        return;
    }
    food_x = -1; food_y = -1;
}

static void reset_game(void) {
    len = 3; head = 2; tail = 0;
    sx[0] = 5; sy[0] = ROWS / 2;
    sx[1] = 6; sy[1] = ROWS / 2;
    sx[2] = 7; sy[2] = ROWS / 2;
    dir = 1; next_dir = 1;
    score = 0; alive = 1; paused = 0;
    tick_ticks = 10;      // 10 * 10 мс = 100 мс на шаг = 10 шагов/сек
    acc_ticks = 0;
    spawn_food();
}

static int cell_px(int c) { return PAD + c * CELL; }
static int cell_py(int r) { return PAD + HUD_H + r * CELL; }

static void draw(int id) {
    ms_clear(id, 0x14161E);

    // HUD
    ms_fill(id, 0, 0, WIN_W, HUD_H, 0x1E1F2A);
    ms_fill(id, 0, HUD_H - 1, WIN_W, 1, 0x3A3C50);

    char buf[40]; char nb[12]; int p;
    p = 0;
    const char* s = "SCORE ";
    for (int i = 0; s[i]; i++) buf[p++] = s[i];
    ui_int_str(score, nb);
    for (int i = 0; nb[i]; i++) buf[p++] = nb[i];
    buf[p] = 0;
    ms_text(id, 6, 5, buf, 0xE8E8F0);

    p = 0;
    const char* b = "BEST ";
    for (int i = 0; b[i]; i++) buf[p++] = b[i];
    ui_int_str(best, nb);
    for (int i = 0; nb[i]; i++) buf[p++] = nb[i];
    buf[p] = 0;
    ms_text(id, WIN_W - ui_text_w(buf) - 6, 5, buf, 0x9092A0);

    // еда
    if (food_x >= 0) {
        int fx = cell_px(food_x), fy = cell_py(food_y);
        ms_fill(id, fx + 2, fy + 2, CELL - 4, CELL - 4, 0xE04040);
        ms_fill(id, fx + 3, fy + 3, CELL - 9, CELL - 9, 0xFF8080);
    }

    // змейка
    for (int i = 0; i < len; i++) {
        int idx = (tail + i) % MAXLEN;
        int px = cell_px(sx[idx]), py = cell_py(sy[idx]);
        unsigned int col;
        if (i == len - 1)      col = 0xA0FFA0;
        else if (i == 0)       col = 0x2A6A2A;
        else                   col = 0x50C050;
        ms_fill(id, px + 1, py + 1, CELL - 2, CELL - 2, col);
        if (i == len - 1) {
            int ex = px + CELL / 2, ey = py + CELL / 2;
            if (dir == 0) ey -= 3;
            if (dir == 2) ey += 3;
            if (dir == 1) ex += 3;
            if (dir == 3) ex -= 3;
            ms_fill(id, ex - 1, ey - 1, 2, 2, 0x102010);
        }
    }

    // оверлеи
    if (!alive) {
        ms_fill(id, 0, PAD + HUD_H, WIN_W, ROWS * CELL, 0x101018);
        int cx = WIN_W / 2;
        int cy = PAD + HUD_H + ROWS * CELL / 2;
        ms_text_scaled(id, cx - 4 * 8 * 2, cy - 24, "GAME OVER", 0xFF7070, 2);
        ms_text(id, cx - (13 * 8) / 2, cy + 4,  "R - restart", 0xE8E8F0);
        ms_text(id, cx - (11 * 8) / 2, cy + 20, "ESC - exit",  0x9092A0);
    } else if (paused) {
        int cx = WIN_W / 2;
        int cy = PAD + HUD_H + ROWS * CELL / 2;
        ms_text_scaled(id, cx - 3 * 8 * 2, cy - 8, "PAUSE", 0xFFD060, 2);
    }
}

static void step(void) {
    dir = next_dir;
    int nx = sx[head], ny = sy[head];
    if (dir == 0) ny--;
    if (dir == 1) nx++;
    if (dir == 2) ny++;
    if (dir == 3) nx--;

    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        alive = 0; if (score > best) best = score; return;
    }
    for (int i = 0; i < len; i++) {
        int idx = (tail + i) % MAXLEN;
        if (i == 0 && len > 1) continue;
        if (sx[idx] == nx && sy[idx] == ny) {
            alive = 0; if (score > best) best = score; return;
        }
    }

    if (nx == food_x && ny == food_y) {
        head = (head + 1) % MAXLEN;
        sx[head] = (signed char)nx; sy[head] = (signed char)ny;
        len++; score += 10;
        unsigned int want = 10u - (unsigned int)(score / 50);
        tick_ticks = want < 4u ? 4u : want;   // пол 40 мс
        spawn_food();
    } else {
        head = (head + 1) % MAXLEN;
        sx[head] = (signed char)nx; sy[head] = (signed char)ny;
        tail = (tail + 1) % MAXLEN;
    }
}

int _start(void) {
    int id = ms_window("ZMEYKA (WASD)", 120, 80, WIN_W, WIN_H);
    if (id < 0) ms_exit(1);

    reset_game();
    unsigned int last = ms_ticks();
    int dirty = 1;

    for (;;) {
        // ---- клавиши: scancode set 1, без is_key-костылей ----
        int ev;
        while ((ev = ms_key_event()) >= 0) {
            int pressed = (ev >> 8) & 1;
            int code    = ev & 0xFF;
            if (!pressed) continue;

            if (code == SC_ESC) { ms_window_close(id); ms_exit(0); }

            if (!alive) {
                if (code == SC_R) { reset_game(); dirty = 1; }
                continue;
            }
            if (code == SC_SPACE) { paused = !paused; dirty = 1; continue; }
            if (paused) continue;

            if ((code == SC_W) || (code == SC_UP))    { if (dir != 2) next_dir = 0; }
            if ((code == SC_D) || (code == SC_RIGHT)) { if (dir != 3) next_dir = 1; }
            if ((code == SC_S) || (code == SC_DOWN))  { if (dir != 0) next_dir = 2; }
            if ((code == SC_A) || (code == SC_LEFT))  { if (dir != 1) next_dir = 3; }
        }

        // ---- время: тики по 10 мс ----
        unsigned int now = ms_ticks();
        unsigned int dt = now - last;
        last = now;
        if (alive && !paused) {
            acc_ticks += dt;
            while (acc_ticks >= tick_ticks) {
                acc_ticks -= tick_ticks;
                if (alive) { step(); dirty = 1; }
            }
        }

        if (dirty) { draw(id); dirty = 0; }
        ms_yield();   // единственная синхронизация кадра
    }
    return 0;
}