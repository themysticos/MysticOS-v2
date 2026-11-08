// apic.h - Local APIC + APIC Timer для MysticOS 2.
//
// Зачем: под WHPX классический PIC (8259) не доставляет IRQ0 (таймер).
// APIC — современный контроллер прерываний, который WHPX поддерживает.
// Local APIC Timer шлёт прерывания на вектор 0x20 (как PIC IRQ0),
// поэтому код, ждущий timer_ticks(), продолжает работать.
#ifndef APIC_H
#define APIC_H

// Инициализировать Local APIC и запустить APIC Timer на hz Гц.
// Возвращает 1 при успехе, 0 если APIC недоступен.
int apic_init(unsigned int hz);

// Послать EOI в Local APIC (вместо PIC). Вызывается из irq_handler.
void apic_eoi(void);

// 1 если APIC активен (тогда PIC-таймер отключён, EOI идёт в APIC).
int apic_active(void);

// Настроить IO-APIC: разрешить внешние IRQ (мышь, клава, PCI) и направить
// их на наш LAPIC. Нужно, т.к. под WHPX PIC не доставляет прерывания.
// Вызывать после apic_init. Возвращает 1 при успехе.
int ioapic_init(void);

// Прописать редирект для одного IRQ (0..23): vector = 0x20 + irq.
void ioapic_route(unsigned char irq);

#endif // APIC_H
