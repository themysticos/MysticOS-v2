// dg_sound_mysticos.c - звуковой модуль Doom для MysticOS.
// Вывод PCM идёт через SYS_SOUND_PCM (int 0x80, 121) -> VSS -> активный
// драйвер (сейчас Intel HDA). Doom не знает, какое железо под ним.
#include "doomgeneric/doomgeneric/i_sound.h"
#include "doomgeneric/doomgeneric/w_wad.h"
#include "doomgeneric/doomgeneric/z_zone.h"
#include "doomgeneric/doomgeneric/m_misc.h"
#include <string.h>
#include <stdlib.h>

#define NUM_CHANNELS 8
static boolean sound_initialized = 0;
// Переменные, нужные i_sound.c под FEATURE_SOUND (libsamplerate нам не нужен).
int   use_libsamplerate = 0;
float libsamplerate_scale = 1.0f;
static int g_playing = 0;

boolean DG_sound_init(boolean use_sfx_prefix) { (void)use_sfx_prefix; sound_initialized = 1; return 1; }
void DG_sound_shutdown(void) { sound_initialized = 0; }

int DG_sound_getlump(sfxinfo_t* sfx) {
    char name[9];
    if (sfx->link) sfx = sfx->link;
    M_snprintf(name, sizeof(name), "ds%s", sfx->name);
    return W_GetNumForName(name);
}

void DG_sound_update(void) { }
void DG_sound_update_params(int ch, int vol, int sep) { (void)ch;(void)vol;(void)sep; }
boolean DG_sound_is_playing(int ch) { (void)ch; return g_playing; }
void DG_sound_precache(sfxinfo_t* s, int n) { (void)s;(void)n; }

// DMX: 03 00 | rate(2) | len(4) | 16 skip | PCM | 16 skip.
static const unsigned char* parse_dmx(const unsigned char* lump, int lumplen, int* out_len, int* out_rate) {
    if (lumplen < 8) return 0;
    if (lump[0] != 0x03 || lump[1] != 0x00) return 0;
    int rate = (lump[3] << 8) | lump[2];
    unsigned int len = (unsigned)lump[4] | ((unsigned)lump[5]<<8) | ((unsigned)lump[6]<<16) | ((unsigned)lump[7]<<24);
    if ((int)len > lumplen - 8 || len <= 48) return 0;
    const unsigned char* data = lump + 16;
    len -= 32;
    *out_len = (int)len; *out_rate = rate;
    return data;
}

int DG_sound_start(sfxinfo_t* sfxinfo, int channel, int vol, int sep) {
    (void)channel; (void)vol; (void)sep;
    if (!sound_initialized) return -1;
    unsigned char* pcm = (unsigned char*)sfxinfo->driver_data;
    unsigned int pcm_len = 0, rate = 11025;
    if (pcm) {
        rate = *(unsigned int*)pcm; pcm_len = *(unsigned int*)(pcm+4); pcm += 8;
    } else {
        int lump = DG_sound_getlump(sfxinfo);
        if (lump < 0) return -1;
        int lumplen = W_LumpLength(lump);
        const unsigned char* lumpd = W_CacheLumpNum(lump, PU_STATIC);
        int len = 0, r = 0;
        const unsigned char* dmx = parse_dmx((const unsigned char*)lumpd, lumplen, &len, &r);
        if (!dmx) { W_ReleaseLumpNum(lump); return -1; }
        pcm_len = len; rate = (unsigned int)r;
        pcm = malloc(8 + pcm_len);
        if (!pcm) { W_ReleaseLumpNum(lump); return -1; }
        *(unsigned int*)pcm = rate; *(unsigned int*)(pcm+4) = pcm_len;
        memcpy(pcm + 8, dmx, pcm_len);
        sfxinfo->driver_data = pcm;
        W_ReleaseLumpNum(lump);
    }
    if (!pcm || pcm_len == 0) return -1;
    // ВАЖНО: ebx = указатель на МАССИВ аргументов (не сам буфер!).
    unsigned int _a[3]; _a[0] = (unsigned int)pcm; _a[1] = pcm_len; _a[2] = rate;
    __asm__ volatile ("int $0x80" :: "a"(121), "b"(_a) : "memory");
    g_playing = 1;
    return 0;
}

void DG_sound_stop(int channel) { (void)channel; g_playing = 0; }

// Отправить готовый PCM музыки (16-бит моно) в ядро (SYS_MUSIC_PCM=122).
int ms_music_pcm(const short* buf, unsigned int n) {
    unsigned int _a[2]; _a[0] = (unsigned int)buf; _a[1] = n;
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(122), "b"(_a) : "memory");
    return ret;
}

static snddevice_t snd_devs[] = { SNDDEVICE_SB, SNDDEVICE_PCSPEAKER };

sound_module_t DG_sound_module = {
    snd_devs, 2,
    DG_sound_init, DG_sound_shutdown, DG_sound_getlump, DG_sound_update,
    DG_sound_update_params, DG_sound_start, DG_sound_stop,
    DG_sound_is_playing, DG_sound_precache,
};
