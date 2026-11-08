// vss.c - реализация VSS (Virtual Sound System).
//
// См. vss.h. Полный аналог vfs.c: таблица драйверов + делегирование.
// Ядро не знает, какой драйвер активен; первый успешный probe() побеждает.
#include "vss.h"

#define VSS_MAX_DRIVERS 4
static const sound_driver_t* g_drivers[VSS_MAX_DRIVERS];
static int g_driver_count = 0;
static const sound_driver_t* g_active = 0;

void vss_init(void) {
    g_driver_count = 0;
    g_active = 0;
    for (int i = 0; i < VSS_MAX_DRIVERS; i++) g_drivers[i] = 0;
}

int vss_register(const sound_driver_t* drv) {
    if (!drv || !drv->name) return 0;
    if (g_driver_count >= VSS_MAX_DRIVERS) return 0;
    // не дублировать по имени
    for (int i = 0; i < g_driver_count; i++)
        if (g_drivers[i] && g_drivers[i]->name == drv->name) return 0;
    g_drivers[g_driver_count++] = drv;
    return 1;
}

// Перебор драйверов: первый, чей probe() вернёт 1, становится активным.
int vss_probe_all(void) {
    for (int i = 0; i < g_driver_count; i++) {
        const sound_driver_t* d = g_drivers[i];
        if (!d || !d->probe) continue;
        if (d->probe()) {
            g_active = d;
            return 1;
        }
    }
    g_active = 0;
    return 0;
}

int vss_ready(void) {
    return (g_active && (!g_active->ready || g_active->ready())) ? 1 : 0;
}

const char* vss_name(void) {
    return g_active ? g_active->name : "none";
}

void vss_set_rate(unsigned int rate) {
    if (g_active && g_active->set_rate) g_active->set_rate(rate);
}

void vss_play_pcm(const unsigned char* buf, unsigned int len, unsigned int rate) {
    if (g_active && g_active->play_pcm) g_active->play_pcm(buf, len, rate);
}

void vss_beep(unsigned int freq_hz, unsigned int ms) {
    if (g_active && g_active->beep) g_active->beep(freq_hz, ms);
}

void vss_poll(void) {
    if (g_active && g_active->poll) g_active->poll();
}
