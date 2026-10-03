// driver.c - реализация VDS (Virtual Driver System).
//
// См. driver.h — там описание модели.
#include "driver.h"
#include "pci.h"
#include "console.h"

static const driver_t* g_drivers[DRV_MAX_DRIVERS];
static int g_driver_count = 0;

void driver_init(void) {
    g_driver_count = 0;
    for (int i = 0; i < DRV_MAX_DRIVERS; i++) g_drivers[i] = 0;
}

int driver_register(const driver_t* drv) {
    if (!drv || !drv->name) return 0;
    if (g_driver_count >= DRV_MAX_DRIVERS) return 0;
    // не дублировать по имени
    for (int i = 0; i < g_driver_count; i++)
        if (g_drivers[i] && g_drivers[i]->name == drv->name) return 0;
    g_drivers[g_driver_count++] = drv;
    return 1;
}

int driver_count(void) { return g_driver_count; }

const driver_t* driver_get(int i) {
    if (i < 0 || i >= g_driver_count) return 0;
    return g_drivers[i];
}

const driver_t* driver_find(const char* name) {
    for (int i = 0; i < g_driver_count; i++) {
        const driver_t* d = g_drivers[i];
        if (!d || !d->name) continue;
        const char* a = d->name;
        const char* b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return d;
    }
    return 0;
}

// ============================================================
// Скан PCI через callback.
// ============================================================
static int g_probe_count = 0;

static int scan_cb(int bus, int slot, int func,
                   unsigned int vendor, unsigned int device,
                   unsigned int class_code) {
    device_t dev;
    dev.bus = BUS_PCI;
    dev.bus_num = bus;
    dev.slot = slot;
    dev.func = func;
    dev.vendor = vendor;
    dev.device = device;
    dev.class_code = class_code;
    dev.irq = pci_irq_line(bus, slot, func);
    dev.priv = 0;

    // BAR'ы
    for (int b = 0; b < 6; b++) {
        unsigned int lo = 0, hi = 0, sz = 0;
        int is_io = 0, is_64 = 0;
        if (pci_bar_info(bus, slot, func, b, &lo, &hi, &sz, &is_io, &is_64)) {
            dev.bar[b] = lo;
            if (is_64) b++;   // следующий BAR — старшая часть; пропускаем
        } else {
            dev.bar[b] = 0;
        }
    }

    // Ищем драйвер, чей match() вернёт 1.
    for (int i = 0; i < g_driver_count; i++) {
        const driver_t* d = g_drivers[i];
        if (!d || !d->match) continue;
        if (!d->match(&dev)) continue;
        if (d->probe) {
            int rc = d->probe(&dev);
            if (rc == 0) g_probe_count++;
        }
        break;   // первое совпадение — берём
    }
    return 1;   // продолжаем скан
}

int driver_scan(void) {
    g_probe_count = 0;
    pci_enumerate(scan_cb);
    return g_probe_count;
}
