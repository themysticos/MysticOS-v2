// music.c - SYSPR: WAV-плеер (СТРИМИНГ через кольцо музыки) для MysticOS 2.
//
// Читает .wav с диска ПОРЦИЯМИ (ms_file_*), ресемплит в 48000 Гц моно
// 16-бит и шлёт в непрерывное кольцо через ms_music_pcm (то же, что Doom).
// Не грузит весь файл в память.
#include "../include/mysticos.h"

#define WIN_W 460
#define WIN_H 300
#define WIN_X 140
#define WIN_Y 90
#define TITLE_H 20
#define LIST_W 200

static int win_id = -1;
static int prev_left = 0;

#define MAXF 24
static char fnames[MAXF][13];
static unsigned int fsizes[MAXF];
static int fcount = 0;
static int playing = -1;

// --- Состояние стриминга ---
#define OUT_RATE 48000u               // частота кольца (HDA MIX_RATE)
static int      s_fd = -1;
static unsigned int s_data_off = 0;
static unsigned int s_data_len = 0;
static unsigned int s_read_pos = 0;   // прочитано из файла (байт PCM)
static unsigned int s_rate = 22050;
static int      s_channels = 1;
static int      s_bits = 16;
// Дробная позиция чтения в исходных кадрах (16.16).
static unsigned int s_src_pos_fp = 0;

// Буфер прочитанных исходных сэмплов (16-бит моно, до ресемплинга).
#define RAWSAMPLES 4096
static short rawsmp[RAWSAMPLES];
static unsigned int raw_count = 0;    // сколько валидных сэмплов в rawsmp
static unsigned int raw_base = 0;     // индекс в rawsmp, откуда ещё не брали

// Выходной буфер 16-бит моно на 48000.
#define OUTSAMPLES 4096
static short outsmp[OUTSAMPLES];
// Недосланный остаток выходного буфера (опережающая буферизация).
static unsigned int out_pending = 0;
static unsigned int out_pending_len = 0;

// Диагностика: максимальный пик сэмпла (клиппинг виден при ~32767).
static int g_peak = 0;

// Диагностика через SYS_DEBUG напрямую (ms_debug не линкуется в примерах).
static void dbg_str(const char* s) {
    __ms_sc1(SYS_DEBUG, (unsigned int)s);
}
static void dbg_num(unsigned int v) {
    char b[12]; int i = 0;
    if (v == 0) { dbg_str("0"); return; }
    char t[12]; int n = 0;
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    while (n) b[i++] = t[--n];
    b[i] = 0;
    dbg_str(b);
}

static unsigned int rd_u32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1]<<8) |
           ((unsigned int)p[2]<<16) | ((unsigned int)p[3]<<24);
}
static unsigned short rd_u16(const unsigned char* p) {
    return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1]<<8));
}

// Абсолютные счётчики позиции в исходных кадрах (для непрерывности).
static unsigned int s_frames_read_abs = 0;   // сколько всего кадров прочитано
static unsigned int raw_block_start = 0;     // абс. индекс rawsmp[0]

// Прочитать следующий блок исходных сэмплов (16-бит МОНО).
// Возвращает число сэмплов (0 = конец файла).
static unsigned int read_raw_block(void) {
    if (s_fd < 0 || s_read_pos >= s_data_len) return 0;
    unsigned int frame_bytes = (s_bits/8) * s_channels;
    if (frame_bytes == 0) return 0;
    unsigned int want_bytes = RAWSAMPLES * frame_bytes;
    unsigned int left = s_data_len - s_read_pos;
    if (want_bytes > left) want_bytes = left;
    static unsigned char raw[RAWSAMPLES * 4];
    if (want_bytes > sizeof(raw)) want_bytes = sizeof(raw);
    int got = ms_file_read(s_fd, raw, want_bytes);
    if (got <= 0) return 0;
    s_read_pos += (unsigned int)got;
    unsigned int frames = (unsigned int)got / frame_bytes;
    if (frames > RAWSAMPLES) frames = RAWSAMPLES;
    for (unsigned int i = 0; i < frames; i++) {
        int s;
        if (s_channels == 1) {
            s = (short)rd_u16(raw + i*2);
        } else {
            short l = (short)rd_u16(raw + i*4);
            short r = (short)rd_u16(raw + i*4 + 2);
            s = ((int)l + (int)r) / 2;
        }
        rawsmp[i] = (short)s;
    }
    raw_block_start = s_frames_read_abs;
    s_frames_read_abs += frames;
    raw_count = frames;
    raw_base = 0;
    return frames;
}

