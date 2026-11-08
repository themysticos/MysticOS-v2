// apic.c - Local APIC + APIC Timer для MysticOS 2.
//
// Проблема: под WHPX PIC (8259) не доставляет IRQ0 -> таймер мёртв.
// Решение: Local APIC Timer, встроенный в CPU. Он работает в WHPX/TCG/на железе.
//
// Идея минимально-инвазивная: APIC Timer шлёт прерывание на ВЕКТОР 0x20
// (тот же, что PIC IRQ0). IDT-гейт irq0 уже стоит на 0x20, irq_handler
// вычитает 0x20 и получает irq==0 -> g_ticks++. Меняем только EOI:
// вместо outb(PIC_EOI) шлём запись в LAPIC EOI-регистр.
//
// Local APIC MMIO обычно по адресу 0xFEE00000 (можно уточнить из MSR 0x1B).
#include "apic.h"
#include "io.h"
#include "serial.h"

#define LAPIC_DEFAULT_BASE 0xFEE00000u

// Регистры LAPIC (смещения от базы).
#define LAPIC_ID        0x020
#define LAPIC_VER       0x030
#define LAPIC_TPR       0x080   // Task Priority
#define LAPIC_EOI       0x0B0   // End Of Interrupt
#define LAPIC_SVR       0x0F0   // Spurious Vector Register
#define LAPIC_LVT_TMR   0x320   // LVT Timer
#define LAPIC_TMR_INIT  0x380   // Initial Count
#define LAPIC_TMR_CUR   0x390   // Current Count
#define LAPIC_TMR_DIV   0x3E0   // Divide Configuration

static volatile unsigned int* g_lapic = 0;
static int g_active = 0;

static unsigned int lapic_read(unsigned int reg) {
    return g_lapic[reg / 4];
}
static void lapic_write(unsigned int reg, unsigned int val) {
    g_lapic[reg / 4] = val;
}

// Прочитать базу LAPIC из MSR 0x1B (IA32_APIC_BASE).
static unsigned int lapic_base_from_msr(void) {
    unsigned int lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    (void)hi;
    // Биты 12..35 — физический адрес базы (страница).
    return lo & 0xFFFFF000u;
}

// Включить LAPIC: установить бит 11 (APIC enable) в IA32_APIC_BASE.
static void lapic_hw_enable(void) {
    unsigned int lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    lo |= (1u << 11);   // APIC global enable
    __asm__ volatile ("wrmsr" :: "a"(lo), "d"(hi), "c"(0x1B));
}

int apic_init(unsigned int hz) {
    // 1. База LAPIC (из MSR или дефолт).
    unsigned int base = lapic_base_from_msr();
    if (base == 0) base = LAPIC_DEFAULT_BASE;
    g_lapic = (volatile unsigned int*)base;

    // 2. Включить APIC на уровне CPU.
    lapic_hw_enable();

    // 3. Проверка: читаем версию LAPIC. Если 0 или 0xFFFFFFFF — APIC нет.
    unsigned int ver = lapic_read(LAPIC_VER);
    serial_print("[apic] base="); serial_print_hex(base);
    serial_print(" ver="); serial_print_hex(ver);
    serial_print("\n");
    if (ver == 0 || ver == 0xFFFFFFFFu) {
        serial_print("[apic] not present\n");
        g_lapic = 0;
        return 0;
    }

    // 4. SVR: разрешить LAPIC, spurious-вектор = 0xFF.
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | 0x100u | 0xFFu);
    // 5. TPR = 0 (принимать все приоритеты).
    lapic_write(LAPIC_TPR, 0);

    // 6. Настроить Timer:
    //    LVT Timer: вектор 0x20, периодический режим (бит 17 = 0 -> one-shot,
    //    1 = periodic). Нам нужен periodic: бит 17 = 1.
    //    Регистр 0x320: [7:0]=вектор, [17]=periodic, [16]=mask.
    lapic_write(LAPIC_LVT_TMR, 0x20u | (1u << 17));
    //    Делитель: 0x3 = делить на 16.
    lapic_write(LAPIC_TMR_DIV, 0x3);

    // 7. Initial Count. Частота APIC-таймера = шина CPU / делитель.
    //    Точную частоту не знаем, поэтому берём разумное число и подстроим
    //    по факту (проверим, растут ли тики). QEMU/WHPX APIC timer ~ 100 МГц
    //    на шине. При делителе 16 и hz=100: count ~ (100e6/16)/100 = 62500.
    // Частота APIC timer в QEMU/WHPX — НЕ 100 МГц, а выше (эмпирически
    // ~200 МГц: при 100 МГц таймер тикал ровно в 2 раза быстрее).
    // Берём 200 МГц как базу. Точную частоту можно потом уточнить
    // калибровкой через PIT.
    unsigned int count = 200000000u / 16u / (hz ? hz : 100u);
    if (count == 0) count = 1;
    lapic_write(LAPIC_TMR_INIT, count);

    g_active = 1;
    serial_print("[apic] timer started, count="); serial_print_int((int)count);
    serial_print("\n");
    return 1;
}

