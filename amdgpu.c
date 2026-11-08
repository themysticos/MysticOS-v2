// amdgpu.c - ЭТАП 1: определение AMD GPU через VDS (driver.h).
//
// Вывод — только в serial (COM1). На реальном ноуте serial нет, поэтому
// РЕЗУЛЬТАТ индицируем через Caps Lock LED:
//   * зажгли в начале probe — значит дошли до него;
//   * погасли в конце — успех (MMIO читается);
//   * ГОРИТ и система упала/висит — упало внутри probe (напр. MMIO не замаплен).
//
// ЭТАП 1: ничего не настраиваем, вывод картинки не трогаем.
#include "amdgpu.h"
#include "amdgpu_dcn.h"
#include "driver.h"
#include "serial.h"
#include "io.h"

// --- Caps Lock как индикатор результата (на ноуте без serial) ------------
// Пишем команду 0xED (Set LEDs) в контроллер клавиатуры через порт 0x60.
// Бит 2 (0x04) = Caps Lock. Ждём готовности порта 0x64 (бит 1 == 0).
// LED зажигаем в начале опасной операции и гасим при успехе — если система
// упадёт/зависнет, Caps Lock останется ГОРЕТЬ (визуальный сигнал «упало тут»).
static void kb_wait_write(void) {
    int t = 100000;
    while (t-- && (inb(0x64) & 0x02)) { }
}
static void kb_send(unsigned char b) {
    kb_wait_write();
    outb(0x60, b);
}
void amdgpu_caps_led(int on) {
    kb_send(0xED);            // Set LEDs command
    kb_send(on ? 0x04 : 0x00); // 0x04 = Caps Lock on; 0x00 = off
}

// Смещения сегментов DCN внутри MMIO (vega10_ip_offset.h).
#define DCN_SEG0  0x00000012u
#define DCN_SEG1  0x000000C0u
#define DCN_SEG2  0x000034C0u

static unsigned int g_mmio = 0;
static int g_ready = 0;

// Вывод ТОЛЬКО в serial. console.c пишет в VBE LFB (fb_put_pixel), а VBE
// на этом железе работает плохо — трогать LFB на этапе теста нельзя.
static void dprint(const char* t) { serial_print(t); }
static void dint(int v)           { serial_print_int(v); }
static void dhex(unsigned int v)  { serial_print_hex(v); }
static void dflush(void)          { /* no-op: только serial */ }

// --- match: наш ли это GPU -------------------------------------------------
static int amdgpu_match(const device_t* dev) {
    if (dev->bus != BUS_PCI) return 0;
    if (dev->vendor != 0x1002) return 0;                 // не AMD
    unsigned int base = (dev->class_code >> 16) & 0xFF;
    if (base != 0x03) return 0;                          // не display controller
    return 1;
}

// --- probe: найти MMIO, пробно прочитать --------------------------------
static int amdgpu_probe(device_t* dev) {
    // СИГНАЛ: зажигаем Caps Lock — значит, дошли до probe. Если система
    // упадёт дальше (на MMIO), Caps Lock останется ГОРЕТЬ.
    amdgpu_caps_led(1);
    dprint("[amdgpu] probe: AMD GPU ");
    dint(dev->bus); dprint(":");
    dint(dev->slot); dprint(".");
    dint(dev->func);
    dprint(" devid=0x"); dhex(dev->device);
    dprint(" class=0x"); dhex(dev->class_code);
    dprint("\n");
    dflush();

    unsigned int mmio = dev->bar[2];
    if (mmio == 0 || (mmio & 0x1)) mmio = dev->bar[5];   // не MMIO — BAR5
    if (mmio == 0 || (mmio & 0x1)) {
        dprint("[amdgpu] no MMIO BAR\n"); dflush();
        amdgpu_caps_led(0);   // MMIO нет — тоже гасим (не «упало», а «не найдено»)
        return -1;
    }
    mmio &= 0xFFFFFFF0u;
    g_mmio = mmio;
    dprint("[amdgpu] MMIO BAR = 0x"); dhex(mmio); dprint("\n");
    dprint("[amdgpu] reading DCN regs (may fault)...\n");
    dflush();   // <-- последняя строка ТОЧНО видна, если дальше #PF

    // Опасное чтение. Если MMIO не замаплен — тут будет #PF/зависание.
    volatile unsigned int* d0 = (volatile unsigned int*)(mmio + (DCN_SEG0 << 2));
    volatile unsigned int* d1 = (volatile unsigned int*)(mmio + (DCN_SEG1 << 2));
    volatile unsigned int* d2 = (volatile unsigned int*)(mmio + (DCN_SEG2 << 2));
    unsigned int v0 = *d0, v1 = *d1, v2 = *d2;

    dprint("[amdgpu] DCN seg0 = 0x"); dhex(v0); dprint("\n");
    dprint("[amdgpu] DCN seg1 = 0x"); dhex(v1); dprint("\n");
    dprint("[amdgpu] DCN seg2 = 0x"); dhex(v2); dprint("\n");

    g_ready = 1;
    dprint("[amdgpu] probe OK (MMIO readable)\n");
    dflush();

    // ЭТАП 2: свой DCN-драйвер — инициализация вывода (заливка цвета).
    // Caps Lock мигает по этапам (1..6). Если зависнет — увидим, где.
    dprint("[amdgpu] starting DCN init...\n");
    int rc = amdgpu_dcn_init(mmio);
    dprint("[amdgpu] DCN init rc="); dint(rc); dprint("\n");

    // СИГНАЛ: всё прошло — гасим Caps Lock (вернули в норму).
    amdgpu_caps_led(0);
    return 0;
}

static const driver_t AMDGPU_DRIVER = {
    "amdgpu",
    DRV_GPU,
    DRV_LEVEL_NORMAL,
    amdgpu_match,
    amdgpu_probe,
    0
};

void amdgpu_driver_register(void) {
    driver_register(&AMDGPU_DRIVER);
}

int amdgpu_ready(void) { return g_ready; }
unsigned int amdgpu_mmio_base(void) { return g_mmio; }
