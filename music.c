// music.c - приёмник музыки в ядре. Синтез (OPL3 + GENMIDI) делает Doom
// и шлёт готовый PCM через SYS_MUSIC_PCM. Здесь кольцевой буфер, из
// которого HDA-миксер берёт сэмплы (music_next_sample).
#include "music.h"
#include "pmem.h"

#define MRING 192000u       // ~4 сек при 48 кГц (моно, int16)
static short* g_mring = 0;
static volatile unsigned int g_mwr = 0, g_mrd = 0;
static int g_mplaying = 0;
// Флаг: музыка только что стартовала после паузы. HDA-миксер по нему
// сбрасывает свой отсчёт времени, чтобы не "доливать" кадры за тот
// интервал, когда музыки ещё не было (иначе ложный underrun на старте).
static volatile int g_music_just_started = 0;

static void music_ensure(void) {
    if (!g_mring) g_mring = (short*)pmem_alloc_block();
}

// Doom шлёт 16-бит моно PCM. Возвращает число принятых сэмплов.
int music_feed(const short* buf, unsigned int n) {
    music_ensure();
    if (!g_mring) return 0;
    unsigned int accepted = 0;
    for (unsigned int i = 0; i < n; i++) {
        unsigned int nxt = (g_mwr + 1u) % MRING;
        if (nxt == g_mrd) break;      // буфер полон
        g_mring[g_mwr] = buf[i];
        g_mwr = nxt;
        accepted++;
    }
    if (accepted && !g_mplaying) g_music_just_started = 1;
    if (accepted) g_mplaying = 1;
    return (int)accepted;
}

void music_stop(void) { g_mplaying = 0; g_mrd = g_mwr; }  // reader догоняет writer, не обнуляем оба
int  music_playing(void) { return g_mplaying; }

int music_take_just_started(void) {
    int v = g_music_just_started;
    g_music_just_started = 0;
    return v;
}

// Вызывается HDA-миксером раз на кадр (48 кГц).
unsigned int g_music_underrun = 0;   // счётчик пустых чтений (для отладки)
int music_next_sample(void) {
    if (!g_mring || !g_mplaying) return 0;
    static int last = 0;
    if (g_mrd == g_mwr) {
        // Кольцо пусто. Раньше возвращали 0 -> резкий скачок -> ЩЕЛЧОК.
        // Держим последний сэмпл с плавным затуханием (click-free).
        g_music_underrun++;
        last = last - (last >> 5);
        return last;
    }
    int s = g_mring[g_mrd];
    g_mrd = (g_mrd + 1u) % MRING;
    last = s;
    return s;
}