void apic_eoi(void) {
    if (g_lapic) lapic_write(LAPIC_EOI, 0);
}

int apic_active(void) { return g_active; }

// ============================================================
// IO-APIC
// ============================================================
// Внешние прерывания (мышь IRQ12, клава IRQ1, PCI) идут через IO-APIC.
// Его MMIO — обычно 0xFEC00000. Регистры:
//   IOREGSEL (0x00): выбор регистра
//   IOWIN    (0x10): данные
// Редиректы: по 2 dword на IRQ, начиная с 0x10 (reg 16).
#define IOAPIC_BASE   0xFEC00000u
#define IOAPIC_REGSEL 0x00
#define IOAPIC_IOWIN  0x10

static volatile unsigned int* g_ioapic = 0;

static unsigned int ioapic_read(unsigned int reg) {
    g_ioapic[IOAPIC_REGSEL / 4] = reg;
    return g_ioapic[IOAPIC_IOWIN / 4];
}
static void ioapic_write(unsigned int reg, unsigned int val) {
    g_ioapic[IOAPIC_REGSEL / 4] = reg;
    g_ioapic[IOAPIC_IOWIN / 4] = val;
}

// Прописать редирект: irq -> vector (0x20+irq), destination = LAPIC ID.
void ioapic_route(unsigned char irq) {
    if (!g_ioapic) return;
    unsigned int lapic_id = lapic_read(LAPIC_ID) >> 24;   // старший байт
    unsigned int low  = (0x20u + irq);          // вектор, fixed, physical, edge
    unsigned int high = lapic_id << 24;         // destination = наш LAPIC
    unsigned int reg = 0x10 + irq * 2;
    ioapic_write(reg + 1, high);   // сначала high (destination)
    ioapic_write(reg, low);        // потом low (включает прерывание)
}

int ioapic_init(void) {
    g_ioapic = (volatile unsigned int*)IOAPIC_BASE;
    unsigned int ver = ioapic_read(0x01);   // IOAPICVER
    serial_print("[ioapic] ver="); serial_print_hex(ver);
    serial_print("\n");
    if (ver == 0 || ver == 0xFFFFFFFFu) {
        serial_print("[ioapic] not present\n");
        g_ioapic = 0;
        return 0;
    }
    // ВАЖНО: НЕ маршрутизируем IRQ0 и IRQ2!
    //  - IRQ0 (PIT): таймер у нас уже даёт LAPIC Timer (вектор 0x20).
    //    Если ещё и PIT пойдёт через IO-APIC на тот же вектор — два
    //    источника бьют в один обработчик (двойные тики, шум).
    //  - IRQ2 (cascade PIC2->PIC1): PIC выключен, cascade не нужен.
    // Маршрутизируем ТОЛЬКО то, что реально работает через прерывания:
    // клавиатура (IRQ1) и PS/2-мышь (IRQ12).
    //
    // ВАЖНО: НЕ трогаем IRQ14/15 (ATA/IDE) — диск у нас на PIO-опросе
    // (ata.c читает порты вручную). Если ATA шлёт IRQ14 через IO-APIC,
    // а обработчика нет -> лавина прерываний (видели irq:14=3764!)
    // и система захлёбывается. Также не трогаем IRQ0/2 (таймер/cascade).
    // PCI-устройства (virtio-net, HDA) у нас тоже на опросе.
    ioapic_route(1);    // клавиатура
    ioapic_route(12);   // PS/2 мышь
    serial_print("[ioapic] routes set (1=kb, 12=mouse)\n");
    return 1;
}