// Абсолютная позиция выхода (в кадрах 48000). От неё считаем исходный кадр.
static unsigned long long out_abs = 0;

// Сгенерировать до `want` выходных сэмплов (48000) из rawsmp с линейной
// интерполяцией. Возвращает число сгенерированных (0 = конец).
// Позиция источника = out_abs * (s_rate/48000) — абсолютная, поэтому
// переходы между блоками непрерывны (нет щелчков/«плёнки»).
static unsigned int gen_out(unsigned int want) {
    if (want > OUTSAMPLES) want = OUTSAMPLES;
    unsigned int produced = 0;
    // ratio = s_rate/48000 в 16.16
    unsigned int ratio = ((unsigned int)s_rate << 16) / OUT_RATE;
    while (produced < want) {
        // Абсолютный исходный кадр (дробно, 16.16).
        unsigned long long src_abs_fp = out_abs * (unsigned long long)ratio;
        unsigned int src_idx = (unsigned int)(src_abs_fp >> 16);
        // Нужен ли этот кадр вне текущего блока?
        if (src_idx < raw_block_start || src_idx >= raw_block_start + raw_count) {
            // читаем блок, пока не покроем src_idx
            unsigned int n = read_raw_block();
            if (n < 2) {
                // конец: если src_idx за пределами — стоп
                if (src_idx >= raw_block_start + raw_count) return produced;
            }
            continue;   // пересчитать после чтения
        }
        unsigned int local = src_idx - raw_block_start;
        unsigned int frac = (unsigned int)(src_abs_fp & 0xFFFF);
        int a, b;
        if (local + 1 < raw_count) {
            a = rawsmp[local]; b = rawsmp[local + 1];
        } else {
            // на границе блока: интерполировать нельзя (нет следующего).
            // Прочитаем следующий блок, если он есть, иначе держим a.
            a = rawsmp[local]; b = a;
        }
        int v = a + (int)(((long)(b - a) * (int)frac) >> 16);
        v = (v * 3) / 4;   // -2.5 dB: запас под SFX
        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        // Трекинг пика (диагностика клиппинга).
        if (v > g_peak) g_peak = v;
        if (-v > g_peak) g_peak = -v;
        outsmp[produced++] = (short)v;
        out_abs++;
    }
    return produced;
}

static void stop_play(void) {
    if (s_fd >= 0) { ms_file_close(s_fd); s_fd = -1; }
    playing = -1;
    raw_count = 0; raw_base = 0; s_src_pos_fp = 0;
    out_pending = 0; out_pending_len = 0;
    out_abs = 0;
    s_frames_read_abs = 0; raw_block_start = 0;
}

