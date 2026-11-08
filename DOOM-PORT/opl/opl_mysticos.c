// opl_mysticos.c - OPL2/OPL3 для MysticOS. Заменяет opl.c (диспетчер SDL).
#include "opl.h"
#include "opl3.h"
#include <stdint.h>

#include <string.h>   // memset (есть в libc Doom)

#define OPL_MAX_CALLBACKS 64

typedef struct { int used; uint64_t time; opl_callback_t cb; void* data; } opl_cb_t;

static opl3_chip    g_chip;
static unsigned int g_rate = 48000;
static uint64_t     g_us = 0;
static opl_cb_t     g_cbs[OPL_MAX_CALLBACKS];
static int          g_paused = 0;
static int          g_lock = 0;
static int          g_inited = 0;
static uint16_t     g_regnum = 0;

unsigned int opl_sample_rate = 48000;

void OPL_SetSampleRate(unsigned int rate) { if (rate) g_rate = rate; }

opl_init_result_t OPL_Init(unsigned int port_base) {
    (void)port_base;
    OPL3_Reset(&g_chip, g_rate);
    g_us = 0; g_paused = 0; g_lock = 0;
    for (int i = 0; i < OPL_MAX_CALLBACKS; i++) g_cbs[i].used = 0;
    g_inited = 1;
    return OPL_INIT_OPL3;
}

void OPL_Shutdown(void) { g_inited = 0; }

void OPL_WritePort(opl_port_t port, unsigned int value) {
    if (port == OPL_REGISTER_PORT || port == OPL_REGISTER_PORT_OPL3) g_regnum = (uint16_t)value;
    else OPL3_WriteRegBuffered(&g_chip, g_regnum, (uint8_t)value);
}
unsigned int OPL_ReadPort(opl_port_t port) { (void)port; return 0; }
unsigned int OPL_ReadStatus(void) { return 0; }
void OPL_WriteRegister(int reg, int value) { OPL3_WriteRegBuffered(&g_chip, (uint16_t)reg, (uint8_t)value); }

void OPL_InitRegisters(int opl3) {
    int r;
    for (r = OPL_REGS_LEVEL; r <= OPL_REGS_LEVEL + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r, 0x3f);
    for (r = OPL_REGS_ATTACK; r <= OPL_REGS_WAVEFORM + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r, 0x00);
    for (r = 1; r < OPL_REGS_LEVEL; ++r) OPL_WriteRegister(r, 0x00);
    OPL_WriteRegister(OPL_REG_TIMER_CTRL, 0x60);
    OPL_WriteRegister(OPL_REG_TIMER_CTRL, 0x80);
    OPL_WriteRegister(OPL_REG_WAVEFORM_ENABLE, 0x20);
    if (opl3) {
        OPL_WriteRegister(OPL_REG_NEW, 0x01);
        for (r = OPL_REGS_LEVEL; r <= OPL_REGS_LEVEL + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r | 0x100, 0x3f);
        for (r = OPL_REGS_ATTACK; r <= OPL_REGS_WAVEFORM + OPL_NUM_OPERATORS; ++r) OPL_WriteRegister(r | 0x100, 0x00);
        for (r = 1; r < OPL_REGS_LEVEL; ++r) OPL_WriteRegister(r | 0x100, 0x00);
    }
    OPL_WriteRegister(OPL_REG_FM_MODE, 0x40);
    if (opl3) OPL_WriteRegister(OPL_REG_NEW, 0x01);
}

void OPL_SetCallback(uint64_t us, opl_callback_t callback, void* data) {
    for (int i = 0; i < OPL_MAX_CALLBACKS; i++) {
        if (!g_cbs[i].used) {
            g_cbs[i].used = 1; g_cbs[i].time = g_us + us;
            g_cbs[i].cb = callback; g_cbs[i].data = data; return;
        }
    }
}
void OPL_ClearCallbacks(void) { for (int i = 0; i < OPL_MAX_CALLBACKS; i++) g_cbs[i].used = 0; }
void OPL_AdjustCallbacks(float factor) {
    for (int i = 0; i < OPL_MAX_CALLBACKS; i++)
        if (g_cbs[i].used) g_cbs[i].time = g_us + (uint64_t)((double)(g_cbs[i].time - g_us) * factor);
}
void OPL_Lock(void) { g_lock = 1; }
void OPL_Unlock(void) { g_lock = 0; }
void OPL_SetPaused(int paused) { g_paused = paused; }
void OPL_Delay(uint64_t us) { (void)us; }
int opl_mysticos_active(void) { return g_inited; }

void opl_mysticos_render(short* out, unsigned int nsamples) {
    if (!g_inited) {
        unsigned int k; for (k = 0; k < nsamples * 2; k++) out[k] = 0;
        return;
    }
    /* Generate while invoking i_oplmusic callbacks at exact moments
       (small chunks). Otherwise notes fire in a batch at buffer end. */
    unsigned int done = 0;
    while (done < nsamples) {
        unsigned int chunk = nsamples - done;
        if (chunk > 64) chunk = 64;   /* ~1.3 ms: note-on accuracy */
        OPL3_GenerateStream(&g_chip, out + (unsigned long)done * 2, chunk);
        done += chunk;
        if (g_paused) continue;
        g_us += (uint64_t)(((unsigned long)chunk * 1000000ul) / g_rate);
        int m;
        for (m = 0; m < OPL_MAX_CALLBACKS; m++) {
            if (g_cbs[m].used && g_us >= g_cbs[m].time) {
                opl_callback_t cb = g_cbs[m].cb;
                void *data = g_cbs[m].data;
                g_cbs[m].used = 0;
                if (cb && !g_lock) cb(data);
            }
        }
    }
}
