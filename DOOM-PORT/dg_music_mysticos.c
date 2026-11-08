// dg_music_mysticos.c - музыкальный модуль Doom для MysticOS.
// Музыка в WAD — формат MUS. Отдаём данные ядру (SYS_MUSIC_PLAY), а оно
// синтезирует ноты своим волновым синтезатором в HDA-миксер.
#include "doomgeneric/doomgeneric/i_sound.h"
#include "../include/ms/syscall.h"
#include <stdlib.h>

static int mus_handle = 0;

static boolean DM_Init(void) { return 1; }
static void DM_Shutdown(void) { __ms_sc0(SYS_MUSIC_STOP); }
static void DM_SetVolume(int v) { (void)v; }
static void DM_Pause(void) { __ms_sc0(SYS_MUSIC_STOP); }
static void DM_Resume(void) { }

// Регистрация песни: возвращаем указатель на данные + длину (handle).
// len кладём в начало буфера, данные — следом.
static void* DM_Register(void* data, int len) {
    if (!data || len <= 0) return 0;
    unsigned char* h = (unsigned char*)malloc(8 + len);
    if (!h) return 0;
    *(unsigned int*)h = (unsigned int)len;
    for (int i = 0; i < len; i++) h[8 + i] = ((unsigned char*)data)[i];
    return h;
}

static void DM_Unregister(void* h) { if (h) free(h); }

static void DM_Play(void* h, boolean loop) {
    if (!h) return;
    unsigned int len = *(unsigned int*)h;
    unsigned char* mus = (unsigned char*)h + 8;
    unsigned int args[3];
    args[0] = (unsigned int)mus;
    args[1] = len;
    args[2] = loop ? 1u : 0u;
    __asm__ volatile ("int $0x80" :: "a"(SYS_MUSIC_PLAY), "b"(args) : "memory");
    mus_handle = 1;
}

static void DM_Stop(void) {
    __ms_sc0(SYS_MUSIC_STOP);
    mus_handle = 0;
}

static boolean DM_IsPlaying(void) { return mus_handle; }
static void DM_Poll(void) { }

static snddevice_t mus_devs[] = { SNDDEVICE_SB, SNDDEVICE_GENMIDI, SNDDEVICE_ADLIB };

music_module_t DG_music_module = {
    mus_devs, 3,
    DM_Init, DM_Shutdown, DM_SetVolume, DM_Pause, DM_Resume,
    DM_Register, DM_Unregister, DM_Play, DM_Stop, DM_IsPlaying, DM_Poll,
};