static int wav_open(const char* path) {
    int fd = ms_file_open(path);
    if (fd < 0) return 0;
    unsigned char hdr[12];
    if (ms_file_read(fd, hdr, 12) != 12) { ms_file_close(fd); return 0; }
    if (hdr[0]!='R'||hdr[1]!='I'||hdr[2]!='F'||hdr[3]!='F') { ms_file_close(fd); return 0; }
    if (hdr[8]!='W'||hdr[9]!='A'||hdr[10]!='V'||hdr[11]!='E') { ms_file_close(fd); return 0; }
    unsigned short fmt = 0, ch = 0, bits = 0;
    unsigned int rate = 22050, data_off = 0, data_len = 0;
    unsigned int pos = 12;
    unsigned char chdr[8];
    while (ms_file_read(fd, chdr, 8) == 8) {
        unsigned int csize = rd_u32(chdr + 4);
        if (chdr[0]=='f'&&chdr[1]=='m'&&chdr[2]=='t'&&chdr[3]==' ') {
            unsigned char fbuf[16];
            unsigned int need = csize < 16 ? csize : 16;
            if (ms_file_read(fd, fbuf, need) != (int)need) break;
            fmt = rd_u16(fbuf);
            ch = rd_u16(fbuf + 2);
            rate = rd_u32(fbuf + 4);
            bits = rd_u16(fbuf + 14);
            if (csize > need) ms_file_seek(fd, pos + 8 + csize);
        } else if (chdr[0]=='d'&&chdr[1]=='a'&&chdr[2]=='t'&&chdr[3]=='a') {
            data_off = pos + 8;
            data_len = csize;
            break;
        } else {
            ms_file_seek(fd, pos + 8 + csize + (csize & 1));
        }
        pos += 8 + csize + (csize & 1);
    }
    if (fmt != 1 || bits != 16 || data_off == 0) { ms_file_close(fd); return 0; }
    s_fd = fd;
    s_data_off = data_off;
    s_data_len = data_len;
    s_read_pos = 0;
    s_rate = rate ? rate : 22050;
    s_channels = ch ? ch : 1;
    s_bits = bits;
    raw_count = 0; raw_base = 0; s_src_pos_fp = 0;
    out_abs = 0;
    s_frames_read_abs = 0; raw_block_start = 0;
    ms_file_seek(fd, data_off);
    return 1;
}

static void draw_list(void) {
    ms_fill(win_id, 0, 0, LIST_W, WIN_H, 0x202028);
    ms_text_bg(win_id, 4, 4, "WAV files:", 0xFFFFFF, 0x202028);
    for (int i = 0; i < fcount; i++) {
        int y = 20 + i * 16;
        unsigned int bg = (i == playing) ? 0x2E6A2E : 0x202028;
        ms_fill(win_id, 0, y - 1, LIST_W, 15, bg);
        ms_text(win_id, 4, y, fnames[i], (i == playing) ? 0xFFFFFF : 0xC0C0C8);
    }
}

static void draw_status(const char* line) {
    ms_fill(win_id, LIST_W, 0, WIN_W - LIST_W, WIN_H, 0x303040);
    ms_text(win_id, LIST_W + 8, 8, "Music Player", 0xFFFFFF);
    ms_text(win_id, LIST_W + 8, 30, line, 0xA0E0A0);
    if (playing >= 0) {
        ms_text(win_id, LIST_W + 8, 50, "Playing:", 0xC0C0C8);
        ms_text(win_id, LIST_W + 8, 62, fnames[playing], 0xFFFFFF);
        ms_rect(win_id, LIST_W + 8, 84, WIN_W - LIST_W - 16, 12, 0x808088);
        if (s_data_len > 0) {
            int pw = (int)((unsigned long)(WIN_W - LIST_W - 18) * s_read_pos / s_data_len);
            if (pw > 0) ms_fill(win_id, LIST_W + 9, 85, pw, 10, 0x4AE04A);
        }
    }
    ms_text(win_id, LIST_W + 8, WIN_H - 40, "Click a file to play.", 0x808090);
    ms_text(win_id, LIST_W + 8, WIN_H - 28, "S = stop   Esc = exit", 0x808090);
}

static void play_file(int idx) {
    stop_play();
    char path[32];
    path[0] = '/'; int k = 1;
    for (int i = 0; fnames[idx][i] && k < 30; i++) path[k++] = fnames[idx][i];
    path[k] = 0;
    if (wav_open(path)) {
        playing = idx;
        draw_list();
        draw_status("OK");
    } else {
        playing = -1;
        draw_list();
        draw_status("Error: not a 16-bit PCM WAV");
    }
}

int _start(void) {
    win_id = ms_window("Music Player", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win_id < 0) ms_exit(1);
    ms_clear(win_id, 0x303040);

    static char all_names[MAXF * 13];
    static int isdir[MAXF];
    static unsigned int szs[MAXF];
    int n = ms_fs_list("/", all_names, isdir, szs, MAXF);
    fcount = 0;
    if (n > 0) {
        for (int i = 0; i < n && fcount < MAXF; i++) {
            const char* nm = all_names + i * 13;
            int len = 0; while (nm[len]) len++;
            if (len < 5) continue;
            char e0 = nm[len-3], e1 = nm[len-2], e2 = nm[len-1];
            if ((e0|32) != 'w' || (e1|32) != 'a' || (e2|32) != 'v') continue;
            int k = 0; while (nm[k] && k < 12) { fnames[fcount][k] = nm[k]; k++; }
            fnames[fcount][k] = 0;
            fsizes[fcount] = szs[i];
            fcount++;
        }
    }

    draw_list();
    draw_status(fcount ? "Select a file" : "No .wav files found");
    ms_yield();

    while (1) {
        while (ms_key_avail()) {
            int ch = ms_key_get();
            if (ch == 27) { stop_play(); ms_exit(0); }
            else if (ch == 's' || ch == 'S') { stop_play(); draw_list(); draw_status("Stopped"); }
        }

        int mx = ms_mouse_x(), my = ms_mouse_y();
        int left = ms_mouse_left();
        int lx = mx - WIN_X;
        int ly = my - WIN_Y - TITLE_H;
        if (left && !prev_left) {
            if (lx >= 0 && lx < LIST_W && ly >= 19) {
                int idx = (ly - 19) / 16;
                if (idx >= 0 && idx < fcount) play_file(idx);
            }
        }
        prev_left = left;

        // --- Аудио: НАБИВАЕМ кольцо ПОЛНОСТЬЮ каждый цикл ---
        // (раньше слали по 2048 -> кольцо почти пусто -> underrun -> щелчки).
        if (playing >= 0) {
            int stuck = 0;
            for (int guard = 0; guard < 128 && !stuck; guard++) {
                // досылаем остаток
                while (out_pending < out_pending_len) {
                    int acc = ms_music_pcm(outsmp + out_pending, out_pending_len - out_pending);
                    if (acc <= 0) { stuck = 1; break; }   // кольцо полно
                    out_pending += (unsigned int)acc;
                }
                if (stuck) break;
                // генерируем следующий блок и шлём
                out_pending = 0; out_pending_len = 0;
                unsigned int produced = gen_out(4096);
                if (produced == 0) {
                    stop_play();
                    draw_list();
                    draw_status("Finished");
                    break;
                }
                out_pending_len = produced; out_pending = 0;
            }
            if (playing >= 0) draw_status("Playing");
            // ДИАГНОСТИКА раз в ~секунду: позиция в файле, сколько сэмплов
            // сгенерировано и отправлено в кольцо.
            {
                static unsigned int dbg_t = 0;
                unsigned int now = ms_ticks();
                if (now - dbg_t > 100) {
                    dbg_t = now;
                    dbg_str("[music] read="); dbg_num(s_read_pos);
                    dbg_str("/"); dbg_num(s_data_len);
                    dbg_str(" pend="); dbg_num(out_pending_len - out_pending);
                    dbg_str(" peak="); dbg_num((unsigned int)g_peak);
                    dbg_str("\n");
                    g_peak = 0;
                }
            }
        }

        ms_yield();
        // Сон только если кольцо полно (нечего слать). Иначе крутимся быстро.
        if (out_pending >= out_pending_len) ms_sleep(3);
    }
    return 0;
}
